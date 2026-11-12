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

#include <cstdio>
#include <cstring>

namespace {

constexpr char kTopicPrefix[] = "matter/v1/";

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
    VerifyOrReturnError(len + 1 < outLen, false);
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

bool DeviceIdMatches(const char * begin, const char * end, chip::CharSpan expectedDeviceId)
{
    const size_t devLen = static_cast<size_t>(end - begin);
    VerifyOrReturnError(devLen == expectedDeviceId.size(), false);
    return memcmp(begin, expectedDeviceId.data(), devLen) == 0;
}

} // namespace

namespace MatterMqttTopic {

// Outbound attribute report: matter/v1/{deviceId}/ep/{ep}/cluster/{cluster}/attr/{attr}
CHIP_ERROR BuildAttributeTopic(chip::CharSpan deviceId, chip::EndpointId ep, chip::CharSpan clusterName, chip::CharSpan attrName,
                               char * out, size_t outLen)
{
    VerifyOrReturnError(!deviceId.empty() && !clusterName.empty() && !attrName.empty() && out != nullptr,
                        CHIP_ERROR_INVALID_ARGUMENT);

    const int n = snprintf(out, outLen, "%.*s%.*s/ep/%u/cluster/%.*s/attr/%.*s", static_cast<int>(sizeof(kTopicPrefix) - 1),
                           kTopicPrefix, static_cast<int>(deviceId.size()), deviceId.data(), static_cast<unsigned>(ep),
                           static_cast<int>(clusterName.size()), clusterName.data(), static_cast<int>(attrName.size()),
                           attrName.data());
    VerifyOrReturnError(AppendChecked(n, outLen), CHIP_ERROR_BUFFER_TOO_SMALL);
    return CHIP_NO_ERROR;
}

// Inbound command subscription filter for this device (Paho '+' wildcards in ep/cluster/cmd segments).
CHIP_ERROR BuildCommandFilter(chip::CharSpan deviceId, bool useWildcard, char * out, size_t outLen)
{
    VerifyOrReturnError(!deviceId.empty() && out != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    int n;
    if (useWildcard)
    {
        n = snprintf(out, outLen, "%.*s%.*s/ep/+/cluster/+/cmd/+", static_cast<int>(sizeof(kTopicPrefix) - 1), kTopicPrefix,
                     static_cast<int>(deviceId.size()), deviceId.data());
    }
    else
    {
        n = snprintf(out, outLen, "%.*s%.*s/ep/+/cluster/+/cmd/", static_cast<int>(sizeof(kTopicPrefix) - 1), kTopicPrefix,
                     static_cast<int>(deviceId.size()), deviceId.data());
    }
    VerifyOrReturnError(AppendChecked(n, outLen), CHIP_ERROR_BUFFER_TOO_SMALL);
    return CHIP_NO_ERROR;
}

// Inbound command topic: matter/v1/{deviceId}/ep/{n}/cluster/{name}/cmd/{command}; @p topic may be non-null-terminated.
CHIP_ERROR ParseCommandTopic(chip::CharSpan topic, chip::CharSpan expectedDeviceId, ParsedCommandTopic & out)
{
    VerifyOrReturnError(!topic.empty() && topic.size() < MATTER_MQTT_MAX_TOPIC_LEN, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(!expectedDeviceId.empty(), CHIP_ERROR_INVALID_ARGUMENT);

    // Copy into scratch so segment parsers can use C-style scans; RX topic is often lenstring without trailing NUL.
    char scratch[MATTER_MQTT_MAX_TOPIC_LEN];
    memcpy(scratch, topic.data(), topic.size());
    scratch[topic.size()] = '\0';

    const char * begin = scratch;
    const char * end   = scratch + topic.size();

    // matter/v1/
    VerifyOrReturnError(static_cast<size_t>(end - begin) >= strlen(kTopicPrefix), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(memcmp(begin, kTopicPrefix, strlen(kTopicPrefix)) == 0, CHIP_ERROR_INVALID_ARGUMENT);
    begin += strlen(kTopicPrefix);

    // {deviceId}/  (must match commissioned node id string, length compared without requiring NUL on input topic)
    const char * afterDevice = static_cast<const char *>(memchr(begin, '/', static_cast<size_t>(end - begin)));
    VerifyOrReturnError(afterDevice != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(DeviceIdMatches(begin, afterDevice, expectedDeviceId), CHIP_ERROR_INVALID_ARGUMENT);

    // /ep/{endpointId}/
    const char * epKey = FindSegment(afterDevice, end, "/ep/");
    VerifyOrReturnError(epKey != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const char * afterEp = static_cast<const char *>(memchr(epKey, '/', static_cast<size_t>(end - epKey)));
    VerifyOrReturnError(afterEp != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    char epBuf[8];
    VerifyOrReturnError(CopySegment(epKey, afterEp, epBuf, sizeof(epBuf)), CHIP_ERROR_INVALID_ARGUMENT);
    char * epEnd              = nullptr;
    const unsigned long epVal = strtoul(epBuf, &epEnd, 10);
    VerifyOrReturnError(epEnd != epBuf && *epEnd == '\0' && epVal <= UINT16_MAX, CHIP_ERROR_INVALID_ARGUMENT);
    out.endpoint = static_cast<chip::EndpointId>(epVal);

    // /cluster/{ClusterName}/
    const char * clusterKey = FindSegment(afterEp, end, "/cluster/");
    VerifyOrReturnError(clusterKey != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const char * afterCluster = static_cast<const char *>(memchr(clusterKey, '/', static_cast<size_t>(end - clusterKey)));
    VerifyOrReturnError(afterCluster != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    static char clusterName[48];
    VerifyOrReturnError(CopySegment(clusterKey, afterCluster, clusterName, sizeof(clusterName)), CHIP_ERROR_INVALID_ARGUMENT);
    out.clusterName = clusterName;

    // /cmd/{CommandName}  (no further path segments)
    const char * cmdKey = FindSegment(afterCluster, end, "/cmd/");
    VerifyOrReturnError(cmdKey != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    static char commandName[48];
    VerifyOrReturnError(CopySegment(cmdKey, end, commandName, sizeof(commandName)), CHIP_ERROR_INVALID_ARGUMENT);
    out.commandName = commandName;

    return CHIP_NO_ERROR;
}

} // namespace MatterMqttTopic
