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

#include "MatterMqttCodec.h"

#include <lib/support/CodeUtils.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr char kArgsPrefix[] = "{\"args\":{";

struct DecodeScratch
{
    char argNameStorage[MATTER_MQTT_MAX_ARGS][MATTER_MQTT_MAX_ARG_NAME_LEN];
    char argValueStorage[MATTER_MQTT_MAX_ARGS][MATTER_MQTT_MAX_ARG_VALUE_LEN];
};

DecodeScratch sDecodeScratch;

bool ExpectChar(const char *& cursor, const char * end, char expected)
{
    VerifyOrReturnError(cursor < end && *cursor == expected, false);
    ++cursor;
    return true;
}

// Strict JSON string: no escapes; content length limited by @p maxContentLen (excluding NUL).
bool ParseStrictQuotedString(const char *& cursor, const char * end, char * out, size_t outLen, size_t maxContentLen)
{
    VerifyOrReturnError(outLen > 0 && outLen > maxContentLen, false);
    VerifyOrReturnError(ExpectChar(cursor, end, '"'), false);

    size_t written = 0;
    while (cursor < end && *cursor != '"')
    {
        VerifyOrReturnError(*cursor != '\\', false);
        VerifyOrReturnError(written < maxContentLen, false);
        out[written++] = *cursor;
        ++cursor;
    }
    VerifyOrReturnError(cursor < end && *cursor == '"', false);
    ++cursor;
    out[written] = '\0';
    return true;
}

bool ParseArgsPairs(const char *& cursor, const char * end, MatterMqttCodecTypes::ArgsView & args)
{
    args.count = 0;
    while (true)
    {
        if (cursor >= end)
        {
            return false;
        }
        if (*cursor == '}')
        {
            break;
        }

        VerifyOrReturnError(args.count < MATTER_MQTT_MAX_ARGS, false);
        VerifyOrReturnError(
            ParseStrictQuotedString(cursor, end, sDecodeScratch.argNameStorage[args.count],
                                    sizeof(sDecodeScratch.argNameStorage[0]), MATTER_MQTT_MAX_ARG_NAME_LEN - 1),
            false);
        VerifyOrReturnError(ExpectChar(cursor, end, ':'), false);
        VerifyOrReturnError(
            ParseStrictQuotedString(cursor, end, sDecodeScratch.argValueStorage[args.count],
                                    sizeof(sDecodeScratch.argValueStorage[0]), MATTER_MQTT_MAX_ARG_VALUE_LEN - 1),
            false);

        args.items[args.count].name  = sDecodeScratch.argNameStorage[args.count];
        args.items[args.count].value = sDecodeScratch.argValueStorage[args.count];
        ++args.count;

        if (cursor < end && *cursor == ',')
        {
            ++cursor;
            continue;
        }
        if (cursor < end && *cursor == '}')
        {
            break;
        }
        return false;
    }
    return true;
}

// Cursor parser over MQTT payload bytes; rejects whitespace and any grammar deviation.
bool ParseArgsObject(const char * json, size_t len, MatterMqttCodecTypes::ArgsView & args)
{
    args.count = 0;
    if (len == 2 && json[0] == '{' && json[1] == '}')
    {
        return true;
    }

    VerifyOrReturnError(len >= sizeof(kArgsPrefix) - 1, false);
    VerifyOrReturnError(memcmp(json, kArgsPrefix, sizeof(kArgsPrefix) - 1) == 0, false);

    const char * cursor = json + sizeof(kArgsPrefix) - 1;
    const char * end    = json + len;

    VerifyOrReturnError(ParseArgsPairs(cursor, end, args), false);
    VerifyOrReturnError(ExpectChar(cursor, end, '}'), false); // close args object
    VerifyOrReturnError(ExpectChar(cursor, end, '}'), false); // close root object
    VerifyOrReturnError(cursor == end, false);
    return true;
}

} // namespace

// Attribute report body: {"value":"<string>"}.
CHIP_ERROR MatterMqttJsonCodec::EncodeReport(chip::CharSpan valueText, char * outJson, size_t outLen)
{
    VerifyOrReturnError(!valueText.empty() && outJson != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    const int n =
        snprintf(outJson, outLen, R"({"value":"%.*s"})", static_cast<int>(valueText.size()), valueText.data());
    VerifyOrReturnError(n >= 0 && static_cast<size_t>(n) < outLen, CHIP_ERROR_BUFFER_TOO_SMALL);
    return CHIP_NO_ERROR;
}

CHIP_ERROR MatterMqttJsonCodec::DecodeArgs(chip::ByteSpan json, MatterMqttCodecTypes::ArgsView & out)
{
    VerifyOrReturnError(!json.empty() && json.size() < MATTER_MQTT_MAX_PAYLOAD_LEN, CHIP_ERROR_MESSAGE_TOO_LONG);

    out.count = 0;
    if (!ParseArgsObject(reinterpret_cast<const char *>(json.data()), json.size(), out))
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    return CHIP_NO_ERROR;
}
