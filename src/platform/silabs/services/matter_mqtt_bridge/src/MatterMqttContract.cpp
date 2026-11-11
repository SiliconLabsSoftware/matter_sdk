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

#include <cinttypes>
#include <cstdio>

// MQTT device id: fixed-width 0x + 16 hex digits (64-bit NodeId).
CHIP_ERROR FormatDeviceId(chip::NodeId nodeId, char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr && outLen >= MATTER_MQTT_DEVICE_ID_LEN, CHIP_ERROR_BUFFER_TOO_SMALL);

    const uint32_t high = static_cast<uint32_t>(nodeId >> 32);
    const uint32_t low  = static_cast<uint32_t>(nodeId & 0xffffffffULL);
    const int n         = snprintf(out, outLen, "0x%08" PRIX32 "%08" PRIX32, high, low);
    VerifyOrReturnError(n >= 0 && static_cast<size_t>(n) < outLen, CHIP_ERROR_BUFFER_TOO_SMALL);
    return CHIP_NO_ERROR;
}
