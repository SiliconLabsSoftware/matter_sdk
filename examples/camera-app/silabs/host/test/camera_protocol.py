#
#    Copyright (c) 2026 Project CHIP Authors
#    All rights reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

"""Camera cluster identifiers and the TLV payloads the SoC forwards to camera_host.

The layouts match examples/camera-app/silabs/src/ForwardingCamera.cpp (SoC) and
examples/camera-app/silabs/host/src/CameraIds.h (host).
"""

from typing import List, Optional, Sequence

from matter_tlv import Writer

# Interaction Model status codes.
SUCCESS = 0x00
FAILURE = 0x01
UNSUPPORTED_ENDPOINT = 0x7F
UNSUPPORTED_COMMAND = 0x81
INVALID_COMMAND = 0x85
UNSUPPORTED_ATTRIBUTE = 0x86
CONSTRAINT_ERROR = 0x87
RESOURCE_EXHAUSTED = 0x89
NOT_FOUND = 0x8B
BUSY = 0x9C
UNSUPPORTED_CLUSTER = 0xC3
INVALID_IN_STATE = 0xCB
DYNAMIC_CONSTRAINT_ERROR = 0xCF

STATUS_NAMES = {
    SUCCESS: "SUCCESS",
    FAILURE: "FAILURE",
    UNSUPPORTED_ENDPOINT: "UNSUPPORTED_ENDPOINT",
    UNSUPPORTED_COMMAND: "UNSUPPORTED_COMMAND",
    INVALID_COMMAND: "INVALID_COMMAND",
    UNSUPPORTED_ATTRIBUTE: "UNSUPPORTED_ATTRIBUTE",
    CONSTRAINT_ERROR: "CONSTRAINT_ERROR",
    RESOURCE_EXHAUSTED: "RESOURCE_EXHAUSTED",
    NOT_FOUND: "NOT_FOUND",
    BUSY: "BUSY",
    UNSUPPORTED_CLUSTER: "UNSUPPORTED_CLUSTER",
    INVALID_IN_STATE: "INVALID_IN_STATE",
    DYNAMIC_CONSTRAINT_ERROR: "DYNAMIC_CONSTRAINT_ERROR",
}


def status_name(status: Optional[int]) -> str:
    if status is None:
        return "NO ANSWER"
    return STATUS_NAMES.get(status, f"0x{status:02x}")


# Camera endpoint on the SoC.
CAMERA_ENDPOINT = 1

# Clusters.
AV_STREAM_MANAGEMENT = 0x0551
AV_SETTINGS = 0x0552
WEBRTC_PROVIDER = 0x0553
WEBRTC_REQUESTOR = 0x0554

# Camera AV Stream Management commands.
AUDIO_STREAM_ALLOCATE = 0x00
AUDIO_STREAM_DEALLOCATE = 0x02
VIDEO_STREAM_ALLOCATE = 0x03
VIDEO_STREAM_MODIFY = 0x05
VIDEO_STREAM_DEALLOCATE = 0x06
SNAPSHOT_STREAM_ALLOCATE = 0x07
SNAPSHOT_STREAM_MODIFY = 0x09
SNAPSHOT_STREAM_DEALLOCATE = 0x0A
SET_STREAM_PRIORITIES = 0x0B
CAPTURE_SNAPSHOT = 0x0C

# Camera AV Settings User Level Management commands.
MPTZ_SET_POSITION = 0x00
MPTZ_RELATIVE_MOVE = 0x01
MPTZ_MOVE_TO_PRESET = 0x02
MPTZ_SAVE_PRESET = 0x03
MPTZ_REMOVE_PRESET = 0x04
DPTZ_SET_VIEWPORT = 0x05
DPTZ_RELATIVE_MOVE = 0x06

# WebRTC Transport Provider commands (SoC -> host).
SOLICIT_OFFER = 0x00
PROVIDE_OFFER = 0x02
PROVIDE_ANSWER = 0x04
PROVIDE_ICE_CANDIDATES = 0x05
END_SESSION = 0x06

# WebRTC Transport Requestor commands (host -> SoC).
REQUESTOR_OFFER = 0x00
REQUESTOR_ANSWER = 0x01
REQUESTOR_ICE_CANDIDATES = 0x02
REQUESTOR_END = 0x03

