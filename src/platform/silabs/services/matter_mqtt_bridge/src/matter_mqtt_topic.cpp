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

#include "matter_mqtt_topic.h"

#include <lib/support/CodeUtils.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr char kTopicPrefix[] = "matter/v1/";
constexpr char kHexUpper[]      = "0123456789ABCDEF";

/** Uppercase hex without 0x or leading zeros */
CHIP_ERROR FormatTopicHexU64(uint64_t value, char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr && outLen > 0, CHIP_ERROR_INVALID_ARGUMENT);

    if (value == 0)
    {
        VerifyOrReturnError(outLen >= 2, CHIP_ERROR_BUFFER_TOO_SMALL);
        out[0] = '0';
        out[1] = '\0';
        return CHIP_NO_ERROR;
    }

    char reversed[MATTER_MQTT_DEVICE_ID_LEN];
    size_t digitCount = 0;
    while (value > 0)
    {
        VerifyOrReturnError(digitCount + 1 < sizeof(reversed), CHIP_ERROR_BUFFER_TOO_SMALL);
        reversed[digitCount++] = kHexUpper[value & 0xFu];
        value >>= 4;
    }
    VerifyOrReturnError(digitCount + 1 <= outLen, CHIP_ERROR_BUFFER_TOO_SMALL);
    for (size_t i = 0; i < digitCount; ++i)
    {
        out[i] = reversed[digitCount - 1 - i];
    }
    out[digitCount] = '\0';
    return CHIP_NO_ERROR;
}

bool AppendChecked(int written, size_t outLen)
{
    return written >= 0 && static_cast<size_t>(written) < outLen;
}

const char * FindSegment(const char * start, const char * end, const char * label)
{
    const size_t labelLen = strlen(label);
    const char * pos      = start;
    while (pos < end)
    {
        if (static_cast<size_t>(end - pos) >= labelLen && memcmp(pos, label, labelLen) == 0)
        {
            return pos + labelLen;
        }
        ++pos;
    }
    return nullptr;
}

bool CopySegment(const char * start, const char * end, char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr && outLen > 0, false);
    size_t len = 0;
    while (start + len < end && start[len] != '/')
    {
        ++len;
    }
    VerifyOrReturnError(len + 1 <= outLen, false);
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

bool IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

CHIP_ERROR ParseTopicHexU64(const char * begin, const char * end, uint64_t & out)
{
    VerifyOrReturnError(begin != nullptr && begin < end, CHIP_ERROR_INVALID_ARGUMENT);

    for (const char * p = begin; p < end; ++p)
    {
        VerifyOrReturnError(IsHexDigit(*p), CHIP_ERROR_INVALID_ARGUMENT);
    }

    char scratch[MATTER_MQTT_DEVICE_ID_LEN];
    VerifyOrReturnError(CopySegment(begin, end, scratch, sizeof(scratch)), CHIP_ERROR_INVALID_ARGUMENT);
    char * parseEnd              = nullptr;
    const unsigned long long v   = strtoull(scratch, &parseEnd, 16);
    VerifyOrReturnError(parseEnd != scratch && *parseEnd == '\0', CHIP_ERROR_INVALID_ARGUMENT);
    out = v;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ParseTopicHexU32(const char * begin, const char * end, uint32_t & out)
{
    uint64_t wide = 0;
    ReturnErrorOnFailure(ParseTopicHexU64(begin, end, wide));
    VerifyOrReturnError(wide <= UINT32_MAX, CHIP_ERROR_INVALID_ARGUMENT);
    out = static_cast<uint32_t>(wide);
    return CHIP_NO_ERROR;
}

CHIP_ERROR ParseTopicHexEndpoint(const char * begin, const char * end, chip::EndpointId & out)
{
    uint64_t wide = 0;
    ReturnErrorOnFailure(ParseTopicHexU64(begin, end, wide));
    VerifyOrReturnError(wide <= UINT16_MAX, CHIP_ERROR_INVALID_ARGUMENT);
    out = static_cast<chip::EndpointId>(wide);
    return CHIP_NO_ERROR;
}

bool DeviceNodeIdMatches(const char * begin, const char * end, chip::NodeId expectedNodeId)
{
    uint64_t parsed = 0;
    if (ParseTopicHexU64(begin, end, parsed) != CHIP_NO_ERROR)
    {
        return false;
    }
    return parsed == static_cast<uint64_t>(expectedNodeId);
}

} // namespace

