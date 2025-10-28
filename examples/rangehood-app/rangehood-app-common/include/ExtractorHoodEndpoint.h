/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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

#include <stdbool.h>
#include <stdint.h>

#include <app/clusters/fan-control-server/fan-control-delegate.h>
#include <app/clusters/fan-control-server/fan-control-server.h>
#include <lib/core/CHIPError.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters::FanControl;
using Protocols::InteractionModel::Status;

namespace chip {
namespace app {
namespace Clusters {
namespace FanDelegate {
class FanDelegate : public FanControl::Delegate
{
public:
    /**
     * @brief
     *   This method handles the step command. This will happen as fast as possible.
     *
     *   @param[in]  aDirection     the direction in which the speed should step
     *   @param[in]  aWrap          whether the speed should wrap or not
     *   @param[in]  aLowestOff     whether the device should consider the lowest setting as off
     *
     *   @return Success On success.
     *   @return Other Value indicating it failed to execute the command.
     */
    Status HandleStep(StepDirectionEnum aDirection, bool aWrap, bool aLowestOff) override;

    FanDelegate(EndpointId aEndpoint) : mEndpoint(aEndpoint) {}

protected:
    EndpointId mEndpoint = 0;
};
} // namespace FanDelegate
namespace ExtractorHood {
class ExtractorHoodEndpoint
{
public:
    ExtractorHoodEndpoint(EndpointId endpointId) : mEndpointId(endpointId) {}

    /**
     * @brief Initialize the ExtractorHood endpoint.
     */
    CHIP_ERROR Init();

    /**
     * @brief Get the FanDelegate instance for this endpoint.
     */
    FanDelegate & GetFanDelegate() { return mFanDelegate; }

    /* Add ExtractorHoodEndpoint functions*/

private:
    EndpointId mEndpointId = kInvalidEndpointId;
    FanDelegate mFanDelegate;
};

} // namespace ExtractorHood
} // namespace Clusters
} // namespace app
} // namespace chip
