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

#include "OvenManager.h"
#include "CookSurfaceEndpoint.h"
#include "CookTopEndpoint.h"
#include "OvenEndpoint.h"
#include "TemperatureControlledCabinetEndpoint.h"

#include <platform/CHIPDeviceLayer.h>

using namespace chip;
using namespace chip::app::Clusters;

OvenManager OvenManager::sOvenMgr;

void OvenManager::Init()
{
    DeviceLayer::PlatformMgr().LockChipStack();
    // Endpoint initializations
    VerifyOrReturn(mOvenEndpoint1.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "OvenEndpoint1 Init failed"));
    VerifyOrReturn(mTemperatureControlledCabinetEndpoint2.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "TemperatureControlledCabinetEndpoint2 Init failed"));
    VerifyOrReturn(mCookTopEndpoint3.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookTopEndpoint3 Init failed"));
    // Temperature Control Delegate set
    TemperatureControl::SetInstance(&mTemperatureControlDelegate);
    VerifyOrReturn(mCookSurfaceEndpoint4.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookSurfaceEndpoint4 Init failed"));
    VerifyOrReturn(mCookSurfaceEndpoint5.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookSurfaceEndpoint5 Init failed"));

    DeviceLayer::PlatformMgr().UnlockChipStack();
}
