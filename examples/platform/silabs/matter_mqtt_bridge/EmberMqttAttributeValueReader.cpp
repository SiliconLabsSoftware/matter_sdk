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

#include "EmberMqttAttributeValueReader.h"

#include <matter_mqtt_log.h>

#include <app-common/zap-generated/attribute-type.h>
#include <app/util/attribute-storage.h>
#include <app/util/attribute-table.h>
#include <lib/core/CHIPEncoding.h>
#include <lib/support/attribute-storage-null-handling.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <cstring>
#include <protocols/interaction_model/StatusCode.h>

namespace {

using namespace chip;

constexpr size_t kEmberScalarReadSize = 8;

template <typename T>
bool IsStoredNullValue(const EmberAfAttributeMetadata * meta, T stored)
{
    return meta->IsNullable() && chip::app::NumericAttributeTraits<T>::IsNullValue(stored);
}

CHIP_ERROR ReadViaEmber(const chip::app::ConcreteAttributePath & path, MatterMqttBridge::RawValue & out)
{
    const EmberAfAttributeMetadata * meta =
        emberAfLocateAttributeMetadata(path.mEndpointId, path.mClusterId, path.mAttributeId);
    VerifyOrReturnError(meta != nullptr, CHIP_ERROR_NOT_FOUND);

    const uint16_t attrSize = emberAfAttributeSize(meta);
    const uint16_t readLen  = static_cast<uint16_t>(attrSize <= kEmberScalarReadSize ? attrSize : kEmberScalarReadSize);
    if (readLen < attrSize)
    {
        MATTER_MQTT_LOG_ERROR("ember attr size %u exceeds scalar read cap", attrSize);
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }

    uint8_t buffer[kEmberScalarReadSize] = {};
    const auto status                    = emberAfReadAttribute(path.mEndpointId, path.mClusterId, path.mAttributeId, buffer, readLen);
    VerifyOrReturnError(status == Protocols::InteractionModel::Status::Success,
                        CHIP_ERROR_IM_GLOBAL_STATUS_VALUE(status));

    switch (meta->attributeType)
    {
    case ZCL_BOOLEAN_ATTRIBUTE_TYPE: {
        out.type         = MatterMqttBridge::RawValueType::kBool;
        out.data.boolean = buffer[0] != 0;
        return CHIP_NO_ERROR;
    }
    case ZCL_BITMAP8_ATTRIBUTE_TYPE:
    case ZCL_ENUM8_ATTRIBUTE_TYPE:
    case ZCL_INT8U_ATTRIBUTE_TYPE:
    case ZCL_PERCENT_ATTRIBUTE_TYPE: {
        const uint8_t stored = buffer[0];
        if (IsStoredNullValue(meta, stored))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        out.type               = MatterMqttBridge::RawValueType::kUnsigned;
        out.data.unsignedValue = stored;
        return CHIP_NO_ERROR;
    }
    case ZCL_INT8S_ATTRIBUTE_TYPE: {
        const int8_t stored = static_cast<int8_t>(buffer[0]);
        if (IsStoredNullValue(meta, stored))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        out.type             = MatterMqttBridge::RawValueType::kSigned;
        out.data.signedValue = stored;
        return CHIP_NO_ERROR;
    }
    case ZCL_TEMPERATURE_ATTRIBUTE_TYPE: {
        const int16_t stored = static_cast<int16_t>(chip::Encoding::LittleEndian::Get16(buffer));
        if (IsStoredNullValue(meta, stored))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        out.type             = MatterMqttBridge::RawValueType::kSigned;
        out.data.signedValue = stored;
        return CHIP_NO_ERROR;
    }
    case ZCL_PERCENT100THS_ATTRIBUTE_TYPE:
    case ZCL_ENUM16_ATTRIBUTE_TYPE:
    case ZCL_BITMAP16_ATTRIBUTE_TYPE:
    case ZCL_INT16U_ATTRIBUTE_TYPE:
    case ZCL_INT16S_ATTRIBUTE_TYPE:
    case ZCL_VOLTAGE_MV_ATTRIBUTE_TYPE:
    case ZCL_AMPERAGE_MA_ATTRIBUTE_TYPE: {
        const int16_t asSigned   = static_cast<int16_t>(chip::Encoding::LittleEndian::Get16(buffer));
        const uint16_t asUnsigned = chip::Encoding::LittleEndian::Get16(buffer);
        if (meta->attributeType == ZCL_INT16S_ATTRIBUTE_TYPE && IsStoredNullValue(meta, asSigned))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        if (meta->attributeType == ZCL_INT16U_ATTRIBUTE_TYPE && IsStoredNullValue(meta, asUnsigned))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        if (meta->attributeType == ZCL_INT16S_ATTRIBUTE_TYPE)
        {
            out.type             = MatterMqttBridge::RawValueType::kSigned;
            out.data.signedValue = asSigned;
        }
        else
        {
            out.type               = MatterMqttBridge::RawValueType::kUnsigned;
            out.data.unsignedValue = asUnsigned;
        }
        return CHIP_NO_ERROR;
    }
    case ZCL_POWER_MW_ATTRIBUTE_TYPE:
    case ZCL_INT32S_ATTRIBUTE_TYPE: {
        const int32_t stored = chip::Encoding::LittleEndian::Get32(buffer);
        if (IsStoredNullValue(meta, stored))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        out.type             = MatterMqttBridge::RawValueType::kSigned;
        out.data.signedValue = stored;
        return CHIP_NO_ERROR;
    }
    case ZCL_INT32U_ATTRIBUTE_TYPE:
    case ZCL_BITMAP32_ATTRIBUTE_TYPE: {
        const uint32_t stored = chip::Encoding::LittleEndian::Get32(buffer);
        if (IsStoredNullValue(meta, stored))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        out.type               = MatterMqttBridge::RawValueType::kUnsigned;
        out.data.unsignedValue = stored;
        return CHIP_NO_ERROR;
    }
    case ZCL_ENERGY_MWH_ATTRIBUTE_TYPE:
    case ZCL_INT64S_ATTRIBUTE_TYPE: {
        const int64_t stored = chip::Encoding::LittleEndian::Get64(buffer);
        if (IsStoredNullValue(meta, stored))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        out.type             = MatterMqttBridge::RawValueType::kSigned;
        out.data.signedValue = static_cast<int32_t>(stored);
        return CHIP_NO_ERROR;
    }
    case ZCL_INT64U_ATTRIBUTE_TYPE:
    case ZCL_BITMAP64_ATTRIBUTE_TYPE: {
        const uint64_t stored = chip::Encoding::LittleEndian::Get64(buffer);
        if (IsStoredNullValue(meta, stored))
        {
            out.type = MatterMqttBridge::RawValueType::kNull;
            return CHIP_NO_ERROR;
        }
        out.type               = MatterMqttBridge::RawValueType::kUnsigned;
        out.data.unsignedValue = static_cast<uint32_t>(stored);
        return CHIP_NO_ERROR;
    }
    case ZCL_SINGLE_ATTRIBUTE_TYPE: {
        float value = 0.0f;
        memcpy(&value, buffer, sizeof(value));
        out.type             = MatterMqttBridge::RawValueType::kSigned;
        out.data.signedValue = static_cast<int32_t>(value);
        return CHIP_NO_ERROR;
    }
    case ZCL_DOUBLE_ATTRIBUTE_TYPE: {
        double value = 0.0;
        memcpy(&value, buffer, sizeof(value));
        out.type             = MatterMqttBridge::RawValueType::kSigned;
        out.data.signedValue = static_cast<int32_t>(value);
        return CHIP_NO_ERROR;
    }
    default:
        MATTER_MQTT_LOG_ERROR("ember read unsupported type 0x%02x", meta->attributeType);
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }
}

} // namespace

CHIP_ERROR EmberMqttAttributeValueReader::Read(const chip::app::ConcreteAttributePath & path, MatterMqttBridge::RawValue & out)
{
    const CHIP_ERROR err = ReadViaEmber(path, out);
    if (err == CHIP_NO_ERROR)
    {
        return CHIP_NO_ERROR;
    }
    MATTER_MQTT_LOG_DETAIL("ember read failed ep=%u attr=" ChipLogFormatMEI " err=%" CHIP_ERROR_FORMAT,
                           static_cast<unsigned>(path.mEndpointId), ChipLogValueMEI(path.mAttributeId), err.Format());
    return err;
}
