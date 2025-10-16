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

#include "CookSurfaceEndpoint.h"
#include <app-common/zap-generated/cluster-objects.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/clusters/temperature-control-server/supported-temperature-levels-manager.h>

using namespace chip;
using namespace chip::app::Clusters::CookSurface;
using namespace chip::app::DataModel;
using namespace chip::app::Clusters;

CHIP_ERROR CookSurfaceEndpoint::Init()
{
    ChipLogProgress(AppServer, "CookSurfaceEndpoint::Init() called for endpoint %d", mEndpoint);

    // Temperature Control Delegate set
    TemperatureControl::SetInstance(&mTemperatureControlDelegate);
    ChipLogProgress(AppServer, "Temperature control delegate set for CookSurfaceEndpoint");

    return CHIP_NO_ERROR;
}

void CookSurfaceEndpoint::offCommand()
{
    return;
}
