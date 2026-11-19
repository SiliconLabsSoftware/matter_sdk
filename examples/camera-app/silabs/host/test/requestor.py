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

"""WebRTC requestor (the Matter controller side of a camera session), based on aiortc."""

import asyncio
from typing import Callable, List, Optional, Tuple

from aiortc import RTCPeerConnection, RTCRtpReceiver, RTCSessionDescription
from aiortc.mediastreams import MediaStreamError
from aiortc.sdp import candidate_from_sdp

# Called with each decoded video frame, as an RGB numpy array (height, width, 3).
FrameCallback = Callable[[object], None]


class RequestorSession:
    def __init__(self, session_id: int, on_video_frame: FrameCallback, log: Callable[[str], None]):
        self.session_id = session_id
        self._on_video_frame = on_video_frame
        self._log = log
        self.pc = RTCPeerConnection()
        self.offer_sdp: Optional[str] = None
        self.answer_sdp: Optional[str] = None
        self.end_reason: Optional[int] = None
        self.offer_received = asyncio.Event()
        self.answer_received = asyncio.Event()
        self.candidates_received = asyncio.Event()
        self.first_video_frame = asyncio.Event()
        self.ended = asyncio.Event()
        self.video_frames = 0
        self.audio_frames = 0
        self._remote_description_set = False
        self._pending_candidates: List[Tuple[str, str]] = []
        self._tasks: List[asyncio.Task] = []

        @self.pc.on("track")
        def on_track(track):
            self._log(f"Session {session_id}: receiving {track.kind}")
            consumer = self._consume_video if track.kind == "video" else self._consume_audio
            self._tasks.append(asyncio.ensure_future(consumer(track)))

        @self.pc.on("connectionstatechange")
        async def on_state():
            self._log(f"Session {session_id}: WebRTC {self.pc.connectionState}")

    # SolicitOffer flow: the camera offers, the requestor answers.

    def on_offer(self, sdp: str):
        self.offer_sdp = sdp
        self.offer_received.set()

    async def create_answer(self) -> str:
        await self.pc.setRemoteDescription(RTCSessionDescription(self.offer_sdp, "offer"))
        await self._remote_description_ready()
        await self.pc.setLocalDescription(await self.pc.createAnswer())
        return self.pc.localDescription.sdp

    # ProvideOffer flow: the requestor offers, the camera answers.

    async def create_offer(self, video: bool, audio: bool) -> str:
        if video:
            transceiver = self.pc.addTransceiver("video", direction="recvonly")
            # camera_host streams H.264 only.
            codecs = [codec for codec in RTCRtpReceiver.getCapabilities("video").codecs if codec.mimeType == "video/H264"]
            transceiver.setCodecPreferences(codecs)
        if audio:
            self.pc.addTransceiver("audio", direction="recvonly")
        await self.pc.setLocalDescription(await self.pc.createOffer())
        return self.pc.localDescription.sdp

    def on_answer(self, sdp: str):
        self.answer_sdp = sdp
        self.answer_received.set()

    async def apply_answer(self):
        await self.pc.setRemoteDescription(RTCSessionDescription(self.answer_sdp, "answer"))
        await self._remote_description_ready()

    # ICE candidates

    def local_candidates(self) -> List[Tuple[str, str]]:
        """(candidate, mid) pairs of the local description. aiortc gathers them before signaling."""
        candidates = []
        mid = ""
        for line in self.pc.localDescription.sdp.splitlines():
            if line.startswith("a=mid:"):
                mid = line[len("a=mid:"):]
            elif line.startswith("a=candidate:"):
                candidates.append((line[len("a="):], mid))
        return candidates

    async def on_remote_candidates(self, candidates: List[Tuple[str, str]]):
        self.candidates_received.set()
        if not self._remote_description_set:
            # Received before the offer or answer was applied.
            self._pending_candidates += candidates
            return
        await self._add_candidates(candidates)

    async def _remote_description_ready(self):
        self._remote_description_set = True
        pending, self._pending_candidates = self._pending_candidates, []
        await self._add_candidates(pending)

    async def _add_candidates(self, candidates: List[Tuple[str, str]]):
        for text, mid in candidates:
            try:
                candidate = candidate_from_sdp(text.split(":", 1)[1] if text.startswith("candidate:") else text)
                candidate.sdpMid = mid or None
                if candidate.sdpMid is None:
                    candidate.sdpMLineIndex = 0
                await self.pc.addIceCandidate(candidate)
            except Exception as error:
                self._log(f"Session {self.session_id}: ignored candidate '{text}': {error}")

    # End of session

    def on_end(self, reason: int):
        self.end_reason = reason
        self.ended.set()

    async def close(self):
        for task in self._tasks:
            task.cancel()
        self._tasks.clear()
        await self.pc.close()

    # Media

    async def _consume_video(self, track):
        while True:
            try:
                frame = await track.recv()
            except MediaStreamError:
                return
            self.video_frames += 1
            self.first_video_frame.set()
            self._on_video_frame(frame.to_ndarray(format="rgb24"))

    async def _consume_audio(self, track):
        while True:
            try:
                await track.recv()
            except MediaStreamError:
                return
            self.audio_frames += 1
