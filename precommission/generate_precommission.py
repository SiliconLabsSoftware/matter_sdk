"""Generate random Matter Direct / Thread Direct precommissioning data.

Produces a pair of `provisioning-config-*.yaml` files (initiator + listener) in the
format consumed by `precommission.py`. See README.md for the meaning of every
field.

Usage:
    generate_precommission.py --chip-cert <chip-cert> --rcac <root cert> [--icac <intermediate cert>]
        --ca-key <issuer key> --out out/<name>/ [--admin-node-id 112233] [--seed N]
    generate_precommission.py --inputs <input.json> [--out out/<name>/] [other options]

`--inputs` reads option values from a JSON object keyed by option name. Paths in it are relative to the JSON file,
null means the default, and command-line options override it.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import ipaddress
import json
import os
import random
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import yaml

import active_dataset_codec
import network_info_codec

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "src" / "controller" / "python"))
from matter.tlv import TLVReader, TLVWriter, uint  # noqa: E402 isort:skip

DEFAULT_ADMIN_NODE_ID = 112233  # Sample commissioner subject; configurable for the target fabric.
DEFAULT_FABRIC_ID = 1
DEFAULT_VENDOR_ID = 0xFFF1  # Matter test VID
DEFAULT_IPK_EPOCH_KEY = b"temporary ipk 01"  # Fixed development/sample input
DEFAULT_PORT = 5540  # Matter UDP port.
DEFAULT_MRP_IDLE_MS = 2000
DEFAULT_MRP_ACTIVE_MS = 2000
DEFAULT_MRP_ACTIVE_THRESHOLD_MS = 4000
DEFAULT_CHANNEL = 25  # Example operating channel; Thread Direct wake/listen remains on channel 20.
THREAD_DIRECT_WAKE_CHANNEL = 20  # Fixed by the Thread Direct protocol.
PRECOMMISSIONED_MARKER = "01"  # One-byte true payload, matching SilabsConfig::WriteConfigValue(bool).

FABRIC_INDEX = 1
CERT_VALID_FROM = "2021-01-01 00:00:00"
CERT_LIFETIME_DAYS = 4294967295
MAX_OPERATIONAL_NODE_ID = 0xFFFF_FFEF_FFFF_FFFF
OP_KEYPAIR_SIZE = 97  # Uncompressed P-256 public key (65) || private key (32).

THREAD_CHANNEL_MASK_PAGE0 = "001fffe0"  # Channels 11-26, bit-reversed as in the Thread TLV.
THREAD_SECURITY_POLICY = {"rotation_time": 672, "flags": "f7f8"}
THREAD_NETWORK_NAME_PREFIX = "ThreadDirect"  # Plus 4 hex PAN ID digits: 16 bytes, the Thread maximum.

ACL_PRIVILEGE_OPERATE = 3
ACL_PRIVILEGE_ADMINISTER = 5
ACL_AUTH_MODE_CASE = 2
BINDING_STORAGE_VERSION = 1
BINDING_NO_NEXT_ENTRY = 0xFF
KEYSET_NO_NEXT = 0xFFFF
KEYSET_MAX_KEYS = 3

CERT_TAG_PUBLIC_KEY = 9
CERT_TAG_SUBJECT = 6
DN_TAG_NODE_ID = 17
DN_TAG_FABRIC_ID = 21


def verify_or_raise(condition: object, error: Exception) -> None:
    """Raises `error` if `condition` is false; Python counterpart of `VerifyOrReturnError(expr, code)`."""
    if not condition:
        raise error


# --- Encoding and key derivation ---

def tlv_hex(value: dict) -> str:
    """Encodes an anonymous TLV structure with `matter.tlv.TLVWriter` and returns it as hex."""
    writer = TLVWriter()
    writer.put(None, value)
    return bytes(writer.encoding).hex()


def le_hex(value: int, size: int) -> str:
    """Returns `value` as a little-endian hex string of `size` bytes (NVM3 integer encoding)."""
    return value.to_bytes(size, "little").hex()


def random_bytes(rng: random.Random, size: int) -> bytes:
    return rng.getrandbits(8 * size).to_bytes(size, "big")


def hkdf_sha256(ikm: bytes, salt: bytes, info: bytes, length: int) -> bytes:
    """RFC 5869 HKDF-SHA256, limited to a single output block (length <= 32)."""
    verify_or_raise(length <= hashlib.sha256().digest_size, ValueError("HKDF length exceeds one block"))
    prk = hmac.new(salt or bytes(32), ikm, hashlib.sha256).digest()
    return hmac.new(prk, info + b"\x01", hashlib.sha256).digest()[:length]


def link_local_from_ext_address(ext_address: bytes) -> str:
    """Returns fe80:: + ext_address with the first byte's U/L bit flipped (0x02)."""
    iid = bytes([ext_address[0] ^ 0x02]) + ext_address[1:]
    return str(ipaddress.IPv6Address(bytes.fromhex("fe80000000000000") + iid))


