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
| SnapshotStreamAllocate      | JPEG only. Accepted, but snapshots are not captured yet.                                                       |
| CaptureSnapshot             | Not implemented yet: `UNSUPPORTED_COMMAND`.                                                                    |
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

## Limitations

-   **Matter message size**: the Silabs SoCs build Matter without TCP, so every
    command must fit in a single Matter message (about 1.1 kB). The host removes
    the ICE candidates from the SDP and sends them in batches, but an SDP offer or
    answer (camera or controller side) can still exceed that size: the SoC then
    fails to send or receive it. The host logs a warning for SDPs above 1000
    bytes.
-   **Snapshots**: `CaptureSnapshot` is not implemented. An image does not fit in
    a Matter message without TCP, and the MMIC UART link is not meant for large
    payloads.
-   **Encoding**: software H.264 (`x264enc`), one video stream at a time, up to
    1280x720 at 30 fps.
-   **Audio**: capture only (no speaker, no two-way talk), requires a microphone
    (none on a Raspberry Pi 4 with a camera module).
-   **Restarts**: the host does not persist the stream allocations. After a host
    restart, reset the SoC so that it sends them again.
