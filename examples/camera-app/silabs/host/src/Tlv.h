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

/**
 * Minimal Matter TLV codec: parses a buffer into a tree of elements and writes elements with
 * anonymous or context-specific tags. Enough for the cluster payloads exchanged over MMIC.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tlv {

struct Tag
{
    enum class Kind : uint8_t
    {
        kAnonymous,
        kContext,
        kProfile,
    };

    Kind kind       = Kind::kAnonymous;
    uint32_t number = 0;

    static Tag Anonymous() { return Tag(); }
    static Tag Context(uint8_t value) { return Tag{ Kind::kContext, value }; }
    bool IsContext(uint8_t value) const { return kind == Kind::kContext && number == value; }
};

inline Tag ContextTag(uint8_t value)
{
    return Tag::Context(value);
}

enum class Type : uint8_t
{
    kSignedInt,
    kUnsignedInt,
    kBool,
    kFloat,
    kUtf8String,
    kByteString,
    kNull,
    kStructure,
    kArray,
    kList,
};

class Element
{
public:
    Type type = Type::kNull;
    Tag tag;
    int64_t signedValue    = 0;
    uint64_t unsignedValue = 0;
    bool boolValue         = false;
    double floatValue      = 0;
    std::string bytes; // UTF-8 and octet strings
    std::vector<Element> children;

    bool IsNull() const { return type == Type::kNull; }
    bool IsContainer() const { return type == Type::kStructure || type == Type::kArray || type == Type::kList; }

    std::optional<uint64_t> AsUInt() const;
    std::optional<int64_t> AsInt() const;
    std::optional<bool> AsBool() const;
    std::optional<std::string> AsString() const;

    // Structure fields, looked up by context tag. Empty when absent or of another type.
    const Element * Field(uint8_t contextTag) const;
    std::optional<uint64_t> UInt(uint8_t contextTag) const;
    std::optional<int64_t> Int(uint8_t contextTag) const;
    std::optional<bool> Bool(uint8_t contextTag) const;
    std::optional<std::string> String(uint8_t contextTag) const;
};

// Parses a sequence of top-level elements. Returns false on malformed input.
bool Parse(const uint8_t * data, size_t length, std::vector<Element> & elements);

class Writer
{
public:
    Writer & StartStructure(Tag tag = Tag::Anonymous());
    Writer & StartArray(Tag tag);
    Writer & EndContainer();
    Writer & PutUInt(Tag tag, uint64_t value);
    Writer & PutInt(Tag tag, int64_t value);
    Writer & PutBool(Tag tag, bool value);
    Writer & PutNull(Tag tag);
    Writer & PutString(Tag tag, const std::string & value);
    Writer & PutBytes(Tag tag, const uint8_t * data, size_t length);

    const std::vector<uint8_t> & Data() const { return mData; }

private:
    void PutControl(Tag tag, uint8_t elementType);
    void PutLittleEndian(uint64_t value, size_t size);
    void PutLengthPrefixed(Tag tag, uint8_t baseType, const uint8_t * data, size_t length);

    std::vector<uint8_t> mData;
};

} // namespace tlv
