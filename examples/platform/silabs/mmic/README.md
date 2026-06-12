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

## Run

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

## Clean

```sh
rm -rf build
```
