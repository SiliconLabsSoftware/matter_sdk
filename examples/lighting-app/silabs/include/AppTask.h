/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

/**********************************************************
 * Includes
 *********************************************************/

#include "AppEvent.h"
#include "BaseApplication.h"

#include <lib/core/CHIPError.h>

namespace chip {
namespace app {
class DeferredAttributePersistenceProvider;
} // namespace app
} // namespace chip

/**********************************************************
 * AppTask Declaration
 *
 * Platform entry: task loop, singleton accessor, and startup. Example-specific
 * lighting logic and state live in LightingAppTask (see AppTaskImpl.h).
 *********************************************************/

class AppTask : public BaseApplication
{

public:
    AppTask() = default;

    static AppTask & GetAppTask();

    /**
     * @brief AppTask task main loop function
     *
     * @param pvParameter FreeRTOS task parameter
     */
    static void AppTaskMain(void * pvParameter);

    CHIP_ERROR StartAppTask();

protected:
    chip::app::DeferredAttributePersistenceProvider * pDeferredAttributePersister = nullptr;
};
