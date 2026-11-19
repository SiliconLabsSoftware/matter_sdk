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

"""Minimal Matter TLV codec: anonymous and context-specific tags, enough for the camera payloads."""

import struct
from dataclasses import dataclass, field
from typing import List, Optional, Union

# Element types (lower 5 bits of the control byte).
_SIGNED_INT8 = 0x00
_UNSIGNED_INT8 = 0x04
_BOOL_FALSE = 0x08
_BOOL_TRUE = 0x09
_FLOAT32 = 0x0A
_FLOAT64 = 0x0B
_UTF8_STRING1 = 0x0C
_BYTE_STRING1 = 0x10
_NULL = 0x14
_STRUCTURE = 0x15
_ARRAY = 0x16
_LIST = 0x17
_END_OF_CONTAINER = 0x18

# Tag controls (upper 3 bits of the control byte).
_TAG_ANONYMOUS = 0x00
_TAG_CONTEXT = 0x20

_TAG_SIZES = {0x00: 0, 0x20: 1, 0x40: 2, 0x60: 4, 0x80: 2, 0xA0: 4, 0xC0: 6, 0xE0: 8}


@dataclass
class Element:
    """A decoded element. tag is the context tag number, None when anonymous (or a profile tag)."""

    kind: str  # "int", "uint", "bool", "float", "str", "bytes", "null", "struct", "array", "list"
    tag: Optional[int] = None
    value: Union[int, bool, float, str, bytes, None] = None
    children: List["Element"] = field(default_factory=list)

    def field(self, tag: int) -> Optional["Element"]:
        for child in self.children:
            if child.tag == tag:
                return child
        return None

    def get(self, tag: int, default=None):
        """Value of a structure field, default when absent or null."""
        child = self.field(tag)
        if child is None or child.kind == "null":
            return default
        if child.kind in ("struct", "array", "list"):
            return child
        return child.value


class TlvError(ValueError):
    pass


def decode(data: bytes) -> List[Element]:
    """Decodes a sequence of top-level elements."""
    elements, offset = _decode_elements(data, 0, False, 0)
    if offset != len(data):
        raise TlvError("trailing data")
    return elements


def _read(data: bytes, offset: int, size: int) -> int:
    if offset + size > len(data):
        raise TlvError("truncated element")
    return int.from_bytes(data[offset:offset + size], "little")


def _decode_elements(data: bytes, offset: int, in_container: bool, depth: int):
    if depth > 16:
        raise TlvError("nesting too deep")
    elements = []
    while offset < len(data):
        control = data[offset]
        offset += 1
        element_type = control & 0x1F
        if element_type == _END_OF_CONTAINER:
            if not in_container:
                raise TlvError("unexpected end of container")
            return elements, offset

        tag_control = control & 0xE0
        tag_size = _TAG_SIZES[tag_control]
        tag = _read(data, offset, tag_size) if tag_control == _TAG_CONTEXT else None
        offset += tag_size

        if element_type <= 0x03:
            size = 1 << element_type
            raw = _read(data, offset, size)
            offset += size
            sign_bit = 1 << (8 * size - 1)
            element = Element("int", tag, (raw ^ sign_bit) - sign_bit)
        elif element_type <= 0x07:
            size = 1 << (element_type - _UNSIGNED_INT8)
            element = Element("uint", tag, _read(data, offset, size))
            offset += size
        elif element_type in (_BOOL_FALSE, _BOOL_TRUE):
            element = Element("bool", tag, element_type == _BOOL_TRUE)
        elif element_type in (_FLOAT32, _FLOAT64):
            size = 4 if element_type == _FLOAT32 else 8
            if offset + size > len(data):
                raise TlvError("truncated float")
            value = struct.unpack("<f" if size == 4 else "<d", data[offset:offset + size])[0]
            element = Element("float", tag, value)
            offset += size
        elif _UTF8_STRING1 <= element_type <= 0x13:
            utf8 = element_type < _BYTE_STRING1
            size = 1 << (element_type - (_UTF8_STRING1 if utf8 else _BYTE_STRING1))
            length = _read(data, offset, size)
            offset += size
            if offset + length > len(data):
                raise TlvError("truncated string")
            raw = data[offset:offset + length]
            offset += length
            element = Element("str", tag, raw.decode("utf-8", "replace")) if utf8 else Element("bytes", tag, bytes(raw))
        elif element_type == _NULL:
            element = Element("null", tag)
        elif element_type in (_STRUCTURE, _ARRAY, _LIST):
            kind = {_STRUCTURE: "struct", _ARRAY: "array", _LIST: "list"}[element_type]
            children, offset = _decode_elements(data, offset, True, depth + 1)
            element = Element(kind, tag, children=children)
        else:
            raise TlvError(f"unknown element type 0x{element_type:02x}")
        elements.append(element)

    if in_container:
        raise TlvError("missing end of container")
    return elements, offset


class Writer:
    """Writes elements. tag=None writes an anonymous tag, an int a context-specific tag."""

    def __init__(self):
        self._data = bytearray()

    def data(self) -> bytes:
        return bytes(self._data)

    def _control(self, tag: Optional[int], element_type: int):
        if tag is None:
            self._data.append(_TAG_ANONYMOUS | element_type)
        else:
            self._data.append(_TAG_CONTEXT | element_type)
            self._data.append(tag)

    def start_struct(self, tag: Optional[int] = None) -> "Writer":
        self._control(tag, _STRUCTURE)
        return self

    def start_array(self, tag: Optional[int] = None) -> "Writer":
        self._control(tag, _ARRAY)
        return self

    def end(self) -> "Writer":
        self._data.append(_END_OF_CONTAINER)
        return self

    def put_uint(self, tag: Optional[int], value: int) -> "Writer":
        for index, size in enumerate((1, 2, 4, 8)):
            if value < (1 << (8 * size)):
                self._control(tag, _UNSIGNED_INT8 + index)
                self._data += value.to_bytes(size, "little")
                return self
        raise TlvError("unsigned integer too large")

    def put_int(self, tag: Optional[int], value: int) -> "Writer":
        for index, size in enumerate((1, 2, 4, 8)):
            limit = 1 << (8 * size - 1)
            if -limit <= value < limit:
                self._control(tag, _SIGNED_INT8 + index)
                self._data += value.to_bytes(size, "little", signed=True)
                return self
        raise TlvError("signed integer too large")

    def put_bool(self, tag: Optional[int], value: bool) -> "Writer":
        self._control(tag, _BOOL_TRUE if value else _BOOL_FALSE)
        return self

    def put_null(self, tag: Optional[int]) -> "Writer":
        self._control(tag, _NULL)
        return self

    def put_str(self, tag: Optional[int], value: str) -> "Writer":
        return self._length_prefixed(tag, _UTF8_STRING1, value.encode("utf-8"))

    def put_bytes(self, tag: Optional[int], value: bytes) -> "Writer":
        return self._length_prefixed(tag, _BYTE_STRING1, bytes(value))

    def _length_prefixed(self, tag: Optional[int], base_type: int, raw: bytes) -> "Writer":
        for index, size in enumerate((1, 2, 4)):
            if len(raw) < (1 << (8 * size)):
                self._control(tag, base_type + index)
                self._data += len(raw).to_bytes(size, "little")
                self._data += raw
                return self
        raise TlvError("string too long")
