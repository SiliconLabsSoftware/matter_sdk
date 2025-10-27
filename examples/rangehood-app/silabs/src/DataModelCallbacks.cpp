/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

/**
 * @file
 *   This file implements the handler for data model messages.
 */

#include "AppConfig.h"
#include "AppTask.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <lib/support/logging/CHIPLogging.h>

#include "RangeHoodManager.h"

#include <app-common/zap-generated/attributes/Accessors.h>

using namespace ::chip;
using namespace ::chip::app::Clusters;

void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
{
    ClusterId clusterId     = attributePath.mClusterId;
    AttributeId attributeId = attributePath.mAttributeId;
    EndpointId endpointId   = attributePath.mEndpointId;
    
    ChipLogProgress(Zcl, "Cluster callback: " ChipLogFormatMEI " on endpoint %u", ChipLogValueMEI(clusterId), endpointId);

    switch (clusterId)
    {
    case FanControl::Id:
        // Fan control should only be on FAN_ENDPOINT
        if (endpointId == FAN_ENDPOINT)
        {
            RangehoodMgr().HandleFanControlAttributeChange(attributeId, type, size, value);
        }
        break;
        
    case OnOff::Id:
        // Light on/off control should only be on LIGHT_ENDPOINT
        if (endpointId == LIGHT_ENDPOINT && attributeId == OnOff::Attributes::OnOff::Id)
        {            
            RangehoodMgr().InitiateAction(AppEvent::kEventType_Light, *value ? RangeHoodManager::ON_ACTION : RangeHoodManager::OFF_ACTION,
                                      value);
        }
        break;
        
    case Identify::Id:
        ChipLogProgress(Zcl, "Identify attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u on endpoint %u",
                        ChipLogValueMEI(attributeId), type, *value, size, endpointId);
        break;
        
    default:
        ChipLogProgress(Zcl, "Unhandled cluster " ChipLogFormatMEI " on endpoint %u", ChipLogValueMEI(clusterId), endpointId);
        break;
    }
}
