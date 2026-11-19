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

"""Plays the SoC role towards camera_host: sends the camera commands and relays the WebRTC signaling
of camera_host to local requestor sessions (aiortc) instead of a Matter controller."""

import asyncio
from typing import Callable, Dict, List, Optional, Tuple

import camera_protocol as proto
from matter_tlv import TlvError, decode
from mmic_client import MmicClient
from requestor import FrameCallback, RequestorSession

# Time camera_host is given to start the camera and establish the WebRTC connection.
SIGNALING_TIMEOUT = 10.0
MEDIA_TIMEOUT = 20.0


class CameraTester:
    def __init__(self, log: Callable[[str], None], on_video_frame: FrameCallback):
        self._log = log
        self._on_video_frame = on_video_frame
        self.client = MmicClient(self._handle_host_request, log)
        self.sessions: Dict[int, RequestorSession] = {}
        self._next_session_id = 1

    # Commands

    async def command(self, name: str, cluster: int, command: int, params: bytes,
                      timeout: float = 3.0) -> Tuple[Optional[int], bytes]:
        status, data = await self.client.send_command(proto.CAMERA_ENDPOINT, cluster, command, params, timeout)
        self._log(f"{name} -> {proto.status_name(status)}")
        return status, data

    async def notify_attribute(self, attribute: int, value: bytes):
        await self.client.send_attribute_changed(proto.CAMERA_ENDPOINT, proto.AV_STREAM_MANAGEMENT, attribute, value)

    # WebRTC sessions

    def new_session(self) -> RequestorSession:
        session = RequestorSession(self._next_session_id, self._on_video_frame, self._log)
        self.sessions[session.session_id] = session
        self._next_session_id = (self._next_session_id % 0xFFFF) + 1
        return session

    async def close_session(self, session: RequestorSession):
        self.sessions.pop(session.session_id, None)
        await session.close()

    async def close_all_sessions(self):
        for session in list(self.sessions.values()):
            await self.close_session(session)

    async def solicit_offer(self, session: RequestorSession, video: List[int], audio: List[int]) -> Optional[int]:
        status, _ = await self.command("SolicitOffer", proto.WEBRTC_PROVIDER, proto.SOLICIT_OFFER,
                                       proto.solicit_offer(session.session_id, video, audio))
        return status

    async def provide_answer(self, session: RequestorSession) -> Optional[int]:
        """Answers the offer received from camera_host."""
        sdp = await session.create_answer()
        status, _ = await self.command("ProvideAnswer", proto.WEBRTC_PROVIDER, proto.PROVIDE_ANSWER,
                                       proto.provide_answer(session.session_id, sdp))
        return status

    async def provide_offer(self, session: RequestorSession, video: List[int], audio: List[int]) -> Optional[int]:
        sdp = await session.create_offer(bool(video), bool(audio))
        status, _ = await self.command("ProvideOffer", proto.WEBRTC_PROVIDER, proto.PROVIDE_OFFER,
                                       proto.provide_offer(session.session_id, sdp, video, audio))
        return status

    async def provide_candidates(self, session: RequestorSession) -> Optional[int]:
        status, _ = await self.command("ProvideICECandidates", proto.WEBRTC_PROVIDER, proto.PROVIDE_ICE_CANDIDATES,
                                       proto.provide_ice_candidates(session.session_id, session.local_candidates()))
        return status

    async def end_session(self, session: RequestorSession) -> Optional[int]:
        status, _ = await self.command("EndSession", proto.WEBRTC_PROVIDER, proto.END_SESSION,
                                       proto.end_session(session.session_id))
        await self.close_session(session)
        return status

    async def start_live_view(self, video: List[int], audio: List[int]) -> Tuple[Optional[RequestorSession], str]:
        """Full SolicitOffer flow. Returns the session and an error message (empty on success)."""
        session = self.new_session()
        status = await self.solicit_offer(session, video, audio)
        if status != proto.SUCCESS:
            await self.close_session(session)
            return None, f"SolicitOffer: {proto.status_name(status)}"
        try:
            await asyncio.wait_for(session.offer_received.wait(), SIGNALING_TIMEOUT)
        except asyncio.TimeoutError:
            await self.end_session(session)
            return None, "No offer received from camera_host"
        status = await self.provide_answer(session)
        if status != proto.SUCCESS:
            await self.end_session(session)
            return None, f"ProvideAnswer: {proto.status_name(status)}"
        return session, ""

    # Requests of camera_host (WebRTCTransportRequestor commands relayed by the SoC)

    async def _handle_host_request(self, endpoint: int, cluster: int, command: int, params: bytes) -> int:
        if cluster != proto.WEBRTC_REQUESTOR:
            self._log(f"Unexpected request: cluster 0x{cluster:04x} command 0x{command:02x}")
            return proto.UNSUPPORTED_CLUSTER
        try:
            elements = decode(params)
        except TlvError as error:
            self._log(f"Malformed request from camera_host: {error}")
            return proto.INVALID_COMMAND
        if not elements or elements[0].kind != "struct":
            return proto.INVALID_COMMAND
        request = elements[0]
        session_id = request.get(0)
        session = self.sessions.get(session_id)
        if session is None:
            self._log(f"Request for unknown WebRTC session {session_id}")
            return proto.NOT_FOUND

        if command == proto.REQUESTOR_OFFER:
            self._log(f"Session {session_id}: offer received ({len(request.get(1, ''))} bytes)")
            session.on_offer(request.get(1, ""))
        elif command == proto.REQUESTOR_ANSWER:
            self._log(f"Session {session_id}: answer received ({len(request.get(1, ''))} bytes)")
            session.on_answer(request.get(1, ""))
            # Applied right away so that the candidates that follow can be added.
            asyncio.ensure_future(session.apply_answer())
        elif command == proto.REQUESTOR_ICE_CANDIDATES:
            candidates = []
            candidate_list = request.get(1)
            for item in (candidate_list.children if candidate_list else []):
                if item.get(0):
                    candidates.append((item.get(0), item.get(1, "")))
            self._log(f"Session {session_id}: {len(candidates)} ICE candidate(s) received")
            asyncio.ensure_future(session.on_remote_candidates(candidates))
        elif command == proto.REQUESTOR_END:
            self._log(f"Session {session_id}: ended by camera_host (reason {request.get(1)})")
            session.on_end(request.get(1, 0))
        else:
            return proto.UNSUPPORTED_COMMAND
        return proto.SUCCESS