def random_ext_address(rng: random.Random) -> bytes:
    """8 random bytes, first byte `& 0xFC | 0x02` (unicast, locally administered)."""
    raw = random_bytes(rng, 8)
    return bytes([(raw[0] & 0xFC) | 0x02]) + raw[1:]


def decode_cert(cert_tlv: bytes) -> dict:
    """Decodes a Matter TLV certificate into {public_key, node_id, fabric_id} (IDs are None if absent)."""
    cert = TLVReader(cert_tlv).get()["Any"]
    subject = dict(cert[CERT_TAG_SUBJECT])  # The DN is a TLV list; iterating yields (tag, value).
    return {
        "public_key": bytes(cert[CERT_TAG_PUBLIC_KEY]),
        "node_id": subject.get(DN_TAG_NODE_ID),
        "fabric_id": subject.get(DN_TAG_FABRIC_ID),
    }


# --- CA chain and NOC issuance (chip-cert wrapper) ---

def run_chip_cert(chip_cert: Path, *args: str) -> None:
    proc = subprocess.run([str(chip_cert), *args], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"chip-cert {' '.join(args[:3])} failed:\n{proc.stdout}{proc.stderr}")


def read_cert_tlv(chip_cert: Path, path: Path) -> bytes:
    """Returns a certificate file (PEM, DER, Matter TLV or base64) as Matter TLV."""
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "cert.chip"
        run_chip_cert(chip_cert, "convert-cert", str(path), str(out), "--chip")
        return out.read_bytes()


@dataclass(frozen=True)
class CA:
    """The CA that issues the NOCs, plus the fabric parameters both devices share."""

    rcac: bytes
    icac: bytes | None
    rcac_path: Path
    icac_path: Path | None
    key_path: Path  # Private key of the issuer: the ICAC if present, else the RCAC.
    fabric_id: int
    vendor_id: int
    ipk_epoch_key: bytes

    @property
    def compressed_fabric_id(self) -> bytes:
        """HKDF-SHA256(rootPub[1:], salt=fabricId BE8, info="CompressedFabric", 8)."""
        root_public_key = decode_cert(self.rcac)["public_key"]
        return hkdf_sha256(root_public_key[1:], self.fabric_id.to_bytes(8, "big"), b"CompressedFabric", 8)