namespace MatterMqttTopic {

CHIP_ERROR FormatTopicIdUint64(uint64_t value, char * out, size_t outLen)
{
    return FormatTopicHexU64(value, out, outLen);
}

// Outbound attribute report: matter/v1/{nodeId}/ep/{ep}/cluster/{clusterId}/attr/{attributeId}
CHIP_ERROR BuildAttributeTopic(chip::NodeId deviceNodeId, chip::EndpointId ep, chip::ClusterId clusterId,
                               chip::AttributeId attributeId, char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    char deviceHex[MATTER_MQTT_DEVICE_ID_LEN];
    char epHex[MATTER_MQTT_DEVICE_ID_LEN];
    char clusterHex[MATTER_MQTT_DEVICE_ID_LEN];
    char attrHex[MATTER_MQTT_DEVICE_ID_LEN];

    ReturnErrorOnFailure(FormatTopicIdUint64(static_cast<uint64_t>(deviceNodeId), deviceHex, sizeof(deviceHex)));
    ReturnErrorOnFailure(FormatTopicIdUint64(static_cast<uint64_t>(ep), epHex, sizeof(epHex)));
    ReturnErrorOnFailure(FormatTopicIdUint64(static_cast<uint64_t>(clusterId), clusterHex, sizeof(clusterHex)));
    ReturnErrorOnFailure(FormatTopicIdUint64(static_cast<uint64_t>(attributeId), attrHex, sizeof(attrHex)));

    const int n = snprintf(out, outLen, "%.*s%s/ep/%s/cluster/%s/attr/%s", static_cast<int>(sizeof(kTopicPrefix) - 1),
                           kTopicPrefix, deviceHex, epHex, clusterHex, attrHex);
    VerifyOrReturnError(AppendChecked(n, outLen), CHIP_ERROR_BUFFER_TOO_SMALL);
    return CHIP_NO_ERROR;
}

// Inbound command subscription filter: matter/v1/{nodeId}/ep/+/cluster/+/cmd/+
CHIP_ERROR BuildCommandFilter(chip::NodeId deviceNodeId, char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    char deviceHex[MATTER_MQTT_DEVICE_ID_LEN];
    ReturnErrorOnFailure(FormatTopicIdUint64(static_cast<uint64_t>(deviceNodeId), deviceHex, sizeof(deviceHex)));

    const int n = snprintf(out, outLen, "%.*s%s/ep/+/cluster/+/cmd/+", static_cast<int>(sizeof(kTopicPrefix) - 1),
                           kTopicPrefix, deviceHex);
    VerifyOrReturnError(AppendChecked(n, outLen), CHIP_ERROR_BUFFER_TOO_SMALL);
    return CHIP_NO_ERROR;
}

// Inbound command topic: matter/v1/{nodeId}/ep/{hex}/cluster/{hex}/cmd/{hex}; @p topic may be non-null-terminated.
CHIP_ERROR ParseCommandTopic(chip::CharSpan topic, chip::NodeId expectedDeviceNodeId, ParsedCommandTopic & out)
{
    VerifyOrReturnError(!topic.empty() && topic.size() < MATTER_MQTT_MAX_TOPIC_LEN, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(expectedDeviceNodeId != chip::kUndefinedNodeId, CHIP_ERROR_INVALID_ARGUMENT);

    char scratch[MATTER_MQTT_MAX_TOPIC_LEN];
    memcpy(scratch, topic.data(), topic.size());
    scratch[topic.size()] = '\0';

    const char * begin = scratch;
    const char * end   = scratch + topic.size();

    VerifyOrReturnError(static_cast<size_t>(end - begin) >= strlen(kTopicPrefix), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(memcmp(begin, kTopicPrefix, strlen(kTopicPrefix)) == 0, CHIP_ERROR_INVALID_ARGUMENT);
    begin += strlen(kTopicPrefix);

    const char * afterDevice = static_cast<const char *>(memchr(begin, '/', static_cast<size_t>(end - begin)));
    VerifyOrReturnError(afterDevice != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(DeviceNodeIdMatches(begin, afterDevice, expectedDeviceNodeId), CHIP_ERROR_INVALID_ARGUMENT);

    const char * epKey = FindSegment(afterDevice, end, "/ep/");
    VerifyOrReturnError(epKey != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const char * afterEp = static_cast<const char *>(memchr(epKey, '/', static_cast<size_t>(end - epKey)));
    VerifyOrReturnError(afterEp != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    ReturnErrorOnFailure(ParseTopicHexEndpoint(epKey, afterEp, out.endpoint));

    const char * clusterKey = FindSegment(afterEp, end, "/cluster/");
    VerifyOrReturnError(clusterKey != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const char * afterCluster = static_cast<const char *>(memchr(clusterKey, '/', static_cast<size_t>(end - clusterKey)));
    VerifyOrReturnError(afterCluster != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    ReturnErrorOnFailure(ParseTopicHexU32(clusterKey, afterCluster, out.clusterId));

    const char * cmdKey = FindSegment(afterCluster, end, "/cmd/");
    VerifyOrReturnError(cmdKey != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    ReturnErrorOnFailure(ParseTopicHexU32(cmdKey, end, out.commandId));

    return CHIP_NO_ERROR;
}

} // namespace MatterMqttTopic
