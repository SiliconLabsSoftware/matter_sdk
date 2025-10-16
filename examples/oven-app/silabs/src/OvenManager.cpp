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
#include "AppConfig.h"
#include "AppTask.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <lib/support/TimeUtils.h>
#include <platform/CHIPDeviceLayer.h>

#include "OvenEndpoint.h"
#include "CookTopEndpoint.h"
#include "CookSurfaceEndpoint.h"
#include "TemperatureControlledCabinetEndpoint.h"

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::DeviceLayer;

// add tag-lists

OvenManager OvenManager::sOvenMgr;

void OvenManager::Init()
{
    DeviceLayer::PlatformMgr().LockChipStack();
    // Endpoint initializations
    mOvenEndpoint1.Init();
    mTemperatureControlledCabinetEndpoint2.Init();
    mCookTopEndpoint3.Init();
    mCookSurfaceEndpoint4.Init();
    mCookSurfaceEndpoint5.Init();

    DeviceLayer::PlatformMgr().UnlockChipStack();
}