def load_ca(
    chip_cert: Path,
    rcac_path: Path,
    icac_path: Path | None,
    ca_key_path: Path,
    fabric_id: int | None,
    vendor_id: int,
    ipk_epoch_key: bytes,
) -> CA:
    """Loads the CA. The fabric ID defaults to the one in the CA certificates, else 1, and must match it when present."""
    rcac = read_cert_tlv(chip_cert, rcac_path)
    icac = read_cert_tlv(chip_cert, icac_path) if icac_path else None
    cert_fabric_ids = {decode_cert(cert)["fabric_id"] for cert in (rcac, icac) if cert} - {None}
    verify_or_raise(len(cert_fabric_ids) <= 1, ValueError("RCAC and ICAC have different fabric IDs"))
    if fabric_id is None:
        fabric_id = next(iter(cert_fabric_ids), DEFAULT_FABRIC_ID)
    verify_or_raise(cert_fabric_ids <= {fabric_id}, ValueError(f"fabric ID 0x{fabric_id:x} does not match the CA certificates"))
    verify_or_raise(0 < fabric_id <= 0xFFFF_FFFF_FFFF_FFFF, ValueError("fabric ID must be a nonzero 64-bit value"))
    verify_or_raise(0 <= vendor_id <= 0xFFFF, ValueError("vendor ID must be 16-bit"))
    verify_or_raise(len(ipk_epoch_key) == 16, ValueError("IPK epoch key must be 16 bytes"))
    return CA(rcac, icac, rcac_path, icac_path, ca_key_path, fabric_id, vendor_id, ipk_epoch_key)


def issue_noc(ca: CA, chip_cert: Path, node_id: int) -> tuple[bytes, bytes]:
    """Generates an operational key pair, issues its NOC and validates the NOC against the CA chain.

    Returns (noc_tlv, op_keypair) where op_keypair is the public||private blob stored at NVM3 0x708.
    """
    issuer = ca.icac_path or ca.rcac_path
    with tempfile.TemporaryDirectory() as tmp:
        noc_path, key_path = Path(tmp) / "noc.chip", Path(tmp) / "noc.key"
        run_chip_cert(chip_cert, "gen-cert", "--type", "n", "--subject-chip-id", f"{node_id:016X}",
                      "--subject-fab-id", f"{ca.fabric_id:016X}", "--valid-from", CERT_VALID_FROM,
                      "--lifetime", str(CERT_LIFETIME_DAYS), "--ca-key", str(ca.key_path),
                      "--ca-cert", str(issuer), "--out-key", str(key_path), "--out", str(noc_path),
                      "--out-format", "chip")
        # gen-cert does not check that --ca-key belongs to --ca-cert; a mismatch yields a bad signature.
        chain = ["-c", str(ca.icac_path)] if ca.icac_path else []
        try:
            run_chip_cert(chip_cert, "validate-cert", str(noc_path), *chain, "-t", str(ca.rcac_path))
        except RuntimeError as error:
            raise ValueError(
                f"issued NOC does not validate against the CA chain; check that --ca-key is the private key of {issuer}\n{error}") from error
        return noc_path.read_bytes(), key_path.read_bytes()


# --- Thread network and devices ---

def make_thread_network(rng: random.Random, channel: int) -> list[dict]:
    """Returns ThreadActiveDataset entries (active_dataset_codec format) with fresh random network secrets."""
    pan_id = rng.randint(0x0001, 0xFFFE)
    return [
        {"type": "active_timestamp", "seconds": 1, "ticks": 0, "authoritative": False},
        {"type": "channel", "page": 0, "channel": channel},
        {"type": "wakeup_channel", "page": 0, "channel": THREAD_DIRECT_WAKE_CHANNEL},
        {"type": "channel_mask", "entries": [{"page": 0, "mask": THREAD_CHANNEL_MASK_PAGE0}]},
        {"type": "ext_pan_id", "value": random_bytes(rng, 8).hex()},
        {"type": "mesh_local_prefix", "value": (b"\xfd" + random_bytes(rng, 7)).hex()},
        {"type": "network_key", "value": random_bytes(rng, 16).hex()},
        {"type": "network_name", "value": f"{THREAD_NETWORK_NAME_PREFIX}{pan_id:04x}"},
        {"type": "pan_id", "value": f"0x{pan_id:04x}"},
        {"type": "pskc", "value": random_bytes(rng, 16).hex()},
        {"type": "security_policy", **THREAD_SECURITY_POLICY},
    ]


