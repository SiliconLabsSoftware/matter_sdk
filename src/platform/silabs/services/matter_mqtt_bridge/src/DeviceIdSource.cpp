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

#include "DeviceIdSource.h"

#include <lib/support/CodeUtils.h>
#include <server/Server.h>

// DeviceIdSource implementations for MQTT topic paths.

// Cached 0x{high}{low} string embedded after matter/v1/ in every bridge topic.
CHIP_ERROR FabricNodeIdSource::Refresh()
{
    chip::NodeId nodeId = chip::kUndefinedNodeId;
    for (const chip::FabricInfo & fabric : chip::Server::GetInstance().GetFabricTable())
    {
        nodeId = fabric.GetNodeId();
        break;
    }
    VerifyOrReturnError(nodeId != chip::kUndefinedNodeId, CHIP_ERROR_INCORRECT_STATE);
    ReturnErrorOnFailure(FormatDeviceId(nodeId, mDeviceId, sizeof(mDeviceId)));
    mKnown = true;
    return CHIP_NO_ERROR;
}

const char * FabricNodeIdSource::GetDeviceId() const
{
    return mDeviceId;
}

bool FabricNodeIdSource::IsKnown() const
{
    return mKnown;
}
