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

## Data in the switch/closure prototype pair

The two YAML files are a coordinated prototype pair. **Shared** means the value
must match on both devices in this pair; **Common** means a Matter-wide
convention or default; **Unique** means device-local data, which may happen to
have equal values on both devices. MatterFabric values are Matter TLV blobs, so
their exact internal fields are not repeated here; the YAML key names identify
the records.

| Name                                   | Key (NVM3 or KVS)                   | Scope  |
| -------------------------------------- | ----------------------------------- | ------ |
| Fabric index                           | KVS `fabric_index`; `g/fidx`        | Shared |
| Root certificate                       | KVS `f/1/r`                         | Shared |
| Intermediate certificate               | KVS `f/1/i`                         | Shared |
| Node Operational Certificate (NOC)     | KVS `f/1/n`                         | Unique |
| Fabric metadata                        | KVS `f/1/m`                         | Shared |
| Access Control List (ACL) entries      | KVS `f/1/ac/<fabric-index>/<entry>` | Unique |
| Group table                            | KVS `f/1/g`                         | Shared |
| Group key set                          | KVS `f/1/k/0`                       | Shared |
| Fabric group flags                     | KVS `g/gfl`                         | Shared |
| Binding metadata                       | KVS `g/bt`, `g/bt/0`                | Unique |
| Operational private key                | NVM3 `0x708`                        | Unique |
| Additional precommissioning state      | NVM3 `0x70A`                        | Unique |
| Thread active dataset                  | NVM3 `0x20100`                      | Shared |
| Thread role/mode and codec version     | NVM3 `0x20300`                      | Unique |
| Thread locator and identity            | NVM3 `0x20300`                      | Unique |
| Thread replay and partition state      | NVM3 `0x20300`                      | Unique |
| Peer extended address and IPv6 address | NVM3 `0x700`, `0x701`               | Unique |
| Matter UDP port                        | NVM3 `0x702`                        | Common |
| MRP remote timing parameters           | NVM3 `0x703`–`0x705`                | Shared |
| Compressed fabric ID                   | NVM3 `0x706`                        | Shared |
| Peer node ID                           | NVM3 `0x707`                        | Unique |

### Data details

#### Fabric index

- **Purpose:** Selects the local fabric slot.
- **Range and constraints:** Nonzero fabric index supported by the device; this
  pair uses slot 1.
- **Generation guidance:** Allocate the same unused slot on both clean prototype
  devices. No reason to use something else than 1 here. Changing thi from one
  must be reflected in the KVS keys "f/<Fidx>/..."

#### Root certificate

- **Purpose:** Trust anchor for the fabric.
- **Range and constraints:** Matter-encoded root certificate.
- **Generation guidance:** Generate/choose one fabric root and use it for both
  devices.

#### Intermediate certificate

- **Purpose:** Intermediate CA in the fabric certificate chain.
- **Range and constraints:** Matter-encoded certificate.
- **Generation guidance:** Use the issuing intermediate from the same fabric
  chain on both devices.

#### Node Operational Certificate (NOC)

- **Purpose:** Binds one node ID and public key to the fabric.
- **Range and constraints:** Matter-encoded, signed certificate.
- **Generation guidance:** Issue a distinct NOC for each device’s operational
  public key and node ID.

#### Fabric metadata

- **Purpose:** Stores fabric-level metadata, including the compressed fabric ID.
- **Range and constraints:** Matter TLV structure.
- **Generation guidance:** Generate once for the fabric and copy to both
  devices.

#### Access Control List (ACL) entries

- **Purpose:** Defines authorized subjects, privileges, and endpoint scope.
- **Range and constraints:** Matter ACL structure and list-size limits apply.
  The YAMLs have different entries.
- **Generation guidance:** Configure each device according to its intended
  access policy.

#### Group table

- **Purpose:** Stores group membership information.
- **Range and constraints:** Matter TLV structure; group IDs are 16-bit.
- **Generation guidance:** Keep membership consistent for group communication
  between the pair.

#### Group key set

- **Purpose:** Stores group key material used for group messages.
- **Range and constraints:** Matter TLV structure with cryptographic key
  material.
- **Generation guidance:** Provision matching group keys on members of the same
  group; generate securely and keep secret.

#### Fabric group flags

- **Purpose:** Stores fabric group feature/state flags.
- **Range and constraints:** Matter TLV structure.
- **Generation guidance:** Copy the compatible fabric setting to both devices.

#### Binding metadata

- **Purpose:** Stores local binding-table state; present only in the closure
  YAML.
- **Range and constraints:** Matter TLV structure and implementation limits
  apply.
- **Generation guidance:** Configure from the local device’s binding needs; do
  not assume both devices need identical entries.

#### Operational private key

- **Purpose:** Private key corresponding to the device’s NOC public key.
- **Range and constraints:** 97 bytes: 65-byte uncompressed P-256 public key
  followed by the 32-byte private scalar. The imported key must match the NOC.