@dataclass
class Node:
    """Thread Direct node identity: operational credentials and Thread addresses."""

    role: str
    node_id: int
    noc: bytes
    op_keypair: bytes
    ext_address: bytes
    ml_eid_iid: bytes

    def check(self, ca: CA) -> None:
        """Verifies that the node ID is valid and that the NOC matches the key pair, node ID and fabric."""
        noc = decode_cert(self.noc)
        verify_or_raise(1 <= self.node_id <= MAX_OPERATIONAL_NODE_ID,
                        ValueError(f"{self.role}: invalid operational node ID 0x{self.node_id:x}"))
        verify_or_raise(len(self.op_keypair) == OP_KEYPAIR_SIZE,
                        ValueError(f"{self.role}: operational key blob must be {OP_KEYPAIR_SIZE} bytes"))
        verify_or_raise(noc["public_key"] == self.op_keypair[:65], ValueError(f"{self.role}: NOC public key does not match 0x708"))
        verify_or_raise(noc["node_id"] == self.node_id, ValueError(f"{self.role}: NOC node ID does not match"))
        verify_or_raise(noc["fabric_id"] == ca.fabric_id, ValueError(f"{self.role}: NOC fabric ID does not match"))


def new_node(role: str, ca: CA, chip_cert: Path, rng: random.Random, node_id: int | None = None) -> Node:
    """Draws the node ID (unless given), extended address and ML-EID IID, then issues its NOC."""
    node_id = rng.randint(1, MAX_OPERATIONAL_NODE_ID) if node_id is None else node_id
    verify_or_raise(1 <= node_id <= MAX_OPERATIONAL_NODE_ID, ValueError(f"{role}: invalid operational node ID 0x{node_id:x}"))
    ext_address, ml_eid_iid = random_ext_address(rng), random_bytes(rng, 8)
    noc, op_keypair = issue_noc(ca, chip_cert, node_id)
    return Node(role, node_id, noc, op_keypair, ext_address, ml_eid_iid)


@dataclass(frozen=True)
class Binding:
    """Unicast binding from the initiator to the listener."""

    local_endpoint: int = 1
    remote_endpoint: int = 1
    cluster_id: int | None = None


@dataclass(frozen=True)
class TargetConnection:
    """Transport and MRP parameters the device uses to reach its peer."""

    port: int = DEFAULT_PORT
    mrp_idle_ms: int = DEFAULT_MRP_IDLE_MS
    mrp_active_ms: int = DEFAULT_MRP_ACTIVE_MS
    mrp_active_threshold_ms: int = DEFAULT_MRP_ACTIVE_THRESHOLD_MS


# --- Document ---

def fabric_kvs(local: Node, remote: Node, ca: CA, admin_node_id: int, binding: Binding | None,
               grant_operate: bool) -> list[tuple[str, str]]:
    """MatterFabric KVS records (fabric, ACL, group keys and optional binding table)."""
    fabric = f"f/{FABRIC_INDEX:x}"
    ipk = hkdf_sha256(ca.ipk_epoch_key, ca.compressed_fabric_id, b"GroupKey v1.0", 16)
    ipk_hash = int.from_bytes(hkdf_sha256(ipk, b"", b"GroupKeyHash", 2), "big")
    unused_key = {4: uint(0), 5: uint(0), 6: bytes(16)}
    acl = [(ACL_PRIVILEGE_ADMINISTER, admin_node_id)] + ([(ACL_PRIVILEGE_OPERATE, remote.node_id)] if grant_operate else [])

    kvs = [(f"{fabric}/r", ca.rcac.hex())]
    if ca.icac is not None:
        kvs.append((f"{fabric}/i", ca.icac.hex()))
    kvs += [
        (f"{fabric}/n", local.noc.hex()),
        (f"{fabric}/m", tlv_hex({0: uint(ca.vendor_id), 1: ""})),  # Vendor ID, label.
        ("g/fidx", tlv_hex({0: uint(FABRIC_INDEX + 1), 1: [uint(FABRIC_INDEX)]})),  # Next fabric index, fabric indices.
    ]
    kvs += [(f"{fabric}/ac/0/{index:x}",
             tlv_hex({1: uint(privilege), 2: uint(ACL_AUTH_MODE_CASE), 3: [uint(subject)], 4: None}))
            for index, (privilege, subject) in enumerate(acl)]
    kvs += [
        ("g/gfl", tlv_hex({1: uint(FABRIC_INDEX), 2: uint(1)})),  # First fabric index, fabric count.
        # GroupDataProvider FabricData: no groups, one keyset.
        (f"{fabric}/g", tlv_hex({1: uint(0), 2: uint(0), 3: uint(0), 4: uint(0), 5: uint(0), 6: uint(1), 7: uint(0)})),
        (f"{fabric}/k/0", tlv_hex({1: uint(0), 2: uint(1),
                                   3: [{4: uint(0), 5: uint(ipk_hash), 6: ipk}] + [unused_key] * (KEYSET_MAX_KEYS - 1),
                                   7: uint(KEYSET_NO_NEXT)})),
    ]
    if binding is not None:
        entry = {1: uint(FABRIC_INDEX), 2: uint(binding.local_endpoint), 4: uint(binding.remote_endpoint),
                 5: uint(remote.node_id), 7: uint(BINDING_NO_NEXT_ENTRY)}
        if binding.cluster_id is not None:
            entry[3] = uint(binding.cluster_id)
        kvs += [("g/bt", tlv_hex({1: uint(BINDING_STORAGE_VERSION), 2: uint(0)})), ("g/bt/0", tlv_hex(entry))]
    return kvs


