/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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

#include "ClosureManager.h"
#include "CRTPHelpers.h"

/**
 * @brief CRTP override layer for `ClosureManager`.
 *
 * Each public entry point dispatches to a corresponding `Derived::*Impl()` hook.
 * The default `*Impl()` (declared private below) forwards to `ClosureManager`,
 * so `Derived` only needs to override the hooks whose behavior it wants to
 * customize.
 *
 * Customer code overrides these hooks in `CustomerAppManager`.
 *
 * @tparam Derived The CRTP derived class (`CustomerAppManager`).
 */
template <typename Derived>
class ClosureManagerImpl : public ClosureManager
{
public:
    void Init() { CRTP_OPTIONAL_VOID_DISPATCH(ClosureManagerImpl, Derived, InitImpl); }

    chip::Protocols::InteractionModel::Status OnCalibrateCommand() override
    {
        CRTP_OPTIONAL_DISPATCH(ClosureManagerImpl, Derived, OnCalibrateCommandImpl);
    }

    chip::Protocols::InteractionModel::Status
    OnMoveToCommand(const chip::Optional<chip::app::Clusters::ClosureControl::TargetPositionEnum> position,
                    const chip::Optional<bool> latch,
                    const chip::Optional<chip::app::Clusters::Globals::ThreeLevelAutoEnum> speed) override
    {
        CRTP_OPTIONAL_DISPATCH_ARGS(ClosureManagerImpl, Derived, OnMoveToCommandImpl, position, latch, speed);
    }

    chip::Protocols::InteractionModel::Status OnStopCommand() override
    {
        CRTP_OPTIONAL_DISPATCH(ClosureManagerImpl, Derived, OnStopCommandImpl);
    }

    chip::Protocols::InteractionModel::Status
    OnSetTargetCommand(const chip::Optional<chip::Percent100ths> & position, const chip::Optional<bool> & latch,
                       const chip::Optional<chip::app::Clusters::Globals::ThreeLevelAutoEnum> & speed,
                       const chip::EndpointId endpointId) override
    {
        CRTP_OPTIONAL_DISPATCH_ARGS(ClosureManagerImpl, Derived, OnSetTargetCommandImpl, position, latch, speed, endpointId);
    }

    chip::Protocols::InteractionModel::Status
    OnStepCommand(const chip::app::Clusters::ClosureDimension::StepDirectionEnum & direction, const uint16_t & numberOfSteps,
                  const chip::Optional<chip::app::Clusters::Globals::ThreeLevelAutoEnum> & speed,
                  const chip::EndpointId & endpointId) override
    {
        CRTP_OPTIONAL_DISPATCH_ARGS(ClosureManagerImpl, Derived, OnStepCommandImpl, direction, numberOfSteps, speed, endpointId);
    }

private:
    friend Derived;

    void InitImpl() { ClosureManager::Init(); }

    chip::Protocols::InteractionModel::Status OnCalibrateCommandImpl() { return ClosureManager::OnCalibrateCommand(); }

    chip::Protocols::InteractionModel::Status
    OnMoveToCommandImpl(const chip::Optional<chip::app::Clusters::ClosureControl::TargetPositionEnum> position,
                        const chip::Optional<bool> latch,
                        const chip::Optional<chip::app::Clusters::Globals::ThreeLevelAutoEnum> speed)
    {
        return ClosureManager::OnMoveToCommand(position, latch, speed);
    }

    chip::Protocols::InteractionModel::Status OnStopCommandImpl() { return ClosureManager::OnStopCommand(); }

    chip::Protocols::InteractionModel::Status
    OnSetTargetCommandImpl(const chip::Optional<chip::Percent100ths> & position, const chip::Optional<bool> & latch,
                           const chip::Optional<chip::app::Clusters::Globals::ThreeLevelAutoEnum> & speed,
                           const chip::EndpointId endpointId)
    {
        return ClosureManager::OnSetTargetCommand(position, latch, speed, endpointId);
    }

    chip::Protocols::InteractionModel::Status
    OnStepCommandImpl(const chip::app::Clusters::ClosureDimension::StepDirectionEnum & direction, const uint16_t & numberOfSteps,
                      const chip::Optional<chip::app::Clusters::Globals::ThreeLevelAutoEnum> & speed,
                      const chip::EndpointId & endpointId)
    {
        return ClosureManager::OnStepCommand(direction, numberOfSteps, speed, endpointId);
    }
};
