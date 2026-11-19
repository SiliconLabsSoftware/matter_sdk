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

#include <MatterMqttContract.h>
#include <MatterMqttLog.h>

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <zap-generated/gen_config.h>
#include <app/data-model/Encode.h>
#include <app/data-model/Nullable.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/SafeInt.h>
#include <lib/support/TypeTraits.h>

#include <type_traits>
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
using namespace chip::app::Clusters;
using namespace chip::TLV;
using chip::Protocols::InteractionModel::Status;

CHIP_ERROR ImStatusToChipError(Status status)
{
    if (status == Status::Success)
    {
        return CHIP_NO_ERROR;
    }
    return CHIP_ERROR_IM_GLOBAL_STATUS_VALUE(status);
}

CHIP_ERROR WriteMqttNull(char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr && outLen > 0, CHIP_ERROR_INVALID_ARGUMENT);
    strncpy(out, "null", outLen - 1);
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

template <typename T>
CHIP_ERROR WriteNullableAsDecimal(const DataModel::Nullable<T> & value, char * out, size_t outLen)
{
    if (value.IsNull())
    {
        return WriteMqttNull(out, outLen);
    }
    if constexpr (std::is_enum_v<T>)
    {
        return WriteMqttDecimal(static_cast<int32_t>(to_underlying(value.Value())), out, outLen);
    }
    return WriteMqttDecimal(static_cast<int32_t>(value.Value()), out, outLen);
}

/** Codegen accessor path for MQTT filter-registry attributes (v1 demo clusters). */
CHIP_ERROR ReadViaEmberAsString(const chip::app::ConcreteAttributePath & path, char * out, size_t outLen)
{
    const EndpointId endpoint = path.mEndpointId;

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    if (path.mClusterId == OnOff::Id)
    {
        if (path.mAttributeId == OnOff::Attributes::OnOff::Id)
        {
            bool value = false;
            ReturnErrorOnFailure(ImStatusToChipError(OnOff::Attributes::OnOff::Get(endpoint, &value)));
            return WriteMqttDecimal(value ? 1 : 0, out, outLen);
        }
    }
#endif

#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    if (path.mClusterId == Thermostat::Id)
    {
        if (path.mAttributeId == Thermostat::Attributes::LocalTemperature::Id)
        {
            DataModel::Nullable<int16_t> value;
            ReturnErrorOnFailure(ImStatusToChipError(Thermostat::Attributes::LocalTemperature::Get(endpoint, value)));
            return WriteNullableAsDecimal(value, out, outLen);
        }
        if (path.mAttributeId == Thermostat::Attributes::OccupiedHeatingSetpoint::Id)
        {
            int16_t value = 0;
            ReturnErrorOnFailure(ImStatusToChipError(Thermostat::Attributes::OccupiedHeatingSetpoint::Get(endpoint, &value)));
            return WriteMqttDecimal(static_cast<int32_t>(value), out, outLen);
        }
        if (path.mAttributeId == Thermostat::Attributes::OccupiedCoolingSetpoint::Id)
        {
            int16_t value = 0;
            ReturnErrorOnFailure(ImStatusToChipError(Thermostat::Attributes::OccupiedCoolingSetpoint::Get(endpoint, &value)));
            return WriteMqttDecimal(static_cast<int32_t>(value), out, outLen);
        }
        if (path.mAttributeId == Thermostat::Attributes::SystemMode::Id)
        {
            Thermostat::SystemModeEnum value = Thermostat::SystemModeEnum::kOff;
            ReturnErrorOnFailure(ImStatusToChipError(Thermostat::Attributes::SystemMode::Get(endpoint, &value)));
            return WriteMqttDecimal(static_cast<int32_t>(to_underlying(value)), out, outLen);
        }
    }
#endif

#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
    if (path.mClusterId == DoorLock::Id && path.mAttributeId == DoorLock::Attributes::LockState::Id)
    {
        DataModel::Nullable<DoorLock::DlLockState> value;
        ReturnErrorOnFailure(ImStatusToChipError(DoorLock::Attributes::LockState::Get(endpoint, value)));
        return WriteNullableAsDecimal(value, out, outLen);
    }
#endif

#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
    if (path.mClusterId == WindowCovering::Id)
    {
        if (path.mAttributeId == WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id)
        {
            DataModel::Nullable<Percent100ths> value;
            ReturnErrorOnFailure(
                ImStatusToChipError(WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Get(endpoint, value)));
            return WriteNullableAsDecimal(value, out, outLen);
        }
        if (path.mAttributeId == WindowCovering::Attributes::CurrentPositionTiltPercent100ths::Id)
        {
            DataModel::Nullable<Percent100ths> value;
            ReturnErrorOnFailure(
                ImStatusToChipError(WindowCovering::Attributes::CurrentPositionTiltPercent100ths::Get(endpoint, value)));
            return WriteNullableAsDecimal(value, out, outLen);
        }
    }
#endif

    return CHIP_ERROR_NOT_FOUND;
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
        VerifyOrReturnError(chip::CanCastTo<uint8_t>(mode) && chip::CanCastTo<int8_t>(amount), CHIP_ERROR_INVALID_ARGUMENT);
        cmd.mode   = static_cast<Clusters::Thermostat::SetpointRaiseLowerModeEnum>(mode);
        cmd.amount = static_cast<int8_t>(amount);
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
            VerifyOrReturnError(chip::CanCastTo<Percent100ths>(v), CHIP_ERROR_INVALID_ARGUMENT);
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
            VerifyOrReturnError(chip::CanCastTo<Percent100ths>(v), CHIP_ERROR_INVALID_ARGUMENT);
            cmd.tiltPercent100thsValue = static_cast<Percent100ths>(v);
            return DataModel::Encode(writer, TLV::AnonymousTag(), cmd);
        }
    }
#endif
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR FinalizeInvokeReader(TLVWriter & writer, uint8_t * tlvBuffer, TLVReader & reader, size_t & tlvLen)
{
    ReturnErrorOnFailure(writer.Finalize());
    tlvLen = writer.GetLengthWritten();
    reader.Init(tlvBuffer, tlvLen);
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
    MATTER_MQTT_LOG_DETAIL("attribute read failed ep=%u attr=" ChipLogFormatMEI " err=%" CHIP_ERROR_FORMAT,
                           static_cast<unsigned>(path.mEndpointId), ChipLogValueMEI(path.mAttributeId), err.Format());
    return err;
}

CHIP_ERROR PlatformMqttCommandInjector::BuildInvokeReader(chip::ClusterId clusterId, chip::CommandId commandId,
                                                          const MatterMqttCodecTypes::ArgsView * args, uint8_t * tlvBuffer,
                                                          size_t tlvBufferSize, TLVReader & reader, size_t & tlvLen) const
{
    TLVWriter writer;
    writer.Init(tlvBuffer, tlvBufferSize);
    ReturnErrorOnFailure(EncodeInvokePayload(clusterId, commandId, args, writer));
    return FinalizeInvokeReader(writer, tlvBuffer, reader, tlvLen);
}