def build_document(
    local: Node,
    remote: Node,
    ca: CA,
    thread_dataset: list[dict],
    admin_node_id: int = DEFAULT_ADMIN_NODE_ID,
    connection: TargetConnection = TargetConnection(),
    binding: Binding | None = None,
    grant_operate: bool = False,
) -> dict:
    """Returns the local node's YAML document.

    `binding` points the initiator at the listener; `grant_operate` adds an
    Operate ACL entry for the initiator on the listener.
    """
    for each in (local, remote):
        each.check(ca)
    verify_or_raise({local.role, remote.role} == {"initiator", "listener"}, ValueError("expected an initiator and a listener"))
    verify_or_raise(local.node_id != remote.node_id, ValueError("both nodes have the same node ID"))
    verify_or_raise(local.ext_address != remote.ext_address, ValueError("both nodes have the same extended address"))
    verify_or_raise(1 <= admin_node_id <= MAX_OPERATIONAL_NODE_ID, ValueError(f"invalid admin node ID 0x{admin_node_id:x}"))
    verify_or_raise(admin_node_id not in (local.node_id, remote.node_id), ValueError("admin node ID collides with a node"))

    cfid = ca.compressed_fabric_id
    initiator, listener = (local, remote) if local.role == "initiator" else (remote, local)
    thread_info = {
        "role": 2,  # Child.
        "device_mode": 4,
        "rloc16": "0xfffe",  # Invalid: no parent is restored.
        "key_sequence": 0,
        "mle_frame_counter": 0,
        "mac_frame_counter": 0,
        "previous_partition_id": "0x00000000",
        "ext_address": local.ext_address.hex(),
        "ml_eid_iid": local.ml_eid_iid.hex(),
        "version": 5,
    }
    # Fail early on values the codecs cannot encode.
    network_info_codec.network_info_to_hex(thread_info)
    active_dataset_codec.dataset_to_hex(thread_dataset)

    target = [
        ("0x700", "Target extended address", remote.ext_address.hex()),
        ("0x701", f"Target address: {remote.role}", link_local_from_ext_address(remote.ext_address)),
        ("0x702", "Target port", le_hex(connection.port, 2)),
        ("0x703", "MRP Remote Idle Interval", le_hex(connection.mrp_idle_ms, 4)),
        ("0x704", "MRP Remote Active Interval", le_hex(connection.mrp_active_ms, 4)),
        ("0x705", "MRP Remote Active Threshold Time", le_hex(connection.mrp_active_threshold_ms, 4)),
        ("0x706", "Compressed Fabric ID", cfid[::-1].hex()),
        ("0x707", "Node ID", le_hex(remote.node_id, 8)),
    ]
    kvs = fabric_kvs(local, remote, ca, admin_node_id, binding, grant_operate)
    precommissioning_entries = [
        {"key": "0x708", "description": "Operational key", "value": local.op_keypair.hex()},
        {"key": "0x70A", "description": "Precommissioned flag", "value": PRECOMMISSIONED_MARKER},
    ]
    return {
        "metadata": {
            "role": local.role,
            "initiator_node_id": f"0x{initiator.node_id:016x}",
            "listener_node_id": f"0x{listener.node_id:016x}",
            "fabric_id": f"0x{ca.fabric_id:016x}",
            "compressed_fabric_id": cfid.hex(),
            "ext_address": local.ext_address.hex(),
            "link_local": link_local_from_ext_address(local.ext_address),
            "generated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        },
        "configs": {
            "MatterFabric": {
                "fabric_index": FABRIC_INDEX,
                "kvs_entries": [{"key": key, "value": value} for key, value in kvs],
            },
            "ThreadNetworkInfo": {"entries": thread_info},
            "ThreadActiveDataset": {"entries": thread_dataset},
            "PreCommissioning": {"entries": precommissioning_entries},
            "TargetConnectionParameters": {
                "entries": [{"key": key, "description": description, "value": value} for key, description, value in target],
            },
        },
    }


# --- CLI ---

def write_yaml(document: dict, path: Path) -> None:
    """Writes `document` owner-only, never overwriting; yaml.safe_dump keeps hex values as quoted strings."""
    header = "# Generated by generate_precommission.py. Contains private keys; flash once, do not commit.\n"
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as file:
        file.write(header + yaml.safe_dump(document, sort_keys=False, default_flow_style=False))


def generate(args: argparse.Namespace) -> None:
    """Builds an initiator/listener pair: the initiator binds to the listener, which grants it Operate."""
    outputs = {name: args.out / f"provisioning-config-{name}.yaml" for name in ("initiator", "listener")}
    existing = [str(path) for path in outputs.values() if path.exists()]
    verify_or_raise(not existing, FileExistsError(f"refusing to overwrite {', '.join(existing)}"))

    ca = load_ca(args.chip_cert, args.rcac, args.icac, args.ca_key, args.fabric_id, args.vendor_id, args.ipk_epoch_key)
    rng = random.Random(args.seed) if args.seed is not None else random.SystemRandom()
    dataset = make_thread_network(rng, args.channel)
    initiator = new_node("initiator", ca, args.chip_cert, rng, args.initiator_node_id)
    listener = new_node("listener", ca, args.chip_cert, rng, args.listener_node_id)
    common = {"admin_node_id": args.admin_node_id,
              "connection": TargetConnection(args.port, args.mrp_idle, args.mrp_active, args.mrp_active_threshold)}
    documents = {
        "initiator": build_document(initiator, listener, ca, dataset, **common,
                                    binding=Binding(args.local_endpoint, args.remote_endpoint, args.cluster_id)),
        "listener": build_document(listener, initiator, ca, dataset, **common, grant_operate=True),
    }

    args.out.mkdir(parents=True, exist_ok=True)
    for name, document in documents.items():
        write_yaml(document, outputs[name])
        print(f"Wrote {outputs[name]}")


def auto_int(value: str) -> int:
    return int(value, 0)


def thread_channel(value: str) -> int:
    channel = int(value, 0)
    if not 11 <= channel <= 26:
        raise argparse.ArgumentTypeError("Thread channel must be 11-26")
    return channel


def existing_file(value: str) -> Path:
    path = Path(value).expanduser()
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"{value} is not a file")
    return path.resolve()


