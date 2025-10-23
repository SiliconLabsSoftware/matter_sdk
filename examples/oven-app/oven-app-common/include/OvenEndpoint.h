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
namespace Oven {

/**
 * @brief Base oven endpoint placeholder.
 */
class OvenEndpoint
{
public:
    OvenEndpoint(EndpointId endpointId) {}

    /**
     * @brief Initialize the oven endpoint.
     *
     * @return CHIP_ERROR indicating success or failure of the initialization.
     */
    CHIP_ERROR Init();
};

} // namespace Oven
} // namespace Clusters
} // namespace app
} // namespace chip
