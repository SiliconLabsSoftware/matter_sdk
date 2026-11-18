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

#include "command_injector.h"

#include "matter_mqtt_contract.h"
#include "matter_mqtt_log.h"
#include "mqtt_noop_command_handler.h"

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

} // namespace

CHIP_ERROR MqttCommandInjector::ValidateArgs(chip::ClusterId clusterId, chip::CommandId commandId,
                                            const MatterMqttCodecTypes::ArgsView * args) const
{
    uint8_t tlvBuffer[MATTER_MQTT_MAX_INVOKE_TLV];
    TLVReader reader;
    return BuildInvokeReader(clusterId, commandId, args, tlvBuffer, sizeof(tlvBuffer), reader);
}

CHIP_ERROR MqttCommandInjector::Invoke(const MatterMqttBridge::InboundCommand & command,
                                      const MatterMqttFilterCommandEntry & entry,
                                      const MatterMqttCodecTypes::ArgsView * decodedArgs)
{
    uint8_t tlvBuffer[MATTER_MQTT_MAX_INVOKE_TLV];
    TLVReader cmdReader;
    ReturnErrorOnFailure(
        BuildInvokeReader(command.clusterId, command.commandId, decodedArgs, tlvBuffer, sizeof(tlvBuffer), cmdReader));

    chip::Access::SubjectDescriptor subject;
    DataModel::InvokeRequest request(ConcreteCommandPath(command.endpoint, command.clusterId, command.commandId), subject);
    const bool timed = (entry.flags & MatterMqttBridge::kCommandFlagTimed) != 0;
    if (timed)
    {
        request.invokeFlags.Set(DataModel::InvokeFlags::kTimed);
    }
    sCommandHandler.SetTimedInvoke(timed);

    const auto status = CodegenDataModelProvider::Instance().InvokeCommand(request, cmdReader, &sCommandHandler);
    sCommandHandler.SetTimedInvoke(false);
    if (!status.has_value())
    {
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
