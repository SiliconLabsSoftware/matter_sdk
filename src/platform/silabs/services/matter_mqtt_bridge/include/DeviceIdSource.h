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

#include "MatterMqttContract.h"

#include <lib/core/CHIPError.h>
#include <lib/core/NodeId.h>

/** Supplies the device id segment embedded in MQTT topic paths (matter/v1/{deviceId}/...). */
class DeviceIdSource
{
public:
    virtual ~DeviceIdSource() = default;

    virtual CHIP_ERROR Refresh()                           = 0;
    virtual const char * GetDeviceId() const                 = 0;
    virtual bool IsKnown() const                             = 0;
};

/** Uses the first commissioned fabric's NodeId formatted as 0x-prefixed hex. */
class FabricNodeIdSource final : public DeviceIdSource
{
public:
    CHIP_ERROR Refresh() override;
    const char * GetDeviceId() const override;
    bool IsKnown() const override;

private:
    char mDeviceId[MATTER_MQTT_DEVICE_ID_LEN] = {};
    bool mKnown                               = false;
};

CHIP_ERROR FormatDeviceId(chip::NodeId nodeId, char * out, size_t outLen);
