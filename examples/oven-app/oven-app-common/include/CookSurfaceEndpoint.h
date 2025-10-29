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

#include <app-common/zap-generated/cluster-objects.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

namespace chip {
namespace app {
namespace Clusters {
namespace CookSurface {

class CookSurfaceEndpoint
{
public:
    CookSurfaceEndpoint(EndpointId endpointId) : mEndpointId(endpointId) {}

    /**
     * @brief Initialize the CookSurface endpoint.
     *
     * @return returns CHIP_NO_ERROR on success, or an error code on failure.
     */
    CHIP_ERROR Init();

    /**
     * @brief Gets the current On/Off state from server.
     * @param state Reference to store the current On/Off state.
     * @return Returns Status::Success on success, or an error code on failure.
     */

    chip::Protocols::InteractionModel::Status GetOnOffState(bool & state);

    /**
     * @brief Set On/Off state for the CookSurface.
     * @param state Desired On/Off state.
     * @return Returns Status::Success on success, or an error code on failure.
     */
    chip::Protocols::InteractionModel::Status SetOnOffState(bool state);

    EndpointId GetEndpointId() const { return mEndpointId; }

private:
    EndpointId mEndpointId = kInvalidEndpointId;
};

} // namespace CookSurface
} // namespace Clusters
} // namespace app
} // namespace chip
