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

#include "CommandInjector.h"

#include "MatterMqttContract.h"
#include "MatterMqttLog.h"
#include "MqttNoopCommandHandler.h"

#include <app/ConcreteClusterPath.h>
#include <app/data-model-provider/ActionReturnStatus.h>
#include <app/data-model-provider/OperationTypes.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

namespace {

using namespace chip;
using namespace chip::app;
using namespace chip::TLV;

MqttNoopCommandHandler sCommandHandler;

CHIP_ERROR InitInvokeTlvReader(const uint8_t * tlv, size_t tlvLen, TLVReader & reader)
{
    VerifyOrReturnError(tlv != nullptr && tlvLen > 0, CHIP_ERROR_INVALID_ARGUMENT);
    reader.Init(tlv, tlvLen);
    ReturnErrorOnFailure(reader.Next());
    VerifyOrReturnError(reader.GetType() == TLVType::kTLVType_Structure, CHIP_ERROR_INVALID_ARGUMENT);
    return CHIP_NO_ERROR;
}

} // namespace

CHIP_ERROR MqttCommandInjector::EncodeInvokeTlv(chip::ClusterId clusterId, chip::CommandId commandId,
                                                const MatterMqttCodecTypes::ArgsView * args, uint8_t * tlvBuffer,
                                                size_t tlvBufferSize, size_t & tlvLen) const
{
    TLVReader reader;
    tlvLen = 0;
    return BuildInvokeReader(clusterId, commandId, args, tlvBuffer, tlvBufferSize, reader, tlvLen);
}

CHIP_ERROR MqttCommandInjector::Invoke(const MatterMqttTopic::ParsedCommandTopic & command, const uint8_t * invokeTlv,
                                       size_t invokeTlvLen)
{
    TLVReader cmdReader;
    ReturnErrorOnFailure(InitInvokeTlvReader(invokeTlv, invokeTlvLen, cmdReader));

    chip::Access::SubjectDescriptor subject;
    DataModel::InvokeRequest request(ConcreteCommandPath(command.endpoint, command.clusterId, command.commandId), subject);

    sCommandHandler.Reset();
    const auto status = CodegenDataModelProvider::Instance().InvokeCommand(request, cmdReader, &sCommandHandler);
    if (!status.has_value())
    {
        const auto & captured = sCommandHandler.LastStatus();
        if (captured.has_value() && !captured->IsSuccess())
        {
            const CHIP_ERROR err = DataModel::ActionReturnStatus(*captured).GetUnderlyingError();
            MATTER_MQTT_LOG_ERROR("InvokeCommand failed ep=%u cmd=" ChipLogFormatMEI " err=%" CHIP_ERROR_FORMAT,
                                  static_cast<unsigned>(command.endpoint), ChipLogValueMEI(command.commandId), err.Format());
            return err;
        }
        return CHIP_NO_ERROR;
    }
    if (status->IsError())
    {
        const CHIP_ERROR err = status->GetUnderlyingError();
        MATTER_MQTT_LOG_ERROR("InvokeCommand failed ep=%u cmd=" ChipLogFormatMEI " err=%" CHIP_ERROR_FORMAT,
                              static_cast<unsigned>(command.endpoint), ChipLogValueMEI(command.commandId), err.Format());
        return err;
    }
    return CHIP_NO_ERROR;
}