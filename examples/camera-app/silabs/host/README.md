# Silabs camera-app: Linux camera host

`camera_host` is the camera part of the Silabs camera-app. The application is
split in two:

-   **SoC** (`slc/apps/camera_app`, sources in `../src`): Matter data model,
    commissioning, attribute storage and command validation, Thread or Wi-Fi.
-   **Linux host** (this application): camera capture, encoding, WebRTC
    streaming and camera controls.

```
 Matter controller                 SoC (camera_app)                 Linux host (camera_host)
 ----------------- Matter -------> data model, storage  -- MMIC --> V4L2 / GStreamer / WebRTC
                  <-- Offer/Answer/ICE relayed by SoC --  (UART)  <--  SDP, ICE candidates
                  <=================== WebRTC media (host network interface) ===============
```

The SoC forwards every camera command to the host over the MMIC protocol
(`examples/platform/silabs/mmic`). The host answers with the Matter Interaction
Model status of the operation, which the SoC returns to the Matter client: a
request the camera cannot honor (unsupported codec, resolution, missing
microphone, ...) fails on the controller side with that status. The WebRTC
signaling produced by the host (SDP offer or answer, ICE candidates, end of
session) goes back to the SoC, which sends it to the requestor. The media itself
never goes through the SoC: it flows from the host network interface to the
peer.

## Contents

