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

#include "EmberMqttBridgePlatform.h"

#include <matter_mqtt_contract.h>
#include <matter_mqtt_log.h>

#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app/data-model/Encode.h>
#include <app/util/attribute-storage.h>
#include <app/util/attribute-table.h>
#include <lib/core/CHIPEncoding.h>
#include <lib/support/attribute-storage-null-handling.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <protocols/interaction_model/StatusCode.h>

#include <climits>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using namespace chip;
using namespace chip::app;
using namespace chip::TLV;

constexpr size_t kEmberScalarReadSize = 2;

template <typename T>
bool IsStoredNullValue(const EmberAfAttributeMetadata * meta, T stored)
{
    return meta->IsNullable() && chip::app::NumericAttributeTraits<T>::IsNullValue(stored);
}

CHIP_ERROR WriteMqttNull(char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr && outLen > 0, CHIP_ERROR_INVALID_ARGUMENT);
    strncpy(out, "null", outLen - 1);
    out[outLen - 1] = '\0';
    return CHIP_NO_ERROR;
}

CHIP_ERROR WriteMqttBool(uint8_t stored, char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr && outLen > 0, CHIP_ERROR_INVALID_ARGUMENT);
    strncpy(out, stored != 0 ? "1" : "0", outLen - 1);
    out[outLen - 1] = '\0';
    return CHIP_NO_ERROR;
}

CHIP_ERROR WriteMqttDecimal(int32_t value, char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr && outLen > 0, CHIP_ERROR_INVALID_ARGUMENT);
    const int n = snprintf(out, outLen, "%" PRId32, value);
    VerifyOrReturnError(n >= 0 && static_cast<size_t>(n) < outLen, CHIP_ERROR_BUFFER_TOO_SMALL);
    return CHIP_NO_ERROR;
}

/** Ember read path scoped to MQTT filter-registry attribute ZCL types (v1 demo clusters). */
CHIP_ERROR ReadViaEmberAsString(const chip::app::ConcreteAttributePath & path, char * out, size_t outLen)
{
    const EmberAfAttributeMetadata * meta =
        emberAfLocateAttributeMetadata(path.mEndpointId, path.mClusterId, path.mAttributeId);
    VerifyOrReturnError(meta != nullptr, CHIP_ERROR_NOT_FOUND);

    const uint16_t attrSize = emberAfAttributeSize(meta);
    VerifyOrReturnError(attrSize <= kEmberScalarReadSize, CHIP_ERROR_BUFFER_TOO_SMALL);

    uint8_t buffer[kEmberScalarReadSize] = {};
    const auto status                    = emberAfReadAttribute(path.mEndpointId, path.mClusterId, path.mAttributeId, buffer, attrSize);
    VerifyOrReturnError(status == Protocols::InteractionModel::Status::Success,
                        CHIP_ERROR_IM_GLOBAL_STATUS_VALUE(status));

    switch (meta->attributeType)
    {
    case ZCL_BOOLEAN_ATTRIBUTE_TYPE:
        return WriteMqttBool(buffer[0], out, outLen);
    case ZCL_ENUM8_ATTRIBUTE_TYPE:
    case ZCL_BITMAP8_ATTRIBUTE_TYPE:
    case ZCL_INT8U_ATTRIBUTE_TYPE:
    case ZCL_PERCENT_ATTRIBUTE_TYPE: {
        const uint8_t stored = buffer[0];
        if (IsStoredNullValue(meta, stored))
        {
            return WriteMqttNull(out, outLen);
        }
        return WriteMqttDecimal(static_cast<int32_t>(stored), out, outLen);
    }
    case ZCL_TEMPERATURE_ATTRIBUTE_TYPE:
    case ZCL_INT16S_ATTRIBUTE_TYPE: {
        const int16_t stored = static_cast<int16_t>(chip::Encoding::LittleEndian::Get16(buffer));
        if (IsStoredNullValue(meta, stored))
        {
            return WriteMqttNull(out, outLen);
        }
        return WriteMqttDecimal(static_cast<int32_t>(stored), out, outLen);
    }
    case ZCL_PERCENT100THS_ATTRIBUTE_TYPE:
    case ZCL_INT16U_ATTRIBUTE_TYPE: {
        const uint16_t stored = chip::Encoding::LittleEndian::Get16(buffer);
        if (IsStoredNullValue(meta, stored))
        {
            return WriteMqttNull(out, outLen);
        }
        return WriteMqttDecimal(static_cast<int32_t>(stored), out, outLen);
    }
    default:
        MATTER_MQTT_LOG_ERROR("ember read unsupported type 0x%02x", meta->attributeType);
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }
}

const char * ArgText(const MatterMqttCodecTypes::ArgsView * args, const char * name)
{
    if (args == nullptr)
    {
        return nullptr;
    }
    for (size_t i = 0; i < args->count; ++i)
    {
        if (strcmp(args->items[i].name, name) == 0)
        {
            return args->items[i].value;
        }
    }
    return nullptr;
}

CHIP_ERROR ParseDecimalString(const char * text, long & out)
{
    VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    char * end   = nullptr;
    const long v = strtol(text, &end, 10);
    VerifyOrReturnError(end != text && *end == '\0', CHIP_ERROR_INVALID_ARGUMENT);
    out = v;
    return CHIP_NO_ERROR;
}

