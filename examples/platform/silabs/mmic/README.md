# mmic host build

Host-side build of the `mmic` serial shell. Supported on Linux and macOS.

## Prerequisites

- CMake >= 3.13
- A C11 compiler (clang or gcc)
- POSIX pthreads (already present on Linux/macOS)

## Build

From this directory:

```sh
cmake -S . -B build
cmake --build build
```

The resulting binary is `build/mmic_host`.

To force a specific compiler or build type:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
cmake --build build
```

### Transport selection (UART vs CPC)

By default `mmic_host` talks to the device over a UART.

To instead talk to the device through the Silicon Labs CPC (Co-Processor
Communication) daemon on **endpoint 90**, configure with:

```sh
cmake -S . -B build -DMMIC_USE_CPC=ON
cmake --build build
```

This requires the CPC daemon (`cpcd`) development files (`sl_cpc.h` and
`libcpc`) to be discoverable by CMake. If they live in a non-standard
location, point at them explicitly:

```sh
cmake -S . -B build -DMMIC_USE_CPC=ON \
    -DCPC_INCLUDE_DIR=/path/to/include \
    -DCPC_LIBRARY=/path/to/libcpc.so
```

## Run

### UART build (default)

Pass the serial device as the only argument.

Linux:

```sh
./build/mmic_host /dev/ttyUSB0
```

macOS:

```sh
./build/mmic_host /dev/cu.usbserial-XXXX
```

The port is opened at 115200 8N1. Bytes received from the port are printed to
stdout. Type `exit` (or send EOF) to close the port and quit.

### CPC build (`-DMMIC_USE_CPC=ON`)

The `cpcd` daemon must already be running and bridging to the device.
Optionally pass the cpcd instance name (defaults to `cpcd_0`):

```sh
./build/mmic_host              # uses "cpcd_0"
./build/mmic_host cpcd_myrcp   # uses the named instance
```

`mmic_host` opens CPC endpoint 90 and exchanges the same framed
command/response bytes as in UART mode.

## Device side

### Transports

The device-side MMIC core (`mmic.cpp`, `mmic_task.cpp`, `mmic_forward.cpp`) is
transport agnostic (`mmic_transport.h`). Exactly one transport is linked:

| SLC component      | Source                    | Notes                                                                                     |
| ------------------ | ------------------------- | ----------------------------------------------------------------------------------------- |
| `matter_mmic_cpc`  | `mmic_transport_cpc.cpp`  | Default, over the `matter_cpc` endpoint.                                                  |
| `matter_mmic_uart` | `mmic_transport_uart.cpp` | Raw UART through the `matter_uart` console driver. Excludes the Matter shell and UART logs. |

Use the `mmic_host` UART build with `matter_mmic_uart`, and the CPC build with
`matter_mmic_cpc`.

### Cluster commands and attribute changes

Either side can initiate a request (`0xF1`), answered with the same op code
(`0xF2`). The device uses this to forward Matter commands and attribute changes
to the host, and the host to have the device send commands on the Matter
network:

- `cluster_command` (both directions): `clusterCommandHeader_t` (transaction id,
  flags, endpoint, cluster, command) followed by the command parameters as
  Matter TLV.
  - Device to host: when `MMIC_CLUSTER_COMMAND_FLAG_ANSWER_EXPECTED` is set, the
    host answers with `clusterCommandAnswerHeader_t` (transaction id, status)
    followed by optional TLV response data.
  - Host to device: always answered with `clusterCommandAnswerHeader_t`. The
    request is handled by the function registered with
    `mmic_set_cluster_command_handler()` (status `UNSUPPORTED_CLUSTER` when
    none). It runs on the MMIC task and must not block.
- `attribute_changed` (device to host): `attributeChangedHeader_t` (endpoint,
  cluster, attribute) followed by the new value as Matter TLV. No answer.

The device API is in `mmic_forward.h` (C) and `mmic_matter_forward.h` (typed
C++ helpers encoding Matter types as TLV). `mmic_host` currently only prints
the device requests (cluster, command or attribute names) and does not answer
them. The camera forwarding can be validated with `mmic_host --test-camera`, see
[../test/README.md](../test/README.md). The camera host application, which
handles these messages, is in
[examples/camera-app/silabs/host](../../../camera-app/silabs/host/README.md).

MMIC frames are limited to a few kilobytes (2048 bytes over the raw UART
transport): it is a control link, large payloads such as images must use
another path.

Application specific request op codes are handled by implementing
`mmic_app_handle_command()` (see `mmic_wake_on_matter.cpp`).

## Tests

Host-side tests live under [test/](test/) and are enabled by default
(disable with `-DMMIC_BUILD_TESTS=OFF`). Each `.cpp` file in that
directory is built into its own executable whose exit status is the
pass/fail result, and registered with CTest.

Build and run everything:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or run a single test binary directly:

```sh
./build/test/verify_test
```

To add a new test, drop `my_test.cpp` into `test/`, list it in
`MMIC_TEST_SOURCES` in [test/CMakeLists.txt](test/CMakeLists.txt), and
reconfigure. It will be linked against `mmic_core` (which owns the
shared host sources) and registered with CTest automatically.

## Clean

```sh
rm -rf build
```
