"""Convert between a Thread MeshCoP Operational Dataset TLV blob
(OT_SETTINGS_KEY_ACTIVE_DATASET / OT_SETTINGS_KEY_PENDING_DATASET, keys 0x20100 /
0x20200) and a YAML list of decoded TLV entries, one per Thread MeshCoP TLV.

TLV type values match OT_MESHCOP_TLV_* in openthread/include/openthread/dataset.h.
TLV types not covered by _CODECS round-trip as a raw {type: raw, tlv_type, value}
entry instead of failing, so unmapped TLVs never block a conversion.
"""

import argparse
import struct
import sys

import yaml

_TLV_CHANNEL = 0
_TLV_PAN_ID = 1
_TLV_EXTENDED_PAN_ID = 2
_TLV_NETWORK_NAME = 3
_TLV_PSKC = 4
_TLV_NETWORK_KEY = 5
_TLV_NETWORK_KEY_SEQUENCE = 6
_TLV_MESH_LOCAL_PREFIX = 7
_TLV_SECURITY_POLICY = 12
_TLV_ACTIVE_TIMESTAMP = 14
_TLV_PENDING_TIMESTAMP = 51
_TLV_DELAY_TIMER = 52
_TLV_CHANNEL_MASK = 53
_TLV_WAKEUP_CHANNEL = 74


def _decode_channel(value: bytes) -> dict:
    page, channel = struct.unpack(">BH", value)
    return {"page": page, "channel": channel}


def _encode_channel(fields: dict) -> bytes:
    return struct.pack(">BH", fields["page"], fields["channel"])


def _decode_timestamp(value: bytes) -> dict:
    # 48-bit seconds, 15-bit ticks, 1-bit authoritative flag, packed big-endian.
    raw = int.from_bytes(value, "big")
    return {"seconds": raw >> 16, "ticks": (raw >> 1) & 0x7FFF, "authoritative": bool(raw & 1)}


def _encode_timestamp(fields: dict) -> bytes:
    raw = (fields["seconds"] << 16) | ((fields["ticks"] & 0x7FFF) << 1) | (1 if fields["authoritative"] else 0)
    return raw.to_bytes(8, "big")


def _decode_channel_mask(value: bytes) -> dict:
    entries = []
    i = 0
    while i < len(value):
        page, length = value[i], value[i + 1]
        entries.append({"page": page, "mask": value[i + 2: i + 2 + length].hex()})
        i += 2 + length
    return {"entries": entries}


def _encode_channel_mask(fields: dict) -> bytes:
    out = bytearray()
    for entry in fields["entries"]:
        mask = bytes.fromhex(entry["mask"])
        out += bytes([entry["page"], len(mask)]) + mask
    return bytes(out)


def _decode_security_policy(value: bytes) -> dict:
    return {"rotation_time": int.from_bytes(value[:2], "big"), "flags": value[2:].hex()}


def _encode_security_policy(fields: dict) -> bytes:
    return fields["rotation_time"].to_bytes(2, "big") + bytes.fromhex(fields["flags"])


def _decode_hex(value: bytes) -> dict:
    return {"value": value.hex()}


def _encode_hex(fields: dict) -> bytes:
    return bytes.fromhex(fields["value"])


def _decode_text(value: bytes) -> dict:
    return {"value": value.decode("utf-8")}


def _encode_text(fields: dict) -> bytes:
    return fields["value"].encode("utf-8")


def _decode_hex_uint(value: bytes) -> dict:
    return {"value": f"0x{int.from_bytes(value, 'big'):0{len(value) * 2}x}"}


def _make_hex_uint_encoder(size: int):
    return lambda fields: int(fields["value"], 16).to_bytes(size, "big")


_CODECS = {
    _TLV_CHANNEL: ("channel", _decode_channel, _encode_channel),
    _TLV_WAKEUP_CHANNEL: ("wakeup_channel", _decode_channel, _encode_channel),
    _TLV_PAN_ID: ("pan_id", _decode_hex_uint, _make_hex_uint_encoder(2)),
    _TLV_EXTENDED_PAN_ID: ("ext_pan_id", _decode_hex, _encode_hex),
    _TLV_NETWORK_NAME: ("network_name", _decode_text, _encode_text),
    _TLV_PSKC: ("pskc", _decode_hex, _encode_hex),
    _TLV_NETWORK_KEY: ("network_key", _decode_hex, _encode_hex),
    _TLV_NETWORK_KEY_SEQUENCE: ("network_key_sequence", _decode_hex_uint, _make_hex_uint_encoder(4)),
    _TLV_MESH_LOCAL_PREFIX: ("mesh_local_prefix", _decode_hex, _encode_hex),
    _TLV_SECURITY_POLICY: ("security_policy", _decode_security_policy, _encode_security_policy),
    _TLV_ACTIVE_TIMESTAMP: ("active_timestamp", _decode_timestamp, _encode_timestamp),
    _TLV_PENDING_TIMESTAMP: ("pending_timestamp", _decode_timestamp, _encode_timestamp),
    _TLV_DELAY_TIMER: ("delay_timer", _decode_hex_uint, _make_hex_uint_encoder(4)),
    _TLV_CHANNEL_MASK: ("channel_mask", _decode_channel_mask, _encode_channel_mask),
}

_NAME_TO_TYPE = {name: tlv_type for tlv_type, (name, _, _) in _CODECS.items()}


def dataset_to_hex(entries: list) -> str:
    """Packs a list of decoded TLV entries into a Dataset TLV hex blob."""
    out = bytearray()
    for entry in entries:
        entry = dict(entry)
        type_name = entry.pop("type")
        if type_name == "raw":
            tlv_type = entry["tlv_type"]
            value = bytes.fromhex(entry["value"])
        else:
            tlv_type = _NAME_TO_TYPE[type_name]
            _, _, encode = _CODECS[tlv_type]
            value = encode(entry)
        out += bytes([tlv_type, len(value)]) + value
    return bytes(out).hex()


def hex_to_dataset(hex_str: str) -> list:
    """Unpacks a Dataset TLV hex blob into a list of decoded TLV entries."""
    raw = bytes.fromhex(hex_str)
    entries = []
    i = 0
    while i < len(raw):
        tlv_type, length = raw[i], raw[i + 1]
        value = raw[i + 2: i + 2 + length]
        if tlv_type in _CODECS:
            name, decode, _ = _CODECS[tlv_type]
            entry = {"type": name, **decode(value)}
        else:
            entry = {"type": "raw", "tlv_type": tlv_type, "value": value.hex()}
        entries.append(entry)
        i += 2 + length
    return entries


def _read_input(arg: str) -> str:
    try:
        with open(arg, "r") as file:
            return file.read().strip()
    except OSError:
        return arg.strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="Path to a file, or literal hex/YAML content")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--to-hex", action="store_true", help="Read YAML TLV entries, print the Dataset hex blob")
    group.add_argument("--to-yaml", action="store_true", help="Read a Dataset hex blob, print YAML TLV entries")
    parser.add_argument("-o", "--output", help="Write result to this file instead of stdout")
    args = parser.parse_args()

    content = _read_input(args.input)
    if args.to_hex:
        result = dataset_to_hex(yaml.safe_load(content))
    else:
        result = yaml.safe_dump(hex_to_dataset(content), sort_keys=False)

    if args.output:
        with open(args.output, "w") as file:
            file.write(result)
    else:
        sys.stdout.write(result if result.endswith("\n") else result + "\n")


if __name__ == "__main__":
    main()
