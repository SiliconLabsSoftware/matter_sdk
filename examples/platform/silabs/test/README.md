# MMIC camera test

Validates that the camera SoC (`slc/apps/camera_app`) forwards the camera
commands and attribute changes to the Linux host over MMIC.

`mmic_host --test-camera` waits for a known set of messages from the SoC,
displays a checklist, and ends with a success message once everything has been
received. A chip-tool controller sends the Matter commands to the SoC.

```
mmic_host --- TESTING CAMERA support ---

  Set stream priorities            -- check
  Video stream allocate            -- check
  Audio stream allocate            -- waiting
  ...

Last message: command CameraAvStreamManagement.VideoStreamAllocate (endpoint 1)

Received 2 out of 22
ctrl+c to end the test early
```

The test accepts every command the SoC waits an answer for (status `SUCCESS`)
and answers `CaptureSnapshot` with a small fake JPEG image, so the Matter
commands succeed. It does not produce any WebRTC signaling (Offer, Answer):
the WebRTC commands only validate the forwarding. Use
[camera_host](../../../camera-app/silabs/host/README.md) for real streaming.

## Setup

- SoC: camera app built from
  `slc/apps/camera_app/thread/matter_thread_soc_camera_app_freertos.slcp` or
  `slc/apps/camera_app/wifi/matter_wifi_soc_camera_app_freertos.slcp`. These
  use the raw UART MMIC transport (`matter_mmic_uart`) on the board VCOM, logs
  are on RTT.
- Linux host: connected to the board VCOM (115200 8N1, usually
  `/dev/ttyACM0`), with CMake >= 3.13, a C++17 compiler and the OpenSSL
  development files (`sudo apt install build-essential cmake libssl-dev`).
- A chip-tool controller able to commission the SoC (Thread border router for
  the Thread variant).

## Build

From this directory:

```sh
cmake -S . -B build
cmake --build build
```

For a SoC using the CPC transport (`matter_mmic_cpc`), configure with
`-DMMIC_USE_CPC=ON` (see `../mmic/README.md` for the CPC prerequisites).

## Run

```sh
./build/mmic_host /dev/ttyACM0 --test-camera     # UART build
./build/mmic_host --test-camera                  # CPC build (default cpcd instance)
```

The exit code is `0` when every message was received, `1` otherwise
(for example when interrupted with ctrl+c).

## Test sequence

Start `mmic_host --test-camera`, then commission the SoC and send the commands
below with chip-tool. The camera is on endpoint `1`; `${NODE}` is the node id
used for commissioning. Messages can be received in any order, but the sequence
below satisfies the cluster preconditions.

### Commissioning

The default test credentials are passcode `20202021` and discriminator `3840`.

```sh
NODE=1
# Thread
chip-tool pairing ble-thread ${NODE} hex:<operational dataset> 20202021 3840
# Wi-Fi
chip-tool pairing ble-wifi ${NODE} <ssid> <password> 20202021 3840
```

### Camera AV Stream Management

Run on a device without allocated streams (stream priorities can only be set
when no stream is allocated). Stream ids start at `0`.

| Checklist entry          | chip-tool command |
| ------------------------ | ----------------- |
| Set stream priorities    | `chip-tool cameraavstreammanagement set-stream-priorities '[3, 1]' ${NODE} 1` |
| Video stream allocate    | `chip-tool cameraavstreammanagement video-stream-allocate 3 0 30 30 '{"width":1920,"height":1080}' '{"width":1920,"height":1080}' 10000 2000000 4000 ${NODE} 1` |
| Audio stream allocate    | `chip-tool cameraavstreammanagement audio-stream-allocate 3 0 1 48000 32000 16 ${NODE} 1` |
| Snapshot stream allocate | `chip-tool cameraavstreammanagement snapshot-stream-allocate 0 30 '{"width":640,"height":480}' '{"width":640,"height":480}' 90 ${NODE} 1` |
| Capture snapshot         | `chip-tool cameraavstreammanagement capture-snapshot null '{"width":640,"height":480}' ${NODE} 1` |
| Viewport changed         | `chip-tool cameraavstreammanagement write viewport '{"x1":0,"y1":0,"x2":1280,"y2":720}' ${NODE} 1` |
| Microphone muted changed | `chip-tool cameraavstreammanagement write microphone-muted 1 ${NODE} 1` |