def inputs_to_argv(path: Path, overrides: set[str]) -> list[str]:
    """Converts a flat JSON object of {option: value} into CLI arguments.

    Keys are the CLI option names (`chip_cert` or `chip-cert`); null and overridden values are skipped.
    Path options are resolved relative to the JSON file.
    """
    with path.open(encoding="utf-8") as file:
        values = json.load(file)
    verify_or_raise(isinstance(values, dict), ValueError(f"{path}: expected a JSON object"))
    argv = []
    for key, value in values.items():
        if value is None:
            continue
        name = key.replace("-", "_")
        if name in overrides:
            continue
        if name in {"chip_cert", "rcac", "icac", "ca_key", "out"}:
            value = (path.parent / Path(str(value)).expanduser()).resolve()
        argv.append(f"--{name.replace('_', '-')}={value}")
    return argv


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    """Parses CLI options, excluding overridden profile values before validation."""
    argv = sys.argv[1:] if argv is None else argv
    pre_parser = argparse.ArgumentParser(add_help=False)
    pre_parser.add_argument("--inputs", type=existing_file)
    inputs = pre_parser.parse_known_args(argv)[0].inputs

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--inputs", type=existing_file, help="JSON file of option values (CLI options override them)")
    parser.add_argument("--chip-cert", type=existing_file, required=True, help="path to the chip-cert binary")
    parser.add_argument("--rcac", type=existing_file, required=True, help="root certificate (PEM, DER or Matter TLV)")
    parser.add_argument("--icac", type=existing_file, help="intermediate certificate; omit to issue NOCs from the root")
    parser.add_argument("--ca-key", type=existing_file, required=True,
                        help="private key of the issuing certificate (the ICAC if given, else the RCAC)")
    parser.add_argument("--fabric-id", type=auto_int, help="default: fabric ID in the CA certificates, else 1")
    parser.add_argument("--vendor-id", type=auto_int, default=DEFAULT_VENDOR_ID)
    parser.add_argument("--ipk-epoch-key", type=bytes.fromhex, default=DEFAULT_IPK_EPOCH_KEY,
                        help="16-byte hex IPK epoch key (default: development sample value 'temporary ipk 01')")
    parser.add_argument("--out", type=Path, required=True, help="output directory for the two YAML files")
    parser.add_argument("--admin-node-id", type=auto_int, default=DEFAULT_ADMIN_NODE_ID)
    parser.add_argument("--initiator-node-id", type=auto_int, help="default: random")
    parser.add_argument("--listener-node-id", type=auto_int, help="default: random")
    parser.add_argument("--channel", type=thread_channel, default=DEFAULT_CHANNEL)
    parser.add_argument("--local-endpoint", type=auto_int, default=1, help="initiator binding endpoint")
    parser.add_argument("--remote-endpoint", type=auto_int, default=1, help="listener endpoint targeted by the binding")
    parser.add_argument("--cluster-id", type=auto_int, help="optional cluster restriction on the binding")
    parser.add_argument("--port", type=auto_int, default=DEFAULT_PORT)
    parser.add_argument("--mrp-idle", type=auto_int, default=DEFAULT_MRP_IDLE_MS)
    parser.add_argument("--mrp-active", type=auto_int, default=DEFAULT_MRP_ACTIVE_MS)
    parser.add_argument("--mrp-active-threshold", type=auto_int, default=DEFAULT_MRP_ACTIVE_THRESHOLD_MS)
    parser.add_argument("--seed", type=int, help="deterministic RNG seed (testing only)")
    profile_args = []
    if inputs:
        cli_parser = argparse.ArgumentParser(add_help=False, argument_default=argparse.SUPPRESS)
        for action in parser._actions:
            if action.dest != "help":
                cli_parser.add_argument(*action.option_strings, dest=action.dest)
        cli_values, _ = cli_parser.parse_known_args(argv)
        profile_args = inputs_to_argv(inputs, set(vars(cli_values)))
    return parser.parse_args(profile_args + argv)


def main(argv: list[str] | None = None) -> None:
    try:
        generate(parse_args(argv))
    except (ValueError, RuntimeError, OSError) as error:
        sys.exit(f"error: {error}")


if __name__ == "__main__":
    main()
