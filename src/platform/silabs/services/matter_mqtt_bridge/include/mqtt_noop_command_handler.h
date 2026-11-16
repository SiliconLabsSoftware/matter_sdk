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

#pragma once

#include <access/SubjectDescriptor.h>
#include <app/CommandHandler.h>
#include <app/ConcreteCommandPath.h>
#include <app/data-model-provider/OperationTypes.h>
#include <lib/core/CHIPError.h>
#include <messaging/ExchangeContext.h>
#include <protocols/interaction_model/StatusCode.h>

/**
 * Stub Interaction Model command handler for MQTT-initiated InvokeCommand.
 * Ember dispatch calls AddStatus on the handler even though there is no IM exchange.
 */
class MqttNoopCommandHandler : public chip::app::CommandHandler
{
public:
    CHIP_ERROR FallibleAddStatus(const chip::app::ConcreteCommandPath & aRequestCommandPath,
                                 const chip::Protocols::InteractionModel::ClusterStatusCode & aStatus,
                                 const char * context = nullptr) override
    {
        return CHIP_NO_ERROR;
    }

    void AddStatus(const chip::app::ConcreteCommandPath & aRequestCommandPath,
                   const chip::Protocols::InteractionModel::ClusterStatusCode & aStatus,
                   const char * context = nullptr) override
    {}

    chip::FabricIndex GetAccessingFabricIndex() const override { return chip::kUndefinedFabricIndex; }

    CHIP_ERROR AddResponseData(const chip::app::ConcreteCommandPath & aRequestCommandPath, chip::CommandId aResponseCommandId,
                               const chip::app::DataModel::EncodableToTLV & aEncodable) override
    {
        return CHIP_NO_ERROR;
    }

    void AddResponse(const chip::app::ConcreteCommandPath & aRequestCommandPath, chip::CommandId aResponseCommandId,
                     const chip::app::DataModel::EncodableToTLV & aEncodable) override
    {}

    bool IsTimedInvoke() const override { return false; }

    void FlushAcksRightAwayOnSlowCommand() override {}

    chip::Access::SubjectDescriptor GetSubjectDescriptor() const override { return chip::Access::SubjectDescriptor(); }

    chip::Messaging::ExchangeContext * GetExchangeContext() const override { return nullptr; }
};
