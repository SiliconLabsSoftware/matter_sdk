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

#include "MatterMqttAttributeListener.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

void MatterMqttAttributeListener::Configure(const MatterMqttBridgeConfig & config, ReportHandler handler)
{
    mConfig  = &config;
    mHandler = handler;
}

void MatterMqttAttributeListener::OnAttributeChanged(const chip::app::ConcreteAttributePath & path,
                                                     chip::app::DataModel::AttributeChangeType type)
{
    // Entry for Matter->MQTT reports: profile ACL, then EnqueueReport (read + MQTT thread publish).
    // Duplicate publishes for overlapping clusters are blocked by DeviceMqttProfile::allowsPublish and adapter tables.
    if (type == chip::app::DataModel::AttributeChangeType::kQuiet)
    {
        return;
    }

    VerifyOrReturn(mConfig != nullptr && mHandler != nullptr);
    VerifyOrReturn(mConfig->profile != nullptr && mConfig->profile->allowsPublish != nullptr);
    if (!mConfig->profile->allowsPublish(path.mEndpointId, path.mClusterId, path.mAttributeId))
    {
        return;
    }

    if (mHandler(*mConfig, path) != CHIP_NO_ERROR)
    {
        ChipLogDetail(AppServer, "MatterMqttBridge: dropped report for ep %u cluster 0x%x attr 0x%x",
                      static_cast<unsigned>(path.mEndpointId), static_cast<unsigned>(path.mClusterId),
                      static_cast<unsigned>(path.mAttributeId));
    }
}