# Enumerations.
STREAM_USAGE_RECORDING = 0x01
STREAM_USAGE_LIVE_VIEW = 0x03
VIDEO_CODEC_H264 = 0x00
VIDEO_CODEC_HEVC = 0x01
AUDIO_CODEC_OPUS = 0x00
IMAGE_CODEC_JPEG = 0x00
IMAGE_CODEC_HEIC = 0x01
END_REASON_USER_HANGUP = 0x02

# Identity of the requestor (the controller) as seen by the SoC.
REQUESTOR_NODE_ID = 0x1122334455667788
REQUESTOR_ENDPOINT = 1
FABRIC_INDEX = 1

# Approximate image size limits the SoC sends with CaptureSnapshot (the SoC logs the exact value):
# TCP session with the 40 KB large buffers of the Wi-Fi camera app, or MRP session (single UDP packet).
SNAPSHOT_MAX_SIZE_TCP = 40000
SNAPSHOT_MAX_SIZE_MRP = 1000


def _resolution(writer: Writer, tag: int, width: int, height: int):
    writer.start_struct(tag).put_uint(0, width).put_uint(1, height).end()


def _viewport(writer: Writer, tag: Optional[int], viewport: Sequence[int]):
    x1, y1, x2, y2 = viewport
    writer.start_struct(tag).put_uint(0, x1).put_uint(1, y1).put_uint(2, x2).put_uint(3, y2).end()


def video_stream(stream_id: int, min_resolution=(640, 360), max_resolution=(1280, 720), min_fps=15, max_fps=30,
                 codec=VIDEO_CODEC_H264, min_bitrate=10000, max_bitrate=2000000, keyframe_interval_ms=1000,
                 watermark: Optional[bool] = None) -> bytes:
    """VideoStreamStruct with the allocated ID, as sent by the SoC for VideoStreamAllocate."""
    w = Writer().start_struct()
    w.put_uint(0, stream_id).put_uint(1, STREAM_USAGE_LIVE_VIEW).put_uint(2, codec)
    w.put_uint(3, min_fps).put_uint(4, max_fps)
    _resolution(w, 5, *min_resolution)
    _resolution(w, 6, *max_resolution)
    w.put_uint(7, min_bitrate).put_uint(8, max_bitrate).put_uint(9, keyframe_interval_ms)
    if watermark is not None:
        w.put_bool(10, watermark)
    w.put_uint(12, 0)
    return w.end().data()


def audio_stream(stream_id: int, channels=1, sample_rate=48000, bitrate=32000, codec=AUDIO_CODEC_OPUS) -> bytes:
    """AudioStreamStruct with the allocated ID."""
    w = Writer().start_struct()
    w.put_uint(0, stream_id).put_uint(1, STREAM_USAGE_LIVE_VIEW).put_uint(2, codec).put_uint(3, channels)
    w.put_uint(4, sample_rate).put_uint(5, bitrate).put_uint(6, 16).put_uint(7, 0)
    return w.end().data()


def snapshot_stream(stream_id: int, resolution=(640, 480), quality=90, codec=IMAGE_CODEC_JPEG) -> bytes:
    """SnapshotStreamStruct with the allocated ID."""
    w = Writer().start_struct()
    w.put_uint(0, stream_id).put_uint(1, codec).put_uint(2, 30)
    _resolution(w, 3, *resolution)
    _resolution(w, 4, *resolution)
    w.put_uint(5, quality).put_uint(6, 0).put_bool(7, False).put_bool(8, False)
    return w.end().data()


def stream_request(stream_id: int, watermark: Optional[bool] = None, osd: Optional[bool] = None) -> bytes:
    """Video/Audio/Snapshot StreamModify or StreamDeallocate request."""
    w = Writer().start_struct().put_uint(0, stream_id)
    if watermark is not None:
        w.put_bool(1, watermark)
    if osd is not None:
        w.put_bool(2, osd)
    return w.end().data()


def set_stream_priorities(usages: Sequence[int]) -> bytes:
    w = Writer().start_struct().start_array(0)
    for usage in usages:
        w.put_uint(None, usage)
    return w.end().end().data()


