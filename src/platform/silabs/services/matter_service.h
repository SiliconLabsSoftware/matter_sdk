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

#include <lib/core/CHIPError.h>

namespace chip {
namespace DeviceLayer {
namespace Silabs {

/**
 * @brief Common lifecycle for Matter platform services (HTTP, MQTT, ...).
 *
 * Concrete services implement Start / Stop without extra parameters so the app
 * can drive them through a uniform interface.
 */
class MatterService
{
public:
    virtual ~MatterService() = default;

    /**
     * @brief Start the service (e.g. spawn a worker thread or open a connection).
     *
     * @retval CHIP_NO_ERROR Success or CHIP_ERROR as defined by the implementation.
     */
    virtual CHIP_ERROR Start() = 0;

    /**
     * @brief Stop the service and release runtime resources owned by Start.
     *
     * Safe to call when the service is not running.
     *
     * @retval CHIP_NO_ERROR Success or CHIP_ERROR as defined by the implementation.
     */
    virtual CHIP_ERROR Stop() = 0;

    /**
     * @brief Whether the service is currently running after a successful Start.
     */
    virtual bool IsRunning() const = 0;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
