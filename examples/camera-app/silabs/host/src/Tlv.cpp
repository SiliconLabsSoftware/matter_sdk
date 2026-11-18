/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "Tlv.h"

#include <cstring>
#include <limits>

namespace tlv {

namespace {

// Element types (lower 5 bits of the control byte).
constexpr uint8_t kTypeSignedInt8     = 0x00;
constexpr uint8_t kTypeUnsignedInt8   = 0x04;
constexpr uint8_t kTypeBoolFalse      = 0x08;
constexpr uint8_t kTypeBoolTrue       = 0x09;
constexpr uint8_t kTypeFloat32        = 0x0A;
constexpr uint8_t kTypeFloat64        = 0x0B;
constexpr uint8_t kTypeUtf8String1    = 0x0C;
constexpr uint8_t kTypeByteString1    = 0x10;
constexpr uint8_t kTypeNull           = 0x14;
constexpr uint8_t kTypeStructure      = 0x15;
constexpr uint8_t kTypeArray          = 0x16;
constexpr uint8_t kTypeList           = 0x17;
constexpr uint8_t kTypeEndOfContainer = 0x18;

// Tag controls (upper 3 bits of the control byte).
constexpr uint8_t kTagAnonymous = 0x00;
constexpr uint8_t kTagContext   = 0x20;

constexpr int kMaxDepth = 16;

bool ReadLittleEndian(const uint8_t * data, size_t length, size_t & offset, size_t size, uint64_t & value)
{
    if (length - offset < size)
    {
        return false;
    }
    value = 0;
    for (size_t i = 0; i < size; i++)
    {
        value |= static_cast<uint64_t>(data[offset + i]) << (8 * i);
    }
    offset += size;
    return true;
}

bool ReadTag(uint8_t tagControl, const uint8_t * data, size_t length, size_t & offset, Tag & tag)
{
    uint64_t value = 0;
    switch (tagControl)
    {
    case 0x00:
        tag = Tag::Anonymous();
        return true;
    case 0x20:
        if (!ReadLittleEndian(data, length, offset, 1, value))
        {
            return false;
        }
        tag = Tag::Context(static_cast<uint8_t>(value));
        return true;
    case 0x40: // Common profile, 2 bytes
    case 0x80: // Implicit profile, 2 bytes
        tag.kind = Tag::Kind::kProfile;
        if (!ReadLittleEndian(data, length, offset, 2, value))
        {
            return false;
        }
        tag.number = static_cast<uint32_t>(value);
        return true;
    case 0x60: // Common profile, 4 bytes
    case 0xA0: // Implicit profile, 4 bytes
        tag.kind = Tag::Kind::kProfile;
        if (!ReadLittleEndian(data, length, offset, 4, value))
        {
            return false;
        }
        tag.number = static_cast<uint32_t>(value);
        return true;
    case 0xC0: // Fully qualified: vendor (2), profile (2), tag (2)
    case 0xE0: // Fully qualified: vendor (2), profile (2), tag (4)
        tag.kind = Tag::Kind::kProfile;
        if (!ReadLittleEndian(data, length, offset, 4, value) ||
            !ReadLittleEndian(data, length, offset, (tagControl == 0xC0) ? 2 : 4, value))
        {
            return false;
        }
        tag.number = static_cast<uint32_t>(value);
        return true;
    default:
        return false;
    }
}

bool ParseElements(const uint8_t * data, size_t length, size_t & offset, std::vector<Element> & out, bool inContainer, int depth)
{
    if (depth > kMaxDepth)
    {
        return false;
    }

    while (offset < length)
    {
        const uint8_t control = data[offset++];
        const uint8_t type    = control & 0x1F;
        if (type == kTypeEndOfContainer)
        {
            return inContainer;
        }

        Element element;
        if (!ReadTag(control & 0xE0, data, length, offset, element.tag))
        {
            return false;
        }

        uint64_t raw = 0;
        if (type <= 0x03)
        {
            const size_t size = size_t(1) << type;
            if (!ReadLittleEndian(data, length, offset, size, raw))
            {
                return false;
            }
            const unsigned shift = static_cast<unsigned>(64 - 8 * size);
            element.type         = Type::kSignedInt;
            element.signedValue  = static_cast<int64_t>(raw << shift) >> shift;
        }
        else if (type <= 0x07)
        {
            if (!ReadLittleEndian(data, length, offset, size_t(1) << (type - kTypeUnsignedInt8), raw))
            {
                return false;
            }
            element.type          = Type::kUnsignedInt;
            element.unsignedValue = raw;
        }
        else if (type == kTypeBoolFalse || type == kTypeBoolTrue)
        {
            element.type      = Type::kBool;
            element.boolValue = (type == kTypeBoolTrue);
        }
        else if (type == kTypeFloat32)
        {
            if (!ReadLittleEndian(data, length, offset, 4, raw))
            {
                return false;
            }
            const uint32_t bits = static_cast<uint32_t>(raw);
            float value;
            std::memcpy(&value, &bits, sizeof(value));
            element.type       = Type::kFloat;
            element.floatValue = value;
        }
        else if (type == kTypeFloat64)
        {
            if (!ReadLittleEndian(data, length, offset, 8, raw))
            {
                return false;
            }
            std::memcpy(&element.floatValue, &raw, sizeof(element.floatValue));
            element.type = Type::kFloat;
        }
        else if (type >= kTypeUtf8String1 && type <= 0x13)
        {
            const bool utf8   = type < kTypeByteString1;
            const size_t size = size_t(1) << ((type - (utf8 ? kTypeUtf8String1 : kTypeByteString1)));
            if (!ReadLittleEndian(data, length, offset, size, raw) || raw > length - offset)
            {
                return false;
            }
            element.type = utf8 ? Type::kUtf8String : Type::kByteString;
            element.bytes.assign(reinterpret_cast<const char *>(data + offset), static_cast<size_t>(raw));
            offset += static_cast<size_t>(raw);
        }
        else if (type == kTypeNull)
        {
            element.type = Type::kNull;
        }
        else if (type == kTypeStructure || type == kTypeArray || type == kTypeList)
        {
            element.type = (type == kTypeStructure) ? Type::kStructure : ((type == kTypeArray) ? Type::kArray : Type::kList);
            if (!ParseElements(data, length, offset, element.children, true, depth + 1))
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        out.push_back(std::move(element));
    }

    // A container must be closed by an end-of-container element.
    return !inContainer;
}

} // namespace

std::optional<uint64_t> Element::AsUInt() const
{
    if (type == Type::kUnsignedInt)
    {
        return unsignedValue;
    }
    if (type == Type::kSignedInt && signedValue >= 0)
    {
        return static_cast<uint64_t>(signedValue);
    }
    return std::nullopt;
}

std::optional<int64_t> Element::AsInt() const
{
    if (type == Type::kSignedInt)
    {
        return signedValue;
    }
    if (type == Type::kUnsignedInt && unsignedValue <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        return static_cast<int64_t>(unsignedValue);
    }
    return std::nullopt;
}

std::optional<bool> Element::AsBool() const
{
    if (type == Type::kBool)
    {
        return boolValue;
    }
    return std::nullopt;
}

std::optional<std::string> Element::AsString() const
{
    if (type == Type::kUtf8String || type == Type::kByteString)
    {
        return bytes;
    }
    return std::nullopt;
}

const Element * Element::Field(uint8_t contextTag) const
{
    for (const auto & child : children)
    {
        if (child.tag.IsContext(contextTag))
        {
            return &child;
        }
    }
    return nullptr;
}

std::optional<uint64_t> Element::UInt(uint8_t contextTag) const
{
    const Element * field = Field(contextTag);
    return field ? field->AsUInt() : std::nullopt;
}

std::optional<int64_t> Element::Int(uint8_t contextTag) const
{
    const Element * field = Field(contextTag);
    return field ? field->AsInt() : std::nullopt;
}

std::optional<bool> Element::Bool(uint8_t contextTag) const
{
    const Element * field = Field(contextTag);
    return field ? field->AsBool() : std::nullopt;
}

std::optional<std::string> Element::String(uint8_t contextTag) const
{
    const Element * field = Field(contextTag);
    return field ? field->AsString() : std::nullopt;
}

bool Parse(const uint8_t * data, size_t length, std::vector<Element> & elements)
{
    elements.clear();
    if (length == 0)
    {
        return true;
    }
    if (data == nullptr)
    {
        return false;
    }
    size_t offset = 0;
    return ParseElements(data, length, offset, elements, false, 0);
}

void Writer::PutControl(Tag tag, uint8_t elementType)
{
    if (tag.kind == Tag::Kind::kContext)
    {
        mData.push_back(static_cast<uint8_t>(kTagContext | elementType));
        mData.push_back(static_cast<uint8_t>(tag.number));
    }
    else
    {
        mData.push_back(static_cast<uint8_t>(kTagAnonymous | elementType));
    }
}

void Writer::PutLittleEndian(uint64_t value, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        mData.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

void Writer::PutLengthPrefixed(Tag tag, uint8_t baseType, const uint8_t * data, size_t length)
{
    if (length <= 0xFF)
    {
        PutControl(tag, baseType);
        PutLittleEndian(length, 1);
    }
    else if (length <= 0xFFFF)
    {
        PutControl(tag, static_cast<uint8_t>(baseType + 1));
        PutLittleEndian(length, 2);
    }
    else
    {
        PutControl(tag, static_cast<uint8_t>(baseType + 2));
        PutLittleEndian(length, 4);
    }
    if (length > 0)
    {
        mData.insert(mData.end(), data, data + length);
    }
}

Writer & Writer::StartStructure(Tag tag)
{
    PutControl(tag, kTypeStructure);
    return *this;
}

Writer & Writer::StartArray(Tag tag)
{
    PutControl(tag, kTypeArray);
    return *this;
}

Writer & Writer::EndContainer()
{
    mData.push_back(kTypeEndOfContainer);
    return *this;
}

Writer & Writer::PutUInt(Tag tag, uint64_t value)
{
    size_t size = 8;
    if (value <= 0xFF)
    {
        size = 1;
    }
    else if (value <= 0xFFFF)
    {
        size = 2;
    }
    else if (value <= 0xFFFFFFFF)
    {
        size = 4;
    }
    const uint8_t type = static_cast<uint8_t>(kTypeUnsignedInt8 + ((size == 1) ? 0 : (size == 2) ? 1 : (size == 4) ? 2 : 3));
    PutControl(tag, type);
    PutLittleEndian(value, size);
    return *this;
}

Writer & Writer::PutInt(Tag tag, int64_t value)
{
    size_t size = 8;
    if (value >= INT8_MIN && value <= INT8_MAX)
    {
        size = 1;
    }
    else if (value >= INT16_MIN && value <= INT16_MAX)
    {
        size = 2;
    }
    else if (value >= INT32_MIN && value <= INT32_MAX)
    {
        size = 4;
    }
    const uint8_t type = static_cast<uint8_t>(kTypeSignedInt8 + ((size == 1) ? 0 : (size == 2) ? 1 : (size == 4) ? 2 : 3));
    PutControl(tag, type);
    PutLittleEndian(static_cast<uint64_t>(value), size);
    return *this;
}

Writer & Writer::PutBool(Tag tag, bool value)
{
    PutControl(tag, value ? kTypeBoolTrue : kTypeBoolFalse);
    return *this;
}

Writer & Writer::PutNull(Tag tag)
{
    PutControl(tag, kTypeNull);
    return *this;
}

Writer & Writer::PutString(Tag tag, const std::string & value)
{
    PutLengthPrefixed(tag, kTypeUtf8String1, reinterpret_cast<const uint8_t *>(value.data()), value.size());
    return *this;
}

Writer & Writer::PutBytes(Tag tag, const uint8_t * data, size_t length)
{
    PutLengthPrefixed(tag, kTypeByteString1, data, length);
    return *this;
}

} // namespace tlv
