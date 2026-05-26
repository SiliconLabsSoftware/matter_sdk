import yaml
import argparse
import sys
import subprocess
import hashlib
import struct
import active_dataset_codec
import network_info_codec

KVS_NVM3_KEYMAP = 0x087500
KVS_NVM3_FIRSTSLOT = 0x087501
KVS_MAX_ENTRIES = 511


def commander_write_all(objects: list[tuple[str, str]], serialno: str = "", dryrun: bool = False):
    for key, value in objects:
        print(f"Writing {key} = {value}")
    if dryrun:
        for key, value in objects:
            print(f"  --object {key}:{value}")
        return
    cmd = ["commander", "nvm3", "writedevice"]
    for key, value in objects:
        cmd += ["--object", f"{key}:{value}"]
    if serialno:
        cmd += ["--serialno", serialno]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"commander exited with {proc.returncode}")


def kvs_to_hash(key: str) -> int:
    """Replicates KeyValueStoreManagerImpl::hashKvsKeyString: SHA256(key), then the
    first non-zero little-endian 16-bit pair scanning from byte 0."""
    digest = hashlib.sha256(key.encode("ascii")).digest()
    hash16 = 0
    i = 0
    while hash16 == 0 and i < (len(digest) - 1):
        hash16 = digest[i] | (digest[i + 1] << 8)
        i += 1
    return hash16


def main(data: dict, serialno: str = "", dryrun: bool = False):
    keymap = [0] * KVS_MAX_ENTRIES
    slot = 0
    has_kvs = False
    pending: list[tuple[str, str]] = []

    for section_name, section in data.get("configs", {}).items():
        for fields in section.get("kvs_entries", []):
            key = fields["key"]
            value = fields["value"]
            keymap[slot] = kvs_to_hash(key)
            data_blob = key.encode("ascii") + bytes.fromhex(value)
            pending.append((f"0x{KVS_NVM3_FIRSTSLOT + slot:05X}", data_blob.hex()))
            slot += 1
            has_kvs = True

        if section_name == "ThreadActiveDataset":
            active_dataset_blob = active_dataset_codec.dataset_to_hex(section.get("entries", []))
            pending.append((f"0x20100", active_dataset_blob))
            continue

        if section_name == "ThreadNetworkInfo":
            network_info_blob = network_info_codec.network_info_to_hex(section.get("entries", {}))
            pending.append((f"0x20300", network_info_blob))
            continue

        for fields in section.get("entries", []):
            key = fields["key"]
            value = fields["value"]
            try:
                bytes.fromhex(value)
            except ValueError:
                value = value.encode("ascii").hex()
            pending.append((key, value))

    if has_kvs:
        keymap_blob = b"".join(struct.pack("<H", h) for h in keymap)
        pending.append((f"0x{KVS_NVM3_KEYMAP:05X}", keymap_blob.hex()))

    if pending:
        commander_write_all(pending, serialno, dryrun)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--filePath", type=str, required=True, default="precommission.yaml")
    parser.add_argument("--serialno", type=str, required=False, default=None)
    parser.add_argument("--dryrun", action="store_true", required=False, default=False)
    args = parser.parse_args()
    with open(args.filePath, "r") as file:
        data = yaml.safe_load(file)

    if data is None:
        print("Error: No data found in the file")
        sys.exit(1)
    main(data, args.serialno, args.dryrun)