def capture_snapshot(stream_id: Optional[int], resolution=(640, 480), max_size: int = SNAPSHOT_MAX_SIZE_TCP) -> bytes:
    """CaptureSnapshot request, then the largest image the SoC can return (uint32, added by the SoC)."""
    w = Writer().start_struct()
    if stream_id is None:
        w.put_null(0)
    else:
        w.put_uint(0, stream_id)
    _resolution(w, 1, *resolution)
    return w.end().put_uint(None, max_size).data()


def mptz(pan: Optional[int] = None, tilt: Optional[int] = None, zoom: Optional[int] = None) -> bytes:
    """MPTZStruct: target position of a mechanical move."""
    w = Writer().start_struct()
    if pan is not None:
        w.put_int(0, pan)
    if tilt is not None:
        w.put_int(1, tilt)
    if zoom is not None:
        w.put_uint(2, zoom)
    return w.end().data()


def preset_request(preset_id: int) -> bytes:
    return Writer().start_struct().put_uint(0, preset_id).end().data()


def dptz(stream_id: int, viewport: Sequence[int]) -> bytes:
    """DPTZSetViewport request / DPTZStruct (same layout)."""
    w = Writer().start_struct().put_uint(0, stream_id)
    _viewport(w, 1, viewport)
    return w.end().data()


def _session(w: Writer, session_id: int, video_streams: List[int], audio_streams: List[int]):
    """WebRTCSessionStruct created by the SoC for the session."""
    w.start_struct()
    w.put_uint(0, session_id).put_uint(1, REQUESTOR_NODE_ID).put_uint(2, REQUESTOR_ENDPOINT)
    w.put_uint(3, STREAM_USAGE_LIVE_VIEW)
    if video_streams:
        w.put_uint(4, video_streams[0])
    else:
        w.put_null(4)
    if audio_streams:
        w.put_uint(5, audio_streams[0])
    else:
        w.put_null(5)
    w.put_bool(6, False)
    if video_streams:
        w.start_array(7)
        for stream_id in video_streams:
            w.put_uint(None, stream_id)
        w.end()
    if audio_streams:
        w.start_array(8)
        for stream_id in audio_streams:
            w.put_uint(None, stream_id)
        w.end()
    w.put_uint(254, FABRIC_INDEX)
    w.end()


def _stream_lists(w: Writer, video_tag: int, audio_tag: int, video_streams: List[int], audio_streams: List[int]):
    if video_streams:
        w.start_array(video_tag)
        for stream_id in video_streams:
            w.put_uint(None, stream_id)
        w.end()
    if audio_streams:
        w.start_array(audio_tag)
        for stream_id in audio_streams:
            w.put_uint(None, stream_id)
        w.end()


def solicit_offer(session_id: int, video_streams: List[int], audio_streams: List[int]) -> bytes:
    """WebRTCSessionStruct, then the SolicitOffer request."""
    w = Writer()
    _session(w, session_id, video_streams, audio_streams)
    w.start_struct().put_uint(0, STREAM_USAGE_LIVE_VIEW).put_uint(1, REQUESTOR_ENDPOINT)
    _stream_lists(w, 8, 9, video_streams, audio_streams)
    return w.end().data()


def provide_offer(session_id: int, sdp: str, video_streams: List[int], audio_streams: List[int]) -> bytes:
    """WebRTCSessionStruct, then the ProvideOffer request."""
    w = Writer()
    _session(w, session_id, video_streams, audio_streams)
    w.start_struct().put_uint(0, session_id).put_str(1, sdp).put_uint(2, STREAM_USAGE_LIVE_VIEW)
    w.put_uint(3, REQUESTOR_ENDPOINT)
    _stream_lists(w, 10, 11, video_streams, audio_streams)
    return w.end().data()


def provide_answer(session_id: int, sdp: str) -> bytes:
    return Writer().start_struct().put_uint(0, session_id).put_str(1, sdp).end().data()


def provide_ice_candidates(session_id: int, candidates: Sequence[tuple]) -> bytes:
    """candidates: (candidate, mid) pairs."""
    w = Writer().start_struct().put_uint(0, session_id).start_array(1)
    for candidate, mid in candidates:
        w.start_struct().put_str(0, candidate)
        if mid:
            w.put_str(1, mid)
        else:
            w.put_null(1)
        w.put_null(2).end()
    return w.end().end().data()


def end_session(session_id: int, reason=END_REASON_USER_HANGUP) -> bytes:
    return Writer().start_struct().put_uint(0, session_id).put_uint(1, reason).end().data()
