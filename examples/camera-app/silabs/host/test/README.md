# camera_host tester

Tests [camera_host](../README.md) without a camera SoC. `camera_host_tester.py`
replaces the SoC: it sends the camera commands the SoC forwards over MMIC, plays
the WebRTC requestor (the Matter controller) and displays the stream.

```
 PC (macOS or Linux)                          Camera machine (Raspberry Pi)
 camera_host_tester.py  <---- TCP 5555 ---->  socat <-> /tmp/ttyMMIC (pty) <-> camera_host
   MMIC commands, WebRTC signaling                                              |
   WebRTC viewer (aiortc)  <================ WebRTC media =====================+ (streaming interface)
```

The camera machine runs the standard `camera_host` binary. `socat` creates a
virtual UART (`/tmp/ttyMMIC`), passed to `camera_host` as MMIC tty, and bridges
it to a TCP port. The tester connects to that port and exchanges the same MMIC
frames as the SoC would over the real UART. The WebRTC signaling produced by
`camera_host` (offer, answer, ICE candidates, end), normally relayed by the SoC
to the Matter controller, is handled by the tester. The media flows directly
from the `camera_host` streaming interface to the PC.

## Contents

-   [Camera machine](#camera-machine)
-   [PC](#pc)
-   [Using the tester](#using-the-tester)
-   [Test suite](#test-suite)
-   [Troubleshooting](#troubleshooting)

## Camera machine

1. Build `camera_host`, see [../README.md](../README.md).
2. Install socat:

    ```sh
    sudo apt install socat
    ```

3. Start `camera_host` behind the TCP bridge:

    ```sh
    ./start_camera_host.sh wlan0 video0
    ```

    Arguments: streaming network interface, camera device, then optionally the
    TCP port (default `5555`) and the `camera_host` binary (default
    `../build/camera_host`). The script runs `camera_host` through
    `libcamerify` when it is installed; set `USE_LIBCAMERIFY=0` for a USB
    camera.

    socat serves a single connection: when the tester disconnects, the script
    restarts socat and `camera_host`, ready for the next session. Stop it with
    ctrl+c.

The equivalent manual commands, in two terminals:

```sh
socat pty,link=/tmp/ttyMMIC,rawer tcp-listen:5555,reuseaddr
libcamerify ../build/camera_host /tmp/ttyMMIC wlan0 video0
```

The PC must reach the camera machine on the TCP port, and on the UDP ports
used by WebRTC (any port, on the streaming interface): allow them in the
firewall if one is enabled.

## PC

Python 3.9 or later. The tester uses PyQt5 for the UI,
[aiortc](https://github.com/aiortc/aiortc) for WebRTC (with PyAV for the H.264
and Opus decoding) and qasync to run asyncio in the Qt event loop.

From this directory:

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Run the tester with the address of the camera machine:

```sh
python3 camera_host_tester.py --host <camera machine address> --port 5555
```

The tester can also run on the camera machine itself (desktop session):
`--host 127.0.0.1`.

The tester reads the MMIC op codes from
`examples/platform/silabs/mmic/mmic.h`: keep it in this source tree.

## Using the tester

1. Click **Connect**. The status shows `Connected`, the log at the bottom
   shows every command and its status.
2. Check **Camera has a microphone** when the camera machine has an audio
   capture device: it changes the expected result of the audio test and adds
   audio to the streams.
3. **Run Test Suite** runs every test one by one. Each line shows ✅ when
   `camera_host` answers with the expected status, ❌ otherwise with the expected
   and received status. The last line reads, for example,
   `36 of 36 commands succeeded`. The video panel shows the stream while the
   WebRTC tests run.
4. **Start Stream** allocates a video stream (and an audio stream with a
   microphone), opens a WebRTC session (camera offer) and displays the video.
   **Stop Stream** ends the session and releases the streams.
5. **Take Snapshot** allocates a snapshot stream and sends `CaptureSnapshot`.
   The image is displayed when `camera_host` returns one. Snapshots are not
   implemented by `camera_host` yet: the panel shows `UNSUPPORTED_COMMAND`.

Each test suite run starts and ends by releasing the streams it uses. Disconnect
before starting another tester: `camera_host` restarts for every connection.

## Test suite

The expected statuses follow the current `camera_host` behavior (see
[../README.md](../README.md#supported-commands)). Update `test_suite.py` when
`camera_host` changes, for example when snapshots get implemented.

| Test                                                    | Expected                                        |
| ------------------------------------------------------- | ----------------------------------------------- |
| SetStreamPriorities                                     | `SUCCESS`                                       |
| VideoStreamAllocate HEVC                                | `CONSTRAINT_ERROR` (H.264 only)                 |
| VideoStreamAllocate 1080p only                          | `RESOURCE_EXHAUSTED` (encodes up to 1280x720)   |
| VideoStreamAllocate (640x360 to 1280x720, 15 to 30 fps) | `SUCCESS`                                       |
| VideoStreamAllocate second stream                       | `RESOURCE_EXHAUSTED` (one encoder)              |
| VideoStreamModify / with watermark                      | `SUCCESS` / `CONSTRAINT_ERROR`                  |
| AudioStreamAllocate                                     | `SUCCESS` with a microphone, `RESOURCE_EXHAUSTED` without |
| SnapshotStreamAllocate JPEG / HEIC                      | `SUCCESS` / `CONSTRAINT_ERROR`                  |
| CaptureSnapshot                                         | `UNSUPPORTED_COMMAND` (not implemented yet)     |
| DPTZSetViewport, DPTZRelativeMove                       | `SUCCESS`, `NOT_FOUND` for an unknown stream    |
| MPTZSetPosition, RelativeMove, MoveToPreset, SavePreset, RemovePreset | `UNSUPPORTED_COMMAND` (no mechanical PTZ) |
| SolicitOffer, ProvideAnswer, ProvideICECandidates, EndSession | `SUCCESS`                                  |
| Offer, ICE candidates and video received (camera offer) | Received from `camera_host` in time             |
| ProvideOffer, EndSession                                | `SUCCESS`                                       |
| Answer and video received (requestor offer)             | Received from `camera_host` in time             |
| ProvideAnswer unknown session                           | `NOT_FOUND`                                     |
| SolicitOffer unallocated stream                         | `NOT_FOUND`                                     |
| Video, Audio, SnapshotStreamDeallocate                  | `SUCCESS`                                       |

The signaling checks wait up to 10 seconds, the video checks up to 20 seconds
(pipeline start and WebRTC connection).

## Troubleshooting

-   **Connection refused**: `start_camera_host.sh` is not running, or the
    previous session is still restarting (wait a second).
-   **Connection closed right after connecting**: `camera_host` exited, check
    its output on the camera machine (camera device, network interface).
-   **Offer received but no video**: the PC cannot reach the address of the
    streaming interface given to `camera_host` (other network, firewall, VPN).
    The log shows the WebRTC connection state. `camera_host` logs the GStreamer
    errors (camera not found, missing `libcamerify`).
-   **All commands show `NO ANSWER`**: the TCP connection is up but
    `camera_host` does not read the virtual UART: check that it was started on
    `/tmp/ttyMMIC`.
