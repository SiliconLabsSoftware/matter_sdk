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

"""camera_host test suite. Each test sends one command and checks the status against the camera_host
behavior (README.md of the host); signaling checks wait for an event of camera_host."""

import asyncio
from dataclasses import dataclass
from typing import Awaitable, Callable, Dict, List, Optional, Tuple

import camera_protocol as proto
from camera_tester import MEDIA_TIMEOUT, SIGNALING_TIMEOUT, CameraTester
from requestor import RequestorSession

VIDEO_STREAM = 0
AUDIO_STREAM = 0
SNAPSHOT_STREAM = 0
SOLICIT_SESSION = "solicit"
PROVIDE_SESSION = "provide"


@dataclass
class TestResult:
    name: str
    passed: bool
    detail: str


@dataclass
class TestCase:
    name: str
    run: Callable[[], Awaitable[Tuple[bool, str]]]


class TestSuite:
    def __init__(self, tester: CameraTester, has_microphone: bool):
        self._tester = tester
        self._has_microphone = has_microphone
        self._sessions: Dict[str, RequestorSession] = {}

    async def run(self, on_result: Callable[[TestResult], None]) -> Tuple[int, int]:
        await self._cleanup()
        passed = 0
        cases = self._cases()
        for case in cases:
            try:
                ok, detail = await case.run()
            except Exception as error:
                ok, detail = False, f"error: {error}"
            passed += ok
            on_result(TestResult(case.name, ok, detail))
        await self._cleanup()
        return passed, len(cases)

    async def _cleanup(self):
        """Releases the sessions and streams of a previous run (ignored results)."""
        await self._tester.close_all_sessions()
        for command, stream_id in ((proto.VIDEO_STREAM_DEALLOCATE, VIDEO_STREAM), (proto.AUDIO_STREAM_DEALLOCATE, AUDIO_STREAM),
                                   (proto.SNAPSHOT_STREAM_DEALLOCATE, SNAPSHOT_STREAM)):
            await self._tester.client.send_command(proto.CAMERA_ENDPOINT, proto.AV_STREAM_MANAGEMENT, command,
                                                   proto.stream_request(stream_id))
        self._sessions.clear()

    # Helpers

    def _expect(self, name: str, cluster: int, command: int, params: bytes, expected: int) -> TestCase:
        async def run():
            status, _ = await self._tester.command(name, cluster, command, params)
            if status == expected:
                return True, proto.status_name(status)
            return False, f"expected {proto.status_name(expected)}, got {proto.status_name(status)}"
        return TestCase(name, run)

    def _avsm(self, name: str, command: int, params: bytes, expected: int) -> TestCase:
        return self._expect(name, proto.AV_STREAM_MANAGEMENT, command, params, expected)

    def _settings(self, name: str, command: int, params: bytes, expected: int) -> TestCase:
        return self._expect(name, proto.AV_SETTINGS, command, params, expected)

    def _wait(self, name: str, key: str, event_name: str, timeout: float) -> TestCase:
        async def run():
            session = self._sessions.get(key)
            if session is None:
                return False, "no session"
            try:
                await asyncio.wait_for(getattr(session, event_name).wait(), timeout)
            except asyncio.TimeoutError:
                return False, f"nothing received within {timeout:.0f} s"
            if event_name == "first_video_frame":
                await asyncio.sleep(1.0)  # Show some video before going on
                return True, f"{session.video_frames} frame(s)"
            return True, "received"
        return TestCase(name, run)

    def _session_step(self, name: str, key: str, step: Callable[[RequestorSession], Awaitable[Optional[int]]]) -> TestCase:
        async def run():
            session = self._sessions.get(key)
            if session is None:
                return False, "no session"
            status = await step(session)
            if status == proto.SUCCESS:
                return True, proto.status_name(status)
            return False, f"expected SUCCESS, got {proto.status_name(status)}"
        return TestCase(name, run)

    def _open_session(self, name: str, key: str, solicit: bool) -> TestCase:
        async def run():
            session = self._tester.new_session()
            self._sessions[key] = session
            video, audio = [VIDEO_STREAM], ([AUDIO_STREAM] if self._has_microphone else [])
            if solicit:
                status = await self._tester.solicit_offer(session, video, audio)
            else:
                status = await self._tester.provide_offer(session, video, audio)
            if status == proto.SUCCESS:
                return True, proto.status_name(status)
            return False, f"expected SUCCESS, got {proto.status_name(status)}"
        return TestCase(name, run)

    def _close_session(self, name: str, key: str) -> TestCase:
        async def run():
            session = self._sessions.pop(key, None)
            if session is None:
                return False, "no session"
            status = await self._tester.end_session(session)
            if status == proto.SUCCESS:
                return True, proto.status_name(status)
            return False, f"expected SUCCESS, got {proto.status_name(status)}"
        return TestCase(name, run)

    def _apply_answer_step(self, name: str, key: str) -> TestCase:
        async def run():
            session = self._sessions.get(key)
            if session is None:
                return False, "no session"
            try:
                await asyncio.wait_for(session.answer_received.wait(), SIGNALING_TIMEOUT)
            except asyncio.TimeoutError:
                return False, f"no answer within {SIGNALING_TIMEOUT:.0f} s"
            return True, f"{len(session.answer_sdp)} bytes SDP"
        return TestCase(name, run)

    def _provide_answer_step(self, name: str, key: str) -> TestCase:
        async def step(session: RequestorSession):
            if not session.offer_received.is_set():
                return None
            return await self._tester.provide_answer(session)
        return self._session_step(name, key, step)

    def _unknown_session(self, name: str) -> TestCase:
        async def run():
            status, _ = await self._tester.command(name, proto.WEBRTC_PROVIDER, proto.PROVIDE_ANSWER,
                                                   proto.provide_answer(0xFFF0, "v=0\r\n"))
            if status == proto.NOT_FOUND:
                return True, proto.status_name(status)
            return False, f"expected NOT_FOUND, got {proto.status_name(status)}"
        return TestCase(name, run)

    def _session_without_stream(self, name: str) -> TestCase:
        async def run():
            session = self._tester.new_session()
            status = await self._tester.solicit_offer(session, [9], [])
            await self._tester.close_session(session)
            if status == proto.NOT_FOUND:
                return True, proto.status_name(status)
            return False, f"expected NOT_FOUND, got {proto.status_name(status)}"
        return TestCase(name, run)

    # Test list

    def _cases(self) -> List[TestCase]:
        audio_expected = proto.SUCCESS if self._has_microphone else proto.RESOURCE_EXHAUSTED
        sensor = (0, 0, 1920, 1080)
        return [
            # Camera AV Stream Management
            self._avsm("SetStreamPriorities", proto.SET_STREAM_PRIORITIES,
                       proto.set_stream_priorities([proto.STREAM_USAGE_LIVE_VIEW, proto.STREAM_USAGE_RECORDING]), proto.SUCCESS),
            self._avsm("VideoStreamAllocate HEVC (unsupported codec)", proto.VIDEO_STREAM_ALLOCATE,
                       proto.video_stream(5, codec=proto.VIDEO_CODEC_HEVC), proto.CONSTRAINT_ERROR),
            self._avsm("VideoStreamAllocate 1080p only (above encoder limit)", proto.VIDEO_STREAM_ALLOCATE,
                       proto.video_stream(5, min_resolution=(1920, 1080), max_resolution=(1920, 1080)), proto.RESOURCE_EXHAUSTED),
            self._avsm("VideoStreamAllocate", proto.VIDEO_STREAM_ALLOCATE, proto.video_stream(VIDEO_STREAM), proto.SUCCESS),
            self._avsm("VideoStreamAllocate second stream (encoder busy)", proto.VIDEO_STREAM_ALLOCATE,
                       proto.video_stream(1), proto.RESOURCE_EXHAUSTED),
            self._avsm("VideoStreamModify", proto.VIDEO_STREAM_MODIFY, proto.stream_request(VIDEO_STREAM), proto.SUCCESS),
            self._avsm("VideoStreamModify watermark (unsupported)", proto.VIDEO_STREAM_MODIFY,
                       proto.stream_request(VIDEO_STREAM, watermark=True), proto.CONSTRAINT_ERROR),
            self._avsm(f"AudioStreamAllocate ({'microphone' if self._has_microphone else 'no microphone'})",
                       proto.AUDIO_STREAM_ALLOCATE, proto.audio_stream(AUDIO_STREAM), audio_expected),
            self._avsm("SnapshotStreamAllocate", proto.SNAPSHOT_STREAM_ALLOCATE, proto.snapshot_stream(SNAPSHOT_STREAM),
                       proto.SUCCESS),
            self._avsm("SnapshotStreamAllocate HEIC (unsupported codec)", proto.SNAPSHOT_STREAM_ALLOCATE,
                       proto.snapshot_stream(1, codec=proto.IMAGE_CODEC_HEIC), proto.CONSTRAINT_ERROR),
            self._avsm("CaptureSnapshot (not implemented)", proto.CAPTURE_SNAPSHOT, proto.capture_snapshot(None),
                       proto.UNSUPPORTED_COMMAND),
            # Camera AV Settings User Level Management
            self._settings("DPTZSetViewport", proto.DPTZ_SET_VIEWPORT, proto.dptz(VIDEO_STREAM, (0, 0, 1280, 720)),
                           proto.SUCCESS),
            self._settings("DPTZRelativeMove", proto.DPTZ_RELATIVE_MOVE, proto.dptz(VIDEO_STREAM, (320, 180, 1600, 900)),
                           proto.SUCCESS),
            self._settings("DPTZSetViewport unknown stream", proto.DPTZ_SET_VIEWPORT, proto.dptz(7, sensor), proto.NOT_FOUND),
            self._settings("DPTZSetViewport full sensor", proto.DPTZ_SET_VIEWPORT, proto.dptz(VIDEO_STREAM, sensor),
                           proto.SUCCESS),
            self._settings("MPTZSetPosition (no mechanical PTZ)", proto.MPTZ_SET_POSITION, proto.mptz(10, 10, 5),
                           proto.UNSUPPORTED_COMMAND),
            self._settings("MPTZRelativeMove (no mechanical PTZ)", proto.MPTZ_RELATIVE_MOVE, proto.mptz(15, 10, 5),
                           proto.UNSUPPORTED_COMMAND),
            self._settings("MPTZMoveToPreset (no mechanical PTZ)", proto.MPTZ_MOVE_TO_PRESET, proto.preset_request(1),
                           proto.UNSUPPORTED_COMMAND),
            self._settings("MPTZSavePreset (no mechanical PTZ)", proto.MPTZ_SAVE_PRESET, proto.preset_request(1),
                           proto.UNSUPPORTED_COMMAND),
            self._settings("MPTZRemovePreset (no mechanical PTZ)", proto.MPTZ_REMOVE_PRESET, proto.preset_request(1),
                           proto.UNSUPPORTED_COMMAND),
            # WebRTC, offer from the camera
            self._open_session("SolicitOffer", SOLICIT_SESSION, solicit=True),
            self._wait("Offer received from camera_host", SOLICIT_SESSION, "offer_received", SIGNALING_TIMEOUT),
            self._provide_answer_step("ProvideAnswer", SOLICIT_SESSION),
            self._session_step("ProvideICECandidates", SOLICIT_SESSION, self._tester.provide_candidates),
            self._wait("ICE candidates received from camera_host", SOLICIT_SESSION, "candidates_received",
                       SIGNALING_TIMEOUT),
            self._wait("Video received (camera offer)", SOLICIT_SESSION, "first_video_frame", MEDIA_TIMEOUT),
            self._close_session("EndSession", SOLICIT_SESSION),
            # WebRTC, offer from the requestor
            self._open_session("ProvideOffer", PROVIDE_SESSION, solicit=False),
            self._apply_answer_step("Answer received from camera_host", PROVIDE_SESSION),
            self._wait("Video received (requestor offer)", PROVIDE_SESSION, "first_video_frame", MEDIA_TIMEOUT),
            self._close_session("EndSession (requestor offer)", PROVIDE_SESSION),
            self._unknown_session("ProvideAnswer unknown session"),
            self._session_without_stream("SolicitOffer unallocated stream"),
            # Release
            self._avsm("VideoStreamDeallocate", proto.VIDEO_STREAM_DEALLOCATE, proto.stream_request(VIDEO_STREAM),
                       proto.SUCCESS),
            self._avsm("AudioStreamDeallocate", proto.AUDIO_STREAM_DEALLOCATE, proto.stream_request(AUDIO_STREAM),
                       proto.SUCCESS),
            self._avsm("SnapshotStreamDeallocate", proto.SNAPSHOT_STREAM_DEALLOCATE, proto.stream_request(SNAPSHOT_STREAM),
                       proto.SUCCESS),
        ]
