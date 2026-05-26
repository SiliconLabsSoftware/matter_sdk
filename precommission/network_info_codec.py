"""Convert between OpenThread's NetworkInfo NVM3 settings blob
(OT_SETTINGS_KEY_NETWORK_INFO, key 0x20300) and a YAML mapping with one field per line.

Struct layout (see Settings::NetworkInfo in
third_party/silabs/simplicity_sdk/.../openthread/src/core/common/settings.hpp), all
multi-byte integers little-endian:
    role                    uint8
    device_mode             uint8
    rloc16                  uint16
    key_sequence            uint32
    mle_frame_counter       uint32
    mac_frame_counter       uint32
    previous_partition_id   uint32
    ext_address             uint8[8]
    ml_eid_iid              uint8[8]
    version                 uint16
"""

import argparse
import struct
import sys

import yaml

_STRUCT_FORMAT = "<BBHIIII8s8sH"
_BYTE_FIELDS = ("ext_address", "ml_eid_iid")
_HEX_INT_WIDTHS = {"rloc16": 4, "previous_partition_id": 8}
_FIELD_NAMES = (
    "role",
    "device_mode",
    "rloc16",
    "key_sequence",
    "mle_frame_counter",
    "mac_frame_counter",
    "previous_partition_id",
    "ext_address",
    "ml_eid_iid",
    "version",
)


def network_info_to_hex(fields: dict) -> str:
    """Packs a NetworkInfo field mapping into its NVM3 hex blob."""
    values = []
    for name in _FIELD_NAMES:
        value = fields[name]
        if name in _BYTE_FIELDS:
            value = bytes.fromhex(value)
        elif name in _HEX_INT_WIDTHS and isinstance(value, str):
            value = int(value, 16)
        values.append(value)
    return struct.pack(_STRUCT_FORMAT, *values).hex()


def hex_to_network_info(hex_str: str) -> dict:
    """Unpacks a NetworkInfo NVM3 hex blob into a field mapping."""
    raw = bytes.fromhex(hex_str)
    expected_size = struct.calcsize(_STRUCT_FORMAT)
    if len(raw) != expected_size:
        raise ValueError(f"expected {expected_size} bytes, got {len(raw)}")
    fields = dict(zip(_FIELD_NAMES, struct.unpack(_STRUCT_FORMAT, raw)))
    for name in _BYTE_FIELDS:
        fields[name] = fields[name].hex()
    for name, width in _HEX_INT_WIDTHS.items():
        fields[name] = f"0x{fields[name]:0{width}x}"
    return fields


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
    group.add_argument("--to-hex", action="store_true", help="Read YAML fields, print the NVM3 hex blob")
    group.add_argument("--to-yaml", action="store_true", help="Read an NVM3 hex blob, print YAML fields")
    parser.add_argument("-o", "--output", help="Write result to this file instead of stdout")
    args = parser.parse_args()

    content = _read_input(args.input)
    if args.to_hex:
        result = network_info_to_hex(yaml.safe_load(content))
    else:
        result = yaml.safe_dump(hex_to_network_info(content), sort_keys=False)

    if args.output:
        with open(args.output, "w") as file:
            file.write(result)
    else:
        sys.stdout.write(result if result.endswith("\n") else result + "\n")


if __name__ == "__main__":
    main()
