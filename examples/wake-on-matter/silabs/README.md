# Matter EFR32 Wake on Matter Example

An example showing the use of CHIP on the Silicon Labs EFR32 MG24.

<hr>

-   [Matter EFR32 Wake on Matter Example](#matter-efr32-wake-on-matter-example)
    -   [Introduction](#introduction)
    -   [Building](#building)
    -   [Flashing the Application](#flashing-the-application)
    -   [Viewing Logging Output](#viewing-logging-output)
        -   [SEGGER RTT (recommended)](#segger-rtt-recommended)
        -   [Console Log](#console-log)
            -   [Configuring the VCOM](#configuring-the-vcom)
        -   [Using the console](#using-the-console)
    -   [Running the Complete Example](#running-the-complete-example)
        -   [Notes](#notes)
    -   [Running RPC console](#running-rpc-console)
    -   [Device Tracing](#device-tracing)
    -   [Memory settings](#memory-settings)
    -   [OTA Software Update](#ota-software-update)
    -   [Group Communication (Multicast)](#group-communication-multicast)
    -   [Building options](#building-options)
        -   [Disabling logging](#disabling-logging)
        -   [Debug build / release build](#debug-build--release-build)
        -   [Disabling LCD](#disabling-lcd)
        -   [KVS maximum entry count](#kvs-maximum-entry-count)

<hr>

> **NOTE:** Silicon Laboratories now maintains a public matter GitHub repo with
> frequent releases thoroughly tested and validated. Developers looking to
> develop matter products with silabs hardware are encouraged to use our latest
> release with added tools and documentation.
> [Silabs matter_sdk Github](https://github.com/SiliconLabsSoftware/matter_sdk/tags)

## Introduction

The EFR32 wake on Matter example provides a baseline demonstration of an OT-NCP
combined with a Matter node that receives subscription to wake up the host.

## Building

-   Download the
    [Simplicity Commander](https://www.silabs.com/mcu/programming-options)
    command line tool, and ensure that `commander` is your shell search path.
    (For Mac OS X, `commander` is located inside
    `Commander.app/Contents/MacOS/`.)

-   Download and install a suitable ARM gcc tool chain (For most Host, the
    bootstrap already installs the toolchain):
    [GNU Arm Embedded Toolchain 12.2 Rel1](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)

-   Install some additional tools (likely already present for CHIP developers):

    -   Linux: `sudo apt-get install git ninja-build`

    -   Mac OS X: `brew install ninja`

-   Supported hardware:

    -   > For the latest supported hardware please refer to the
        > [Hardware Requirements](https://docs.silabs.com/matter/latest/matter-prerequisites/hardware-requirements)
        > in the Silicon Labs Matter Documentation

    MG24 boards :

    -   BRD2703A / SLWSTK6000B / Wireless Starter Kit / 2.4GHz@10dBm
    -   BRD4186C / SLWSTK6006A / Wireless Starter Kit / 2.4GHz@10dBm
    -   BRD4187C / SLWSTK6006A / Wireless Starter Kit / 2.4GHz@20dBm
    -   BRD2703A / MG24 Explorer Kit
    -   BRD2704A / SparkFun Thing Plus MGM240P board

*   Build the example application:

            cd ~/connectedhomeip
            ./scripts/examples/gn_silabs_example.sh ./examples/wake-on-matter/silabs/ ./out/wake-on-matter BRD4187C

-   To delete generated executable, libraries and object files use:

            $ cd ~/connectedhomeip
            $ rm -rf ./out/

    OR use GN/Ninja directly

            $ cd ~/connectedhomeip/examples/wake-on-matter/silabs
            $ git submodule update --init
            $ source third_party/connectedhomeip/scripts/activate.sh
            $ export SILABS_BOARD=BRD4187C
            $ gn gen out/debug
            $ ninja -C out/debug

-   To delete generated executable, libraries and object files use:

            $ cd ~/connectedhomeip/examples/wake-on-matter/silabs
            $ rm -rf out/

For more build options, help is provided when running the build script without
arguments

         ./scripts/examples/gn_silabs_example.sh

## Flashing the Application

-   On the command line:

            $ cd ~/connectedhomeip/examples/wake-on-matter/silabs
            $ python3 out/debug/matter-silabs-wake-on-matter-example.flash.py

-   Or with the Ozone debugger, just load the .out file.

All EFR32 boards require a bootloader, see Silicon Labs documentation for more
info. Pre-built bootloader binaries are available on the
[Matter Software Artifacts page](https://docs.silabs.com/matter/latest/matter-prerequisites/matter-artifacts#matter-bootloader-binaries).

### Console Log

If the binary was built with this option or if you're using the Siwx917 WiFi
SoC, the logs and the CLI (if enabled) will be available on the serial console.

This console required a baudrate of **115200** with CTS/RTS. This is the default
configuration of Silicon Labs dev kits.

**HOWEVER** the console will required a baudrate of **921600** with CTS/RTS if
the verbose mode is selected (--verbose)

#### Configuring the VCOM

-   Using (Simplicity
    Studio)[https://community.silabs.com/s/article/wstk-virtual-com-port-baudrate-setting?language=en_US]
-   Using commander-cli
    ```
    commander vcom config --baudrate 921600 --handshake rtscts
    ```

### Using the console

With any serial terminal application such as screen, putty, minicom etc.

### Notes

-   Depending on your network settings your router might not provide native ipv6
    addresses to your devices (Border router / PC). If this is the case, you
    need to add a static ipv6 addresses on both device and then an ipv6 route to
    the border router on your PC

    -   On Border Router: `sudo ip addr add dev <Network interface> 2002::2/64`

    -   On PC(Linux): `sudo ip addr add dev <Network interface> 2002::1/64`

    -   Add Ipv6 route on PC(Linux)
        `sudo ip route add <Thread global ipv6 prefix>/64 via 2002::2`

### Debug build / release build

`--release`

    $ ./scripts/examples/gn_silabs_example.sh ./examples/wake-on-matter/silabs ./out/wake-on-matter BRD4164A "is_debug=false"

# Matter Wake on Lan configuration

This samples apps allows subscriptions to another Matter device. This is usefull
to receive state changes and to act accordingly

Steps

1. Commission this applications to a matter thread network
2. Give ACL to the desired node with which you want to establish a subscription
   with the following command

```
    chip-tool accesscontrol write acl '[{"fabricIndex": 1, "privilege": 1, "authMode": 2, "subjects": [<woM node ID>], "targets": null}]' <target node id> 0

Important detail
write acl replaces the full ACL list, it does not append one entry. So if you already have other ACL entries, you must include them too in the same JSON array, for example:

```
chip-tool accesscontrol write acl '[{"fabricIndex": 1, "privilege": 5, "authMode": 2, "subjects": [112233], "targets": null}, {"fabricIndex": 1, "privilege": 1, "authMode": 2, "subjects": [<node ID>], "targets": null}]' 1 0

3. Using the silabs_console.py or a standard CLI interface like screen or PuTTY run the following shell commands
4. run this command in the WoM device shell so that a subscription can be establish between the node and the WoM device
```

    im subscribe <fabricIndex> <nodeId> <endpointId> <clusterId>
    matterCli> im subscribe 1 40 1 6 0

# Testing / End-to-end validation

The `scripts/` directory ships with two helpers that together validate an
entire wake-on-matter setup end-to-end from an Ubuntu-based OpenThread Border
Router (OTBR) host:

- `scripts/verify_install.sh` - checks that `cpcd.service`,
  `otbr-agent.service` and the `wpan0` interface are present and up. If
  `wpan0` is missing but an active OT dataset exists, it attempts to bring it
  up via `ot-ctl`.
- `scripts/tests/end_to_end_validation.sh` - runs the checks above, then
  commissions an accessory + the wake-on-matter NCP, writes the ACL, and
  establishes a subscription.

## Prerequisites

Host packages (Ubuntu):

```sh
sudo apt-get install net-tools     # provides ifconfig, required by verify_install.sh
```

Silabs services running on the host:

- `cpcd.service` (CPC daemon bridging the RCP)
- `otbr-agent.service` (OpenThread Border Router agent)
- A committed active OT dataset (`sudo ot-ctl dataset init new` then
  `sudo ot-ctl dataset commit active` if none is present)

Matter tools:

- `chip-tool` on `PATH` (built from the connectedhomeip tree).
- `mmic_host` on `PATH` (build instructions below).
- A wake-on-matter EFR32 device flashed and connected either via
  the running `cpcd` instance (endpoint 90 ).
- A second Matter accessory (e.g. the `sensor-app`) in commissioning mode
  (BLE advertising), reachable with the standard test setup pin `20202021`
  and discriminator `3840`.

## Building `mmic_host`

`mmic_host` is the host-side CLI used to drive the NCP (commission, query
state, establish subscriptions, etc.). Its sources live at
`third_party/matter_sdk/examples/platform/silabs/mmic/`.

UART transport (for testing purposes only):

```sh
cd third_party/matter_sdk/examples/platform/silabs/mmic
cmake -S . -B build
cmake --build build
```

CPC transport (recommended when `cpcd` is already bridging the device):

```sh
cd third_party/matter_sdk/examples/platform/silabs/mmic
cmake -S . -B build -DMMIC_USE_CPC=ON
cmake --build build
```

The resulting binary is `build/mmic_host`. Add it to `PATH` (or point the
test script at it with `MMIC_HOST=/full/path/to/mmic_host`).

For the full mmic build options (compiler selection, non-standard libcpc
locations, one-shot vs interactive mode), see
`third_party/matter_sdk/examples/platform/silabs/mmic/README.md`.

## End-to-end test steps

Once the prerequisites are in place, run:

```sh
./scripts/tests/end_to_end_validation.sh
```

The script performs, in order:

1. **`verify_install.sh`** - verifies `cpcd.service`, `otbr-agent.service`,
   and that `wpan0` is up (auto-brings it up via `ot-ctl` if a dataset is
   present but the interface is down).
2. **Commission the accessory over BLE-Thread** - reads the active dataset
   with `sudo ot-ctl dataset active -x` and runs:
   ```sh
   chip-tool pairing ble-thread <ACCESSORY_NODE_ID> hex:<dataset> \
       <SETUP_PINCODE> <DISCRIMINATOR>
   ```
3. **Commission the wake-on-matter NCP** - runs `mmic_host matter_state` as a
   pre-check; if a fabric is already present it issues `mmic_host decommission`
   and confirms the fabric count returns to 0. Then:
   ```sh
   mmic_host commission <NCP_NODE_ID>
   mmic_host matter_state          # must show the new nodeId
   ```
4. **Grant ACL** - allows the NCP to open a CASE session against the accessory:
   ```sh
   chip-tool accesscontrol write acl \
       '[{"fabricIndex": 1, "privilege": 1, "authMode": 2, "subjects": [<NCP_NODE_ID>], "targets": null}]' \
       <ACCESSORY_NODE_ID> 0
   ```
5. **Establish a subscription** from the NCP toward the accessory:
   ```sh
   mmic_host establish_subscription \
       <SUB_FABRIC_INDEX> <ACCESSORY_NODE_ID> \
       <SUB_ENDPOINT_ID> <SUB_CLUSTER_ID> <SUB_ATTRIBUTE_ID>
   ```

Every step prints `[PASS]` or `[FAIL]`. The first failing step aborts the
script (exit code `1`), and a final summary block is printed.
`Ctrl+C` interrupts and exits with `130`.

## Configuration

All values are overridable via environment variables (defaults shown):

| Variable             | Default                | Purpose                                    |
| -------------------- | ---------------------- | ------------------------------------------ |
| `CHIP_TOOL`          | `chip-tool`            | chip-tool binary                           |
| `MMIC_HOST`          | `mmic_host`            | mmic_host binary                           |
| `VERIFY_SCRIPT`      | `<script_dir>/verify_install.sh` | Path to `verify_install.sh`      |
| `ACCESSORY_NODE_ID`  | `1`                    | Node id assigned to the Matter accessory   |
| `NCP_NODE_ID`        | `100`                  | Node id assigned to the wake-on-matter NCP |
| `SETUP_PINCODE`      | `20202021`             | Accessory setup pin code                   |
| `DISCRIMINATOR`      | `3840`                 | Accessory setup discriminator              |
| `SUB_FABRIC_INDEX`   | `1`                    | Fabric index used for the subscription     |
| `SUB_ENDPOINT_ID`    | `0`                    | Subscription endpoint                      |
| `SUB_CLUSTER_ID`     | `0x0028`               | Basic Information cluster                  |
| `SUB_ATTRIBUTE_ID`   | `0x0000`               | DataModelRevision attribute                |
| `COMMISSION_TIMEOUT` | `180`                  | BLE-Thread commission timeout (s)          |
| `CHIP_TOOL_TIMEOUT`  | `60`                   | chip-tool ACL write timeout (s)            |
| `MMIC_TIMEOUT`       | `30`                   | mmic_host command timeout (s)              |

Example - commission the NCP with node id `200` and target a `lighting-app`
accessory with node id `42` on cluster `OnOff` (`0x0006`) / attribute `OnOff`
(`0x0000`) on endpoint `1`:

```sh
NCP_NODE_ID=200 \
ACCESSORY_NODE_ID=42 \
SUB_CLUSTER_ID=0x0006 \
SUB_ATTRIBUTE_ID=0x0000 \
SUB_ENDPOINT_ID=1 \
./scripts/tests/end_to_end_validation.sh
```

## Troubleshooting

- **`verify_install.sh` reports `ifconfig not installed`** - install
  `net-tools` (`sudo apt-get install net-tools`).
- **`wpan0` never appears** - no active dataset. Create one:
  `sudo ot-ctl dataset init new && sudo ot-ctl dataset commit active`.
- **`chip-tool pairing ble-thread` hangs** - the accessory is not advertising
  or BlueZ is not reachable. Confirm the device is in commissioning mode and
  that `sudo systemctl status bluetooth` is active.
- **`mmic_host matter_state` fabric nodeId mismatch** - a stale fabric is
  present. The script auto-runs `mmic_host decommission` in that case; if the
  check still fails, decommission manually and re-run.
- **Subscription fails at Sigma1 with a `destinationId` mismatch** - the
  accessory and NCP are on different fabrics, or the NCP holds a stale IPK.
  Factory-reset the NCP, decommission the accessory
  (`chip-tool pairing unpair <node-id>`) and re-run the script from step 2.
