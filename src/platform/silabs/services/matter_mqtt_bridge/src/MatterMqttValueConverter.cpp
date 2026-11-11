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

#include "MatterMqttValueConverter.h"

#include <lib/support/CodeUtils.h>

#include <cstdio>
#include <cstring>

// Maps RawValue + adapter quantity metadata to MQTT report JSON value strings.

namespace {

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER) || defined(MATTER_MQTT_BRIDGE_HOST_TEST)
using namespace chip::app::Clusters;

bool FormatOnOffBool(bool value, char * out, size_t outLen)
{
    const char * text = value ? "on" : "off";
    strncpy(out, text, outLen - 1);
    out[outLen - 1] = '\0';
    return true;
}
#endif

} // namespace

CHIP_ERROR MatterMqttFormatReportValue(const ClusterAdapter * adapter, const MatterMqttAttributeEntry * attr,
                                       const MatterMqttBridge::RawValue & raw, char * out, size_t outLen, bool & asNumber)
{
    VerifyOrReturnError(adapter != nullptr && attr != nullptr && out != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    // asNumber true -> EncodeReport emits unquoted JSON number; false -> quoted string or enum name.
    asNumber = false;

    if (raw.type == MatterMqttBridge::RawValueType::kNull)
    {
        strncpy(out, "null", outLen - 1);
        out[outLen - 1] = '\0';
        return CHIP_NO_ERROR;
    }

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER) || defined(MATTER_MQTT_BRIDGE_HOST_TEST)
    // Contract uses "on"/"off" strings for OnOff (not true/false).
    if (adapter->clusterId == OnOff::Id && attr->id == OnOff::Attributes::OnOff::Id &&
        raw.type == MatterMqttBridge::RawValueType::kBool)
    {
        VerifyOrReturnError(FormatOnOffBool(raw.data.boolean, out, outLen), CHIP_ERROR_INTERNAL);
        return CHIP_NO_ERROR;
    }
#endif

    // e.g. SystemMode enum name instead of raw integer in JSON.
    if (attr->quantity == MatterMqttAttributeQuantity::kEnum && adapter->formatEnum != nullptr &&
        raw.type == MatterMqttBridge::RawValueType::kUnsigned)
    {
        const char * name = adapter->formatEnum(attr->id, raw.data.unsignedValue);
        VerifyOrReturnError(name != nullptr, CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
        strncpy(out, name, outLen - 1);
        out[outLen - 1] = '\0';
        return CHIP_NO_ERROR;
    }

    // Temperature / generic integers: emit decimal text and mark for unquoted JSON number.
    if (raw.type == MatterMqttBridge::RawValueType::kSigned || raw.type == MatterMqttBridge::RawValueType::kUnsigned)
    {
        const int32_t value = raw.type == MatterMqttBridge::RawValueType::kSigned ? raw.data.signedValue : static_cast<int32_t>(raw.data.unsignedValue);
        const int n         = snprintf(out, outLen, "%d", static_cast<int>(value));
        VerifyOrReturnError(n >= 0 && static_cast<size_t>(n) < outLen, CHIP_ERROR_BUFFER_TOO_SMALL);
        asNumber = true;
        return CHIP_NO_ERROR;
    }

    if (raw.type == MatterMqttBridge::RawValueType::kBool)
    {
        strncpy(out, raw.data.boolean ? "true" : "false", outLen - 1);
        out[outLen - 1] = '\0';
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}
