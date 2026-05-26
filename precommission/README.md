# Pre-Commissioning Script

## Quick Start

To quickly get the demo up and running, you can run the following commands to
put 2 devices into a pre-commissioned state that can communicate over thread
direct.

_note:_ The following command assume an MG26 closure app and an MG24 switch app,
but they are interchangeable with the provided images. A double mg26 or double
mg24 setup can also be achieved.

```bash
# Clear both devices prior to flashing (optional).
commander device masserase --serialno <target-device>
commander device masserase --serialno <target-device>

# Flash bootloader for both devices.
commander flash bootloader-mg24.s37 --serialno <target-device>
commander flash bootloader-mg26.s37 --serialno <target-device>

# Flash the apps of each device.
commander flash matter-silabs-closure-example-mg26.s37 --serialno <target-device>
commander flash matter-silabs-switch-example-mg24.s37 --serialno <target-device>

# Run the precommissioning script to have each device reachable on the same fabric through thread direct.
python precommission/precommission.py --filePath precommission/provisioning-config-closure.yaml --serialno <target-device>
python precommission/precommission.py --filePath precommission/provisioning-config-switch.yaml --serialno <target-device>
```

## Description

`precommission.py` reads a provisioning YAML and writes the corresponding
objects into the device NVM3 via Silicon Labs `commander nvm3 writedevice`, so
the device can boot already commissioned from factory.

It walks each section under `configs` and builds a single write batch:

- `kvs_entries` (typically `MatterFabric`): SHA256-hashes each KVS key, packs
  `key + hex value` into successive slots starting at `0x087501`, and writes the
  keymap at `0x087500`
- `ThreadActiveDataset` / `ThreadNetworkInfo`: encodes structured YAML entries
  through `active_dataset_codec` / `network_info_codec` into blobs at `0x20100`
  and `0x20300`
- other `entries` (e.g. `PreCommissioning`, `TargetConnectionParameters`):
  written as raw NVM3 objects; non-hex string values are ASCII-hex encoded

### Usage

```bash
python3 precommission/precommission.py --filePath precommission/provisioning-config-switch.yaml [--serialno <target-device>] [--dryrun]
```

| Flag         | Required | Description                                              |
| ------------ | -------- | -------------------------------------------------------- |
| `--filePath` | yes      | Path to the provisioning YAML                            |
| `--serialno` | no       | Target debugger/device serial (passed to `commander`)    |
| `--dryrun`   | no       | Print the `--object key:value` list without flashing NVM |

`commander` must be on `PATH`. Without `--serialno`, `commander` uses its
default attached device. Sample YAMLs (`provisioning-config-closure.yaml` /
`provisioning-config-switch.yaml`) show a paired closure + generic switch setup.

## Yaml layout

The documents `provisioning-config-closure.yaml` and
`provisioning-config-switch.yaml` were initially filled from device captures.

The format of each section differs based on the usage in the matter code.

Values land in NVM3 via:

| Section                      | NVM3 style                   | What it is                                |
| ---------------------------- | ---------------------------- | ----------------------------------------- |
| `MatterFabric`               | KVS string keys (`f/1/n`, …) | This device's fabric/certs/groups/ACL     |
| `ThreadNetworkInfo`          | raw blob @ `0x20300`         | Thread role, counters, ML-EID IID         |
| `ThreadActiveDataset`        | raw blob @ `0x20100`         | Full active operational dataset TLVs      |
| `PreCommissioning`           | raw blob @ `0x708`           | Operational keypair (local device)        |
| `TargetConnectionParameters` | mixed @ `0x701`–`0x707`      | Peer to connect to (address, MRP, PeerId) |

### MatterFabric

The `MatterFabric` section contains the fabric information for the device, it
can be read directly form the nvm3 via a conversion of the KVS keys to hash, and
using the 0x087500 to get a keymap of where each hash point in the NVM3.

The only missing piece is the Operational Key, which is stored in the
PreCommissioning section, and needs to be copied to the MatterFabric section.

### ThreadNetworkInfo

The `ThreadNetworkInfo` section contains the Thread network information for the
device, it can be also be read from NVM3, directly from key 0x20300, which will
yield a hex blod of data.

Each value can be populated individually in the yaml format and the python
script will convert those values into the blod to write in the nvm3.

### ThreadActiveDataset

The `ThreadActiveDataset` section contains the Thread active dataset information
for the device, it can be also be read from NVM3, directly from key 0x20100,
using.

```bash
ot-ctl dataset active
```

or

```bash
ot-ctl dataset active -x
```

To get the value in a hex blob.

Each value can be populated individually in the yaml format and the python
script will convert those values into the blod to write in the nvm3.

### PreCommissioning

This cannot be read from the device as it is an Opake Key, and therefore its
storage location is unknown. The workaround is to pre-generate it and pre-sign
the device NOC and Root certificates using its public key, and then copy it to
the yaml file.

It will be written in the NVM3 temporarily at 0x708, then moved to psa storage
during the bootup, and the nvm3 entry will be erased form that point for
security.

### TargetConnectionParameters

The `TargetConnectionParameters` section contains the parameters to connect to
the target device, the only necessary ones are the IPv6 address, the compressed
fabric id and the target node id. The rest of the parameters are set to default
and do not need to be changed, however they can be changed if needed.

To write those parameters to the NVM3, they first need to be converted to a
binary format, and then written to the NVM3 at 0x701 to 0x708.
