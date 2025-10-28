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

#include <app/clusters/on-off-server/on-off-server.h>
#include <lib/core/CHIPError.h>

namespace chip {
namespace app {
namespace Clusters {
namespace Light {

class LightEndpoint
{
public:
    LightEndpoint() {}

    /**
     * @brief Initialize the Light endpoint.
     */
    CHIP_ERROR Init();

    /**
     * @brief Handle the "on/off" command for the Light.
     */
    bool GetOnOffState();

    /**
     * @brief Set On/Off state for the Light.
     */
    void SetOnOffState(bool state);

private:
    EndpointId mEndpointId = kInvalidEndpointId;
};

} // namespace Light
} // namespace Clusters
} // namespace app
} // namespace chip
