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

#include "AppTaskImpl.h"

/**
 * @brief Minimal AppTaskImpl-derived class that overrides only ButtonEventHandler.
 *
 * Use this as a template when you need custom button behavior; override
 * ButtonEventHandlerImpl() and add AppInitImpl() / GetAppTask() / sAppTask
 * as required by the CRTP base.
 */
class CommonAppTask : public AppTaskImpl<CommonAppTask>
{
public:
    static CommonAppTask & GetAppTask() { return sAppTask; }

private:
    friend class AppTaskImpl<CommonAppTask>;

    CHIP_ERROR AppInitImpl();
    void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction);

    static CommonAppTask sAppTask;
};
