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
#include <app/clusters/on-off-server/on-off-server.h>
#include <protocols/interaction_model/StatusCode.h>

using namespace chip;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::CookSurface;

CHIP_ERROR CookSurfaceEndpoint::Init()
{
    bool state = false;
    OnOffServer::Instance().getOnOffValue(mEndpointId, &state);
    return CHIP_NO_ERROR;
}

bool CookSurfaceEndpoint::GetOnOffState()
{
    bool state = false;
    OnOffServer::Instance().getOnOffValue(mEndpointId, &state);
    return state;
}

void CookSurfaceEndpoint::SetOnOffState(bool state)
{
    CommandId commandId = state ? OnOff::Commands::On::Id : OnOff::Commands::Off::Id;
    auto status         = OnOffServer::Instance().setOnOffValue(mEndpointId, commandId, false);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(AppServer, "ERR: updating on/off %x", to_underlying(status));
    }
}
