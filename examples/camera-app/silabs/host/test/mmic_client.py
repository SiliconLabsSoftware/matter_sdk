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

"""MMIC protocol client playing the SoC role, over TCP (socat bridge to the camera_host UART).

Frame layout (examples/platform/silabs/mmic/mmic.h), little-endian:
  [0] header (0xF1 request, 0xF2 answer) [1..2] frame length [3] op code [4..N-3] payload
  [N-2..N-1] CRC-16/CCITT-FALSE over bytes [0..N-3]
"""

import asyncio
import os
import re
import struct
from typing import Awaitable, Callable, Dict, Optional, Tuple

HEADER_REQUEST = 0xF1
HEADER_ANSWER = 0xF2
PACKET_OVERHEAD = 6
MAX_FRAME_SIZE = 4096
ANSWER_EXPECTED = 0x01

CLUSTER_COMMAND_HEADER = struct.Struct("<HBHII")  # transactionId, flags, endpointId, clusterId, commandId
CLUSTER_ANSWER_HEADER = struct.Struct("<HB")  # transactionId, status
ATTRIBUTE_CHANGED_HEADER = struct.Struct("<HII")  # endpointId, clusterId, attributeId

_MMIC_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "platform", "silabs",
                            "mmic", "mmic.h")


def _load_op_codes() -> Dict[str, int]:
    """Op codes are the positions in the COMMAND_LIST of mmic.h."""
    try:
        with open(_MMIC_HEADER, encoding="utf-8") as header:
            text = header.read()
        command_list = text[text.index("#define COMMAND_LIST"):]
        command_list = command_list[:command_list.index("typedef enum mmic_command_id")]
        names = re.findall(r"X\((\w+)\s*,", command_list)
        return {name: index for index, name in enumerate(names)}
    except (OSError, ValueError):
        return {}


_OP_CODES = _load_op_codes()
OP_CLUSTER_COMMAND = _OP_CODES.get("cluster_command", 11)
OP_ATTRIBUTE_CHANGED = _OP_CODES.get("attribute_changed", 12)


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(header: int, op_code: int, payload: bytes) -> bytes:
    length = PACKET_OVERHEAD + len(payload)
    frame = bytes([header]) + struct.pack("<H", length) + bytes([op_code]) + payload
    return frame + struct.pack("<H", crc16(frame))


# Handler of the cluster commands sent by the host: (endpoint, cluster, command, TLV params) -> status.
RequestHandler = Callable[[int, int, int, bytes], Awaitable[int]]


class MmicClient:
    def __init__(self, request_handler: RequestHandler, log: Callable[[str], None]):
        self._request_handler = request_handler
        self._log = log
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._read_task: Optional[asyncio.Task] = None
        self._pending: Dict[int, asyncio.Future] = {}
        self._next_transaction = 0
        self._buffer = bytearray()

    @property
    def connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    async def connect(self, host: str, port: int, timeout: float = 5.0):
        self._reader, self._writer = await asyncio.wait_for(asyncio.open_connection(host, port), timeout)
        self._buffer.clear()
        self._read_task = asyncio.create_task(self._read_loop())
        self._log(f"Connected to {host}:{port}")

    async def close(self):
        if self._read_task:
            self._read_task.cancel()
            self._read_task = None
        if self._writer:
            self._writer.close()
            try:
                await self._writer.wait_closed()
            except (ConnectionError, OSError):
                pass
            self._writer = None
        for future in self._pending.values():
            if not future.done():
                future.set_result((None, b""))
        self._pending.clear()

    async def send_command(self, endpoint: int, cluster: int, command: int, params: bytes,
                           timeout: float = 3.0) -> Tuple[Optional[int], bytes]:
        """Sends a cluster_command and waits for the host answer: (status or None on timeout, response TLV)."""
        if not self.connected:
            raise ConnectionError("not connected")
        transaction = self._next_transaction
        self._next_transaction = (self._next_transaction + 1) & 0xFFFF
        future = asyncio.get_running_loop().create_future()
        self._pending[transaction] = future

        header = CLUSTER_COMMAND_HEADER.pack(transaction, ANSWER_EXPECTED, endpoint, cluster, command)
        await self._write(encode_frame(HEADER_REQUEST, OP_CLUSTER_COMMAND, header + params))
        try:
            return await asyncio.wait_for(future, timeout)
        except asyncio.TimeoutError:
            return None, b""
        finally:
            self._pending.pop(transaction, None)

    async def send_attribute_changed(self, endpoint: int, cluster: int, attribute: int, value: bytes):
        """attribute_changed notification, not answered by the host."""
        header = ATTRIBUTE_CHANGED_HEADER.pack(endpoint, cluster, attribute)
        await self._write(encode_frame(HEADER_REQUEST, OP_ATTRIBUTE_CHANGED, header + value))

    async def _write(self, frame: bytes):
        if not self.connected:
            raise ConnectionError("not connected")
        self._writer.write(frame)
        await self._writer.drain()

    async def _read_loop(self):
        try:
            while True:
                data = await self._reader.read(4096)
                if not data:
                    self._log("Connection closed by camera_host")
                    break
                self._buffer += data
                await self._process_buffer()
        except asyncio.CancelledError:
            raise
        except (ConnectionError, OSError) as error:
            self._log(f"Connection error: {error}")
        if self._writer:
            self._writer.close()

    async def _process_buffer(self):
        while len(self._buffer) >= PACKET_OVERHEAD:
            header = self._buffer[0]
            if header not in (HEADER_REQUEST, HEADER_ANSWER):
                del self._buffer[0]
                continue
            length = struct.unpack_from("<H", self._buffer, 1)[0]
            if length < PACKET_OVERHEAD or length > MAX_FRAME_SIZE:
                del self._buffer[0]
                continue
            if len(self._buffer) < length:
                return
            frame = bytes(self._buffer[:length])
            if struct.unpack_from("<H", frame, length - 2)[0] != crc16(frame[:-2]):
                # Corrupted or misaligned: resynchronize on the next byte.
                del self._buffer[0]
                continue
            del self._buffer[:length]
            await self._on_frame(header, frame[3], frame[4:-2])

    async def _on_frame(self, header: int, op_code: int, payload: bytes):
        if op_code != OP_CLUSTER_COMMAND:
            self._log(f"Ignored frame 0x{header:02x} op code {op_code}")
            return

        if header == HEADER_ANSWER:
            if len(payload) < CLUSTER_ANSWER_HEADER.size:
                return
            transaction, status = CLUSTER_ANSWER_HEADER.unpack_from(payload)
            future = self._pending.get(transaction)
            if future and not future.done():
                future.set_result((status, payload[CLUSTER_ANSWER_HEADER.size:]))
            return

        # Request of the host (WebRTC signaling): always answered.
        if len(payload) < CLUSTER_COMMAND_HEADER.size:
            return
        transaction, _, endpoint, cluster, command = CLUSTER_COMMAND_HEADER.unpack_from(payload)
        try:
            status = await self._request_handler(endpoint, cluster, command, payload[CLUSTER_COMMAND_HEADER.size:])
        except Exception as error:  # Keep the link up whatever the handler does.
            self._log(f"Request handler error: {error}")
            status = 0x01
        await self._write(encode_frame(HEADER_ANSWER, OP_CLUSTER_COMMAND, CLUSTER_ANSWER_HEADER.pack(transaction, status)))