CHIP_ERROR EncodeInvokePayload(chip::ClusterId clusterId, chip::CommandId commandId,
                               const MatterMqttCodecTypes::ArgsView * args, TLVWriter & writer)
{
#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    using namespace Clusters::OnOff::Commands;
    if (clusterId == Clusters::OnOff::Id)
    {
        if (commandId == On::Id)
        {
            On::Type cmd;
            return DataModel::Encode(writer, TLV::AnonymousTag(), cmd);
        }
        if (commandId == Off::Id)
        {
            Off::Type cmd;
            return DataModel::Encode(writer, TLV::AnonymousTag(), cmd);
        }
        if (commandId == Toggle::Id)
        {
            Toggle::Type cmd;
            return DataModel::Encode(writer, TLV::AnonymousTag(), cmd);
        }
    }
#endif
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    using namespace Clusters::Thermostat::Commands;
    if (clusterId == Clusters::Thermostat::Id && commandId == SetpointRaiseLower::Id)
    {
        SetpointRaiseLower::Type cmd;
        const char * modeText   = ArgText(args, "mode");
        const char * amountText = ArgText(args, "amount");
        VerifyOrReturnError(modeText != nullptr && amountText != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
        long mode   = 0;
        long amount = 0;
        ReturnErrorOnFailure(ParseDecimalString(modeText, mode));
        ReturnErrorOnFailure(ParseDecimalString(amountText, amount));
        VerifyOrReturnError(mode >= 0 && mode <= 255, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(amount >= INT8_MIN && amount <= INT8_MAX, CHIP_ERROR_INVALID_ARGUMENT);
        cmd.mode   = static_cast<Clusters::Thermostat::SetpointRaiseLowerModeEnum>(mode);
        cmd.amount = static_cast<int8_t>(amount);
        return DataModel::Encode(writer, TLV::AnonymousTag(), cmd);
    }
#endif
#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
    using namespace Clusters::DoorLock::Commands;
    if (clusterId == Clusters::DoorLock::Id && commandId == LockDoor::Id)
    {
        LockDoor::Type cmd;
        const char * pin = ArgText(args, "PINCode");
        if (pin != nullptr && pin[0] != '\0')
        {
            cmd.PINCode.SetValue(ByteSpan(reinterpret_cast<const uint8_t *>(pin), strlen(pin)));
        }
        return DataModel::Encode(writer, TLV::AnonymousTag(), cmd);
    }
#endif
#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
    using namespace Clusters::WindowCovering::Commands;
    if (clusterId == Clusters::WindowCovering::Id)
    {
        if (commandId == GoToLiftPercentage::Id)
        {
            GoToLiftPercentage::Type cmd;
            const char * text = ArgText(args, "liftPercent100thsValue");
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            long v = 0;
            ReturnErrorOnFailure(ParseDecimalString(text, v));
            VerifyOrReturnError(v >= 0 && v <= UINT16_MAX, CHIP_ERROR_INVALID_ARGUMENT);
            cmd.liftPercent100thsValue = static_cast<Percent100ths>(v);
            return DataModel::Encode(writer, TLV::AnonymousTag(), cmd);
        }
        if (commandId == GoToTiltPercentage::Id)
        {
            GoToTiltPercentage::Type cmd;
            const char * text = ArgText(args, "tiltPercent100thsValue");
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            long v = 0;
            ReturnErrorOnFailure(ParseDecimalString(text, v));
            VerifyOrReturnError(v >= 0 && v <= UINT16_MAX, CHIP_ERROR_INVALID_ARGUMENT);
            cmd.tiltPercent100thsValue = static_cast<Percent100ths>(v);
            return DataModel::Encode(writer, TLV::AnonymousTag(), cmd);
        }
    }
#endif
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR FinalizeInvokeReader(TLVWriter & writer, uint8_t * tlvBuffer, TLVReader & reader)
{
    ReturnErrorOnFailure(writer.Finalize());
    reader.Init(tlvBuffer, writer.GetLengthWritten());
    ReturnErrorOnFailure(reader.Next());
    VerifyOrReturnError(reader.GetType() == TLVType::kTLVType_Structure, CHIP_ERROR_INVALID_ARGUMENT);
    return CHIP_NO_ERROR;
}

} // namespace

CHIP_ERROR EmberMqttAttributeValueReader::ReadAsString(const chip::app::ConcreteAttributePath & path, char * out, size_t outLen)
{
    const CHIP_ERROR err = ReadViaEmberAsString(path, out, outLen);
    if (err == CHIP_NO_ERROR)
    {
        return CHIP_NO_ERROR;
    }
    MATTER_MQTT_LOG_DETAIL("ember read failed ep=%u attr=" ChipLogFormatMEI " err=%" CHIP_ERROR_FORMAT,
                           static_cast<unsigned>(path.mEndpointId), ChipLogValueMEI(path.mAttributeId), err.Format());
    return err;
}

CHIP_ERROR PlatformMqttCommandInjector::BuildInvokeReader(chip::ClusterId clusterId, chip::CommandId commandId,
                                                          const MatterMqttCodecTypes::ArgsView * args, uint8_t * tlvBuffer,
                                                          size_t tlvBufferSize, TLVReader & reader) const
{
    TLVWriter writer;
    writer.Init(tlvBuffer, tlvBufferSize);
    ReturnErrorOnFailure(EncodeInvokePayload(clusterId, commandId, args, writer));
    return FinalizeInvokeReader(writer, tlvBuffer, reader);
}
