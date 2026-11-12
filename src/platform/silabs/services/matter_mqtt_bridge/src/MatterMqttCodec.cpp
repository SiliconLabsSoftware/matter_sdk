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

#include <cctype>
#include <cstdio>
#include <cstring>

// Minimal JSON codec for {"value":...} reports and {"args":{...}} command payloads.

namespace {

constexpr size_t kDecodeScratchLen = MATTER_MQTT_MAX_PAYLOAD_LEN;

struct DecodeScratch
{
    char buffer[kDecodeScratchLen];
    MatterMqttCodecTypes::NameValue argItems[MATTER_MQTT_MAX_ARGS];
    char argNameStorage[MATTER_MQTT_MAX_ARGS][32];
    char argValueStorage[MATTER_MQTT_MAX_ARGS][32];
};

DecodeScratch sDecodeScratch;

const char * SkipWhitespace(const char * p, const char * end)
{
    while (p < end && isspace(static_cast<unsigned char>(*p)))
    {
        ++p;
    }
    return p;
}

bool ParseQuotedString(const char *& p, const char * end, char * out, size_t outLen)
{
    VerifyOrReturnError(outLen > 0, false);
    p = SkipWhitespace(p, end);
    VerifyOrReturnError(p < end && *p == '"', false);
    ++p;

    // Copy until closing quote; supports minimal backslash escaping.
    size_t o = 0;
    while (p < end && *p != '"')
    {
        if (*p == '\\' && (p + 1) < end)
        {
            ++p;
        }
        if (o + 1 < outLen)
        {
            out[o++] = *p;
        }
        ++p;
    }
    VerifyOrReturnError(p < end && *p == '"', false);
    ++p;
    out[o] = '\0';
    return true;
}

// Extract key/value pairs from top-level {"args":{...}}; missing "args" yields empty ArgsView (valid for no-arg commands).
bool ParseArgsObject(const char * json, size_t len, MatterMqttCodecTypes::ArgsView & args)
{
    args.count = 0;
    const char * argsKey = "\"args\"";
    const char * end     = json + len;

    // Linear scan for the "args" property name (payloads are small; no full JSON parser).
    const char * pos = static_cast<const char *>(memchr(json, '"', len));
    while (pos != nullptr && pos < end)
    {
        if (static_cast<size_t>(end - pos) >= strlen(argsKey) && memcmp(pos, argsKey, strlen(argsKey)) == 0)
        {
            // Opening brace of the args object: {"args": { HERE ... }}
            pos = static_cast<const char *>(memchr(pos, '{', static_cast<size_t>(end - pos)));
            VerifyOrReturnError(pos != nullptr, true);
            const char * cursor = pos + 1;

            // Each entry: "name" : "value" or "name" : number
            while (cursor < end && args.count < MATTER_MQTT_MAX_ARGS)
            {
                cursor = SkipWhitespace(cursor, end);
                if (cursor >= end || *cursor == '}')
                {
                    break;
                }

                if (!ParseQuotedString(cursor, end, sDecodeScratch.argNameStorage[args.count],
                                       sizeof(sDecodeScratch.argNameStorage[0])))
                {
                    break;
                }
                cursor = SkipWhitespace(cursor, end);
                VerifyOrReturnError(cursor < end && *cursor == ':', true);
                ++cursor;

                // Value: quoted string, or bare integer/decimal run.
                if (!ParseQuotedString(cursor, end, sDecodeScratch.argValueStorage[args.count],
                                       sizeof(sDecodeScratch.argValueStorage[0])))
                {
                    char numBuf[32];
                    cursor = SkipWhitespace(cursor, end);
                    size_t i = 0;
                    while (cursor + i < end &&
                           (isdigit(static_cast<unsigned char>(cursor[i])) || cursor[i] == '-' || cursor[i] == '.') &&
                           i + 1 < sizeof(numBuf))
                    {
                        numBuf[i] = cursor[i];
                        ++i;
                    }
                    numBuf[i] = '\0';
                    strncpy(sDecodeScratch.argValueStorage[args.count], numBuf, sizeof(sDecodeScratch.argValueStorage[0]) - 1);
                }

                sDecodeScratch.argItems[args.count].name  = sDecodeScratch.argNameStorage[args.count];
                sDecodeScratch.argItems[args.count].value = sDecodeScratch.argValueStorage[args.count];
                ++args.count;

                cursor = SkipWhitespace(cursor, end);
                if (cursor < end && *cursor == ',')
                {
                    ++cursor;
                }
            }
            return true;
        }
        pos = static_cast<const char *>(memchr(pos + 1, '"', static_cast<size_t>(end - pos - 1)));
    }

    // {} or missing "args" is valid for commands with no parameters.
    return true;
}

bool AppendChecked(int written, size_t outLen)
{
    return written >= 0 && static_cast<size_t>(written) < outLen;
}

} // namespace

// Attribute report body: {"value":<number>} or {"value":"<string>"}.
CHIP_ERROR MatterMqttJsonCodec::EncodeReport(bool asNumber, chip::CharSpan valueText, char * outJson, size_t outLen)
{
    VerifyOrReturnError(!valueText.empty() && outJson != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    int n;
    if (asNumber)
    {
        n = snprintf(outJson, outLen, R"({"value":%.*s})", static_cast<int>(valueText.size()), valueText.data());
    }
    else
    {
        n = snprintf(outJson, outLen, R"({"value":"%.*s"})", static_cast<int>(valueText.size()), valueText.data());
    }
    VerifyOrReturnError(AppendChecked(n, outLen), CHIP_ERROR_BUFFER_TOO_SMALL);
    return CHIP_NO_ERROR;
}

CHIP_ERROR MatterMqttJsonCodec::DecodeArgs(chip::ByteSpan json, MatterMqttCodecTypes::ArgsView & out)
{
    VerifyOrReturnError(!json.empty() && json.size() <= MATTER_MQTT_MAX_PAYLOAD_LEN, CHIP_ERROR_MESSAGE_TOO_LONG);

    // NUL-terminate once for the hand-rolled parser; pointers in ArgsView point at static decode scratch.
    memcpy(sDecodeScratch.buffer, json.data(), json.size());
    sDecodeScratch.buffer[json.size()] = '\0';
    out.count                          = 0;
    if (!ParseArgsObject(sDecodeScratch.buffer, json.size(), out))
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    return CHIP_NO_ERROR;
}
