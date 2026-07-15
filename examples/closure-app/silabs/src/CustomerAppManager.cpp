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

/**
 * @file
 * @brief Customer-facing ClosureManager definition site.
 *
 * Add `*Impl()` overrides in `CustomerAppManager` (declared in
 * CustomerAppManager.h) to customize individual ClosureManager behaviors.
 * Any `*Impl()` you do not override keeps the default ClosureManager behavior.
 *
 * Overridable hooks (declared in ClosureManagerImpl.h):
 *   - InitImpl()
 *   - OnCalibrateCommandImpl()
 *   - OnMoveToCommandImpl(position, latch, speed)
 *   - OnStopCommandImpl()
 *   - OnSetTargetCommandImpl(position, latch, speed, endpointId)
 *   - OnStepCommandImpl(direction, numberOfSteps, speed, endpointId)
 *   - HandleClosureActionCompleteImpl(action)
 *   - HandleClosureMotionActionImpl()
 *   - HandleClosureUnlatchActionImpl()
 *   - HandlePanelSetTargetActionImpl(endpointId)
 *   - HandlePanelUnlatchActionImpl(endpointId)
 *   - HandlePanelStepActionImpl(endpointId)
 *   - GetPanelNextPositionImpl(currentState, targetState, nextPosition)
 *
 * Out-of-line override bodies belong here.
 */

#include "CustomerAppManager.h"

CustomerAppManager CustomerAppManager::sInstance;

ClosureManager & ClosureManager::GetInstance()
{
    return CustomerAppManager::GetInstance();
}
