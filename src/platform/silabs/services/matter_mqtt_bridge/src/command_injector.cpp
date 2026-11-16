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
#include <lib/core/TLV.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include <cstring>

// Builds invoke TLV from packed args + JSON schema, then calls CodegenDataModelProvider::InvokeCommand.

namespace {

using namespace chip;
using namespace chip::app;
using namespace chip::TLV;

MqttNoopCommandHandler sCommandHandler;

Tag ArgFieldTag(uint8_t fieldId)
{
    return ContextTag(fieldId);
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

// Merge packed numeric/bool args with UTF-8 strings from JSON into one invoke-structure TLV.
// @p tlvBuffer must outlive use of @p reader (caller-owned storage).
CHIP_ERROR BuildCommandReader(uint8_t * tlvBuffer, size_t tlvBufferSize, const MatterMqttCommandEntry & entry,
                              const MatterMqttBridge::InboundCommand & command,
                              const MatterMqttCodecTypes::ArgsView * decodedArgs, TLVReader & reader)
{
    TLVWriter writer;
    writer.Init(tlvBuffer, tlvBufferSize);

    TLVType container;
    ReturnErrorOnFailure(writer.StartContainer(AnonymousTag(), TLVType::kTLVType_Structure, container));

    // Each schema field: JSON string lookup and/or the next byte from PackCommandArgs.
    size_t offset = 0;
    for (uint8_t i = 0; i < entry.argSchemaCount; ++i)
    {
        const MatterMqttArgField & field = entry.argSchema[i];
        const char * text                = ArgText(decodedArgs, field.name);

        switch (field.tlvType)
        {
        // Boolean arg: next packed byte from PackCommandArgs.
        case TLVType::kTLVType_Boolean: {
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(offset < command.argsLen, CHIP_ERROR_INVALID_ARGUMENT);
            const uint8_t raw = command.args[offset++];
            ReturnErrorOnFailure(writer.PutBoolean(ArgFieldTag(field.fieldId), raw != 0));
            break;
        }
        // Signed integer arg: one packed int8-sized byte from PackCommandArgs.
        case TLVType::kTLVType_SignedInteger: {
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(offset < command.argsLen, CHIP_ERROR_INVALID_ARGUMENT);
            const uint8_t raw = command.args[offset++];
            ReturnErrorOnFailure(writer.Put(ArgFieldTag(field.fieldId), static_cast<int8_t>(raw)));
            break;
        }
        // Unsigned integer arg: one packed byte from PackCommandArgs.
        case TLVType::kTLVType_UnsignedInteger: {
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(offset < command.argsLen, CHIP_ERROR_INVALID_ARGUMENT);
            const uint8_t raw = command.args[offset++];
            ReturnErrorOnFailure(writer.Put(ArgFieldTag(field.fieldId), raw));
            break;
        }
        // UTF-8 arg: value stays in decoded JSON (not in packed bytes).
        case TLVType::kTLVType_UTF8String: {
            if (text == nullptr)
            {
                // Optional string omitted from JSON.
                if (field.optional)
                {
                    break;
                }
                return CHIP_ERROR_INVALID_ARGUMENT;
            }
            ReturnErrorOnFailure(
                writer.Put(ArgFieldTag(field.fieldId), ByteSpan(reinterpret_cast<const uint8_t *>(text), strlen(text))));
            break;
        }
        // Adapter schema must use only the TLV types handled above.
        default:
            return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
        }
    }

    ReturnErrorOnFailure(writer.EndContainer(container));
    ReturnErrorOnFailure(writer.Finalize());

    // Position reader at the invoke payload structure for CodegenDataModelProvider::InvokeCommand.
    reader.Init(tlvBuffer, writer.GetLengthWritten());
    ReturnErrorOnFailure(reader.Next());
    VerifyOrReturnError(reader.GetType() == TLVType::kTLVType_Structure, CHIP_ERROR_INVALID_ARGUMENT);
    return CHIP_NO_ERROR;
}

} // namespace

CHIP_ERROR MqttCommandInjector::Invoke(const MatterMqttBridge::InboundCommand & command,
                                       const MatterMqttCommandEntry & entry,
                                       const MatterMqttCodecTypes::ArgsView * decodedArgs)
{
    uint8_t tlvBuffer[MATTER_MQTT_MAX_INVOKE_TLV];
    TLVReader cmdReader;
    ReturnErrorOnFailure(BuildCommandReader(tlvBuffer, sizeof(tlvBuffer), entry, command, decodedArgs, cmdReader));

    chip::Access::SubjectDescriptor subject;
    DataModel::InvokeRequest request(ConcreteCommandPath(command.endpoint, command.clusterId, command.commandId), subject);
    if ((entry.flags & MatterMqttBridge::kCommandFlagTimed) != 0)
    {
        request.invokeFlags.Set(DataModel::InvokeFlags::kTimed);
    }

    const auto status = CodegenDataModelProvider::Instance().InvokeCommand(request, cmdReader, &sCommandHandler);
    // Codegen provider returns nullopt when dispatch completed (including Ember callbacks).
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