### WebRTC Transport Provider

`<session>` is the `webRTCSessionID` returned in the `SolicitOfferResponse`.

| Checklist entry               | chip-tool command |
| ----------------------------- | ----------------- |
| Start WebRTC (solicit offer)  | `chip-tool webrtctransportprovider solicit-offer 3 1 ${NODE} 1 --VideoStreams '[0]' --AudioStreams '[0]'` |
| Start WebRTC (provide offer)  | `chip-tool webrtctransportprovider provide-offer null "v=0" 3 1 ${NODE} 1 --VideoStreams '[0]'` |
| WebRTC provide answer         | `chip-tool webrtctransportprovider provide-answer <session> "v=0" ${NODE} 1` |
| WebRTC provide ICE candidates | `chip-tool webrtctransportprovider provide-icecandidates <session> '[{"candidate":"candidate:1 1 UDP 2122252543 192.168.1.2 49152 typ host","SDPMid":"0","SDPMLineIndex":0}]' ${NODE} 1` |
| Stop WebRTC (end session)     | `chip-tool webrtctransportprovider end-session <session> 2 ${NODE} 1` |

### Camera AV Settings User Level Management

A mechanical move is considered complete 2 seconds after it starts: wait at
least 2 seconds between the pan/tilt/zoom commands, otherwise they fail with
`BUSY`. The digital PTZ commands use the video stream `0` allocated above.

| Checklist entry              | chip-tool command |
| ---------------------------- | ----------------- |
| Pan/tilt/zoom set position   | `chip-tool cameraavsettingsuserlevelmanagement mptzset-position ${NODE} 1 --Pan 10 --Tilt 10 --Zoom 5` |
| Pan/tilt/zoom relative move  | `chip-tool cameraavsettingsuserlevelmanagement mptzrelative-move ${NODE} 1 --PanDelta 5` |
| Pan/tilt/zoom save preset    | `chip-tool cameraavsettingsuserlevelmanagement mptzsave-preset "Test" ${NODE} 1 --PresetID 1` |
| Pan/tilt/zoom move to preset | `chip-tool cameraavsettingsuserlevelmanagement mptzmove-to-preset 1 ${NODE} 1` |
| Pan/tilt/zoom remove preset  | `chip-tool cameraavsettingsuserlevelmanagement mptzremove-preset 1 ${NODE} 1` |
| Digital PTZ set viewport     | `chip-tool cameraavsettingsuserlevelmanagement dptzset-viewport 0 '{"x1":0,"y1":0,"x2":1280,"y2":720}' ${NODE} 1` |
| Digital PTZ relative move    | `chip-tool cameraavsettingsuserlevelmanagement dptzrelative-move 0 ${NODE} 1 --DeltaX 10` |

### Teardown

| Checklist entry            | chip-tool command |
| -------------------------- | ----------------- |
| Video stream deallocate    | `chip-tool cameraavstreammanagement video-stream-deallocate 0 ${NODE} 1` |
| Audio stream deallocate    | `chip-tool cameraavstreammanagement audio-stream-deallocate 0 ${NODE} 1` |
| Snapshot stream deallocate | `chip-tool cameraavstreammanagement snapshot-stream-deallocate 0 ${NODE} 1` |

## Not covered

`VideoStreamModify` and `SnapshotStreamModify` are forwarded by the SoC but
require the Watermark or OSD features, which the camera app does not enable;
the cluster rejects them before they reach the forwarding layer.
