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

#include "AppTaskBase.h"

#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>

namespace chip::app::AllDevices {

// Initializes Silabs provision storage reader and points the credential providers at it.
// Uses the example credentials if CONFIG_CHIP_FACTORY_DATA is not set.
CHIP_ERROR InitSilabsCredentials();

// Supplies the initialized Thread instance and locks, bypasses Zephyr's networking stack.
// No-op when native Thread endpoints are disabled.
CHIP_ERROR ConfigureOpenThreadEndpointParams(ServerInitParams & initParams);

template <class Derived>
class SilabsAppTask : public AppTaskBase<Derived>
{
public:
    CHIP_ERROR InitCredentials() { return InitSilabsCredentials(); }

    CHIP_ERROR InitThreadNetworking()
    {
        ReturnErrorOnFailure(AppTaskBase<Derived>::InitThreadNetworking());
        if constexpr(CHIP_SYSTEM_CONFIG_USE_OPENTHREAD_ENDPOINT && CHIP_DEVICE_CONFIG_ENABLE_THREAD){
            ReturnErrorOnFailure(ConfigureOpenThreadEndpointParams(this->mInitParams));
        }
        return CHIP_NO_ERROR;
    }
};

class AppTask final : public SilabsAppTask<AppTask>
{
};

} // namespace chip::app::AllDevices