- **Generation guidance:** Generate a fresh operational key pair per device,
  issue its NOC for that public key, and store the matching private key. Never
  randomize the stored blob independently.

#### Additional precommissioning state

- **Purpose:** boolean value store in NVM3 to indicate to the matter app that it
  was precommission. It trigger a behaviour on the app after the first boot
  post-precommission
- **Range and constraints:** Boolean (nvm3 store as 4 bytes).
- **Generation guidance:** Just set to True during Precommission

#### Thread active dataset

- **Purpose:** Defines the active Thread network and its security parameters.
- **Range and constraints:** Extended PAN ID 8 bytes; mesh-local prefix 8 bytes;
  network key and PSKc 16 bytes each; PAN ID 16 bits (avoid reserved values);
  name 1–16 UTF-8 bytes; 2.4 GHz channels 11–26 on page 0; channel mask must
  include operating channel. Other fields have Thread-defined encodings.
- **Generation guidance:** Create one dataset per pair and install it on both.
  Generate secret keys securely; choose a supported channel and non-reserved PAN
  ID. Advance the active timestamp when changing the dataset.

#### Thread role/mode and codec version

- **Fields:** `role`, `device_mode`, `version`.
- **Purpose:** Describes local Thread state and the serialized network-info
  format.
- **Range and constraints:** Fixed field widths; version must be supported by
  network_info_codec.py.
- **Generation guidance:** Use values compatible with the firmware and codec.
  These are state/configuration values, not random data.

#### Thread locator and identity

- **Fields:** `rloc16`, `ext_address`, `ml_eid_iid`.
- **Purpose:** Identifies the device’s Thread locator, radio address, and
  Mesh-Local EID.
- **Range and constraints:** RLOC16 is 16 bits; extended address and ML-EID IID
  are 8 bytes each.
- **Generation guidance:** Keep the extended address unique and aligned with the
  actual radio. Derive/capture the locator and ML-EID from the Thread stack
  rather than choosing them independently.

#### Thread replay and partition state

- **Fields:** `key_sequence`, `mle_frame_counter`, `mac_frame_counter`,
  `previous_partition_id`.
- **Purpose:** Preserves replay protection and Thread network continuity.
- **Range and constraints:** Codec-defined integer widths; counters must not
  roll back for a reused network identity.
- **Generation guidance:** Initialize from the Thread stack. Do not randomize
  counters or partition state.

#### Peer extended address and IPv6 address

- **Keys:** `0x700` stores the peer extended address; `0x701` stores its IPv6
  address.
- **Purpose:** Tells each device how to reach its peer over Thread.
- **Range and constraints:** Extended address is 8 bytes; IPv6 address is 16
  bytes. The sample uses a link-local address derived from the peer’s extended
  address.
- **Generation guidance:** Set reciprocally: each device stores the other
  device’s actual address. Derive the link-local address using the correct IID
  procedure.

#### Matter UDP port

- **Purpose:** Selects the Matter-over-Thread UDP destination port.
- **Range and constraints:** Matter default is UDP port 5540 (16 bits).
- **Generation guidance:** Use the Matter port unless the peer application is
  intentionally configured for another port.

#### MRP remote timing parameters

- **Purpose:** Configures Message Reliability Protocol timing for peer
  exchanges.
- **Range and constraints:** 32-bit millisecond values; use supported intervals
  and a suitable active threshold.
- **Generation guidance:** Keep compatible on both devices; use the Matter
  defaults unless the application requires different timing.

#### Compressed fabric ID

- **Purpose:** Identifies the Matter fabric in peer connection parameters.
- **Range and constraints:** 8-byte fabric-derived identifier.
- **Generation guidance:** Derive from the same fabric identity used by the
  certificates and provision it on both.

#### Peer node ID

- **Purpose:** Identifies the target Matter node.
- **Range and constraints:** 64-bit Matter Node ID; use a valid, non-reserved
  node ID.
- **Generation guidance:** Allocate distinct node IDs within the fabric. Each
  device stores the other device’s NOC node ID. However this node ID selections
  affect other stored data, like the ACL, Bindings and NOC. Keep in sync !

### Generating a fresh prototype pair

- Generate one fabric identity and one Thread network dataset for each pair;
  provision the same fabric chain, fabric metadata, and Thread dataset to both
  devices.
- Generate a distinct Matter operational key pair and node ID for each device.
  Issue each NOC for that device’s public key and node ID, then store its
  matching private key in `PreCommissioning`.
- Assign a unique extended address per radio. Populate each device’s
  Thread-local locator, EID, counters, and partition state from the Thread
  stack.
- Set each device’s peer extended address, IPv6 address, and node ID to the
  other device’s values.
- Generate private keys, network keys, PSKc, and group keys securely and keep
  them secret. Do not reuse prototype secrets for production or unrelated pairs.