-   [Requirements](#requirements)
-   [Building](#building)
-   [Running](#running)
-   [Supported commands](#supported-commands)
-   [Example: live view](#example-live-view)
-   [Testing without the SoC](#testing-without-the-soc)
-   [Limitations](#limitations)

## Requirements

Reference setup: Raspberry Pi 4, Raspberry Pi camera module, Ubuntu 24.04.

-   A camera SoC running `slc/apps/camera_app` (Thread or Wi-Fi), built with the
    raw UART MMIC transport (`matter_mmic_uart`, the default of these projects).
    MMIC uses the board VCOM at 115200 8N1, the SoC logs are on RTT.
-   The SoC VCOM connected to the host over USB (usually `/dev/ttyACM0`).
-   A network interface on the host reachable by the WebRTC peers (`wlan0`,
    `eth0`, ...).

Packages (Ubuntu 24.04):

```sh
sudo apt update
sudo apt install \
    build-essential cmake pkg-config git libssl-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-ugly gstreamer1.0-alsa \
    libcamera-tools libcamera-v4l2 v4l-utils
```

-   `gstreamer1.0-plugins-ugly` provides the `x264enc` H.264 encoder,
    `gstreamer1.0-plugins-base` the Opus encoder.
-   `libcamera-v4l2` provides `libcamerify`, required by the Raspberry Pi
    camera (see [Running](#running)).

If apt reports unmet dependencies between a `-dev` package and its library,
for example:

```
libselinux1-dev : Depends: libselinux1 (= 3.5-2ubuntu2) but 3.5-2ubuntu2.1 is to be installed
libzstd-dev : Depends: libzstd1 (= 1.5.5+dfsg2-2build1) but 1.5.5+dfsg2-2build1.1 is to be installed
```

the installed libraries come from `noble-updates` but apt does not see that
pocket, so it only finds the older `-dev` packages of `noble`. Check that the
`Suites:` line of `/etc/apt/sources.list.d/ubuntu.sources` contains
`noble noble-updates noble-backports` (and that the security entry contains
`noble-security`), then refresh the package lists before installing:

```sh
sudo apt update
apt-cache policy libselinux1 libselinux1-dev # Both candidates must have the same version
```

The CMake error `The following required packages were not found: gstreamer-1.0`
means that `libgstreamer1.0-dev` is not installed: fix the apt errors first.

Give the user access to the camera and to the UART:

```sh
sudo usermod -aG video,dialout $USER
# Log out and back in, or run: newgrp video
```

## Building

`camera_host` uses CMake. It does not link the Matter SDK: the MMIC framing is
shared with the SoC (`examples/platform/silabs/mmic/mmic.cpp`) and the Matter
TLV payloads are handled by a small codec (`src/Tlv.cpp`).

WebRTC uses [libdatachannel](https://github.com/paullouisageneau/libdatachannel),
built from the Matter SDK submodule. From the Matter SDK root
(`third_party/matter_sdk` in the extension):

```sh
git submodule update --init --recursive third_party/libdatachannel/repo
```

Then, from this directory:

```sh
cmake -S . -B build
cmake --build build -j4
```

The binary is `build/camera_host`.

To use an installed libdatachannel instead of the submodule:

```sh
cmake -S . -B build -DCAMERA_HOST_USE_SYSTEM_LIBDATACHANNEL=ON
```

## Running

```
camera_host <mmic tty> <streaming network interface> <camera device>
```

| Argument                    | Description                                    | Example        |
| --------------------------- | ---------------------------------------------- | -------------- |
| mmic tty                    | UART connected to the SoC VCOM                 | `/dev/ttyACM0` |
| streaming network interface | Interface the WebRTC (ICE) sockets are bound to | `wlan0`        |
| camera device               | V4L2 device, `/dev/` is optional               | `video0`       |

The Raspberry Pi camera is driven by libcamera: the application must run through
`libcamerify`, which exposes it as a V4L2 device:

```sh
libcamerify ./build/camera_host /dev/ttyACM0 wlan0 video0
```

A USB (UVC) camera does not need `libcamerify`.

Start `camera_host` before the SoC, or reset the SoC once `camera_host` runs:
the SoC sends its persisted stream allocations to the host at boot only.

## Supported commands

Status codes are the Matter Interaction Model status returned to the Matter
client through the SoC.

### Camera AV Stream Management

| Command / attribute         | Host behavior                                                                                                 |
| --------------------------- | ------------------------------------------------------------------------------------------------------------- |
| VideoStreamAllocate         | H.264 only (`CONSTRAINT_ERROR` otherwise). Encodes the largest resolution of the requested range up to 1280x720 and up to 30 fps, `RESOURCE_EXHAUSTED` when the range is above. One video stream at a time (`RESOURCE_EXHAUSTED`). |
| AudioStreamAllocate         | Opus, 1 or 2 channels, 8 to 48 kHz. `RESOURCE_EXHAUSTED` when no microphone is detected.                     |
| SnapshotStreamAllocate      | JPEG only (`CONSTRAINT_ERROR` otherwise). The maximum resolution and quality are used by CaptureSnapshot.     |
| CaptureSnapshot             | JPEG image, see [Snapshots](#snapshots). `NOT_FOUND` for an unknown stream, `RESOURCE_EXHAUSTED` when the image cannot fit the size limit of the SoC. |
| Video/SnapshotStreamModify  | Watermark and OSD are not supported (`CONSTRAINT_ERROR`).                                                      |
| *StreamDeallocate           | Stops the stream.                                                                                              |
| SetStreamPriorities         | Accepted.                                                                                                      |
| Viewport                    | Crops the video stream.                                                                                        |
| ImageRotation, ImageFlip*   | 0 and 180 degree rotations and flips.                                                                          |
| MicrophoneMuted/VolumeLevel | Applied to the audio stream.                                                                                   |
| Soft/Hard privacy modes     | End the WebRTC sessions; the hard privacy mode also stops the capture.                                         |
| HDR, night vision, speaker, local recording, status light | Not supported: the value is only stored on the SoC (logged on the host).        |

Attribute changes are notifications: a value the camera cannot apply stays
stored on the SoC, the error is only logged by the host.

### WebRTC Transport Provider

| Command              | Host behavior                                                                                       |
| -------------------- | --------------------------------------------------------------------------------------------------- |
| SolicitOffer         | Creates the peer connection and sends the SDP offer, then the ICE candidates, through the SoC.     |
| ProvideOffer         | Applies the offer and sends the SDP answer, then the ICE candidates, through the SoC.               |
| ProvideAnswer        | Applies the answer.                                                                                  |
| ProvideICECandidates | Adds the remote candidates.                                                                          |
| EndSession           | Closes the peer connection.                                                                          |

Up to 4 sessions (`RESOURCE_EXHAUSTED`). When a peer connection fails or is
closed by the peer, the host sends `End` to the requestor through the SoC.

### Camera AV Settings User Level Management

| Command                 | Host behavior                                               |
| ----------------------- | ----------------------------------------------------------- |
| MPTZ*                   | No mechanical pan/tilt/zoom: `UNSUPPORTED_COMMAND`.         |
| DPTZSetViewport         | Crops the video stream to the viewport.                     |
| DPTZRelativeMove        | Crops the video stream to the viewport resolved by the SoC. |

## Snapshots

The Matter specification carries the image in the `Data` field of the
CaptureSnapshotResponse: the whole image is one Matter message, sent by the SoC.

1.  The SoC forwards CaptureSnapshot with the largest image the response can
    carry on the session of the client: about 40 kB on a TCP session (Wi-Fi
    app, set by `CHIP_SYSTEM_CONFIG_MAX_LARGE_BUFFER_SIZE_BYTES` in
    `include/CHIPProjectConfig.h`), about 1 kB on an MRP (UDP) session, the only
    one available on Thread. The SoC logs the exact limit.
2.  The host takes the latest frame of the running video stream, or starts the
    camera for a few frames when there is none. It encodes a JPEG image at the
    requested resolution, capped by the maximum resolution of the snapshot
    stream, with the quality of the stream. Until the image fits the limit, the
    quality is lowered (60, 40, 20), then the resolution is halved (down to 80
    pixels wide). `RESOURCE_EXHAUSTED` is returned when it never fits.
3.  The image is sent to the SoC with MMIC `bulk_data` frames (2 kB chunks, each
    acknowledged by the SoC), then the answer carries the codec and resolution.
    The SoC returns the image to the client in the CaptureSnapshotResponse.

At 115200 baud the link carries about 11 kB/s: a 40 kB image takes about 4
seconds, during which the Matter thread of the SoC is blocked.

With chip-tool (Wi-Fi SoC, TCP session):

```sh
chip-tool cameraavstreammanagement snapshot-stream-allocate 0 30 '{"width":640,"height":480}' '{"width":640,"height":480}' 90 ${NODE} 1
chip-tool cameraavstreammanagement capture-snapshot null '{"width":640,"height":480}' ${NODE} 1 --allow-large-payload 1 --timeout 20
```

Without `--allow-large-payload 1`, chip-tool uses MRP and gets a much smaller
image, or `RESOURCE_EXHAUSTED`.

## Example: live view

The `camera-controller` example of the Matter SDK
(`examples/camera-controller`) acts as WebRTC requestor and displays the
stream. After commissioning the SoC:

```
liveview start <node id> --min-res-width 640 --min-res-height 480 --min-framerate 30
```

The controller allocates a video stream (accepted up to 1280x720), then opens a
WebRTC session.

With chip-tool, the stream commands can be checked individually, for example
(camera on endpoint 1):

```sh
chip-tool cameraavstreammanagement video-stream-allocate 3 0 15 30 '{"width":640,"height":360}' '{"width":1280,"height":720}' 10000 2000000 1000 ${NODE} 1
chip-tool cameraavstreammanagement video-stream-deallocate 0 ${NODE} 1
```

## Testing without the SoC

[test/](test/README.md) contains a PyQt5 tester that replaces the SoC: run
`camera_host` on a virtual UART bridged to TCP by socat, then run the test suite
or view the stream from a PC.

## Limitations

-   **Matter message size**: the Thread camera app builds Matter without TCP, so
    every command must fit in a single Matter message (about 1.1 kB). The Wi-Fi
    app enables TCP for large messages, but a controller may still use MRP (UDP).
    The host removes the ICE candidates from the SDP and sends them in batches,
    but an SDP offer or answer (camera or controller side) can still exceed that
    size: the SoC then fails to send or receive it. The host logs a warning for
    SDPs above 1000 bytes.
-   **Snapshots**: limited to what one CaptureSnapshotResponse can carry, see
    [Snapshots](#snapshots). The one-frame capture used without a video stream
    does not apply the viewport.
-   **Encoding**: software H.264 (`x264enc`), one video stream at a time, up to
    1280x720 at 30 fps.
-   **Audio**: capture only (no speaker, no two-way talk), requires a microphone
    (none on a Raspberry Pi 4 with a camera module).
-   **Restarts**: the host does not persist the stream allocations. After a host
    restart, reset the SoC so that it sends them again.
