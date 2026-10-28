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

#include <lib/core/CHIPError.h>
#include <platform/CHIPDeviceEvent.h>
#include <system/SystemClock.h>
#include <system/SystemLayer.h>

namespace chip {

class VendorOTARequestor;

/**
 * Analog of DefaultOTARequestorDriver for HTTPS OTA periodic checks and connectivity.
 * App/config overrides the period via SetPeriodicCheckTimeout (see OTAConfig / sl_matter_ota_config.h).
 */
class VendorOTARequestorDriver
{
public:
    /** Fallback when SetPeriodicCheckTimeout is not called (same as DefaultOTARequestorDriver). */
    static constexpr uint32_t kDefaultPeriodicTimeoutSec = 24 * 60 * 60;

    void Init(VendorOTARequestor * requestor);
    void SetPeriodicCheckTimeout(uint32_t timeoutSec);
    void StartPeriodicCheckTimer();
    void StopPeriodicCheckTimer();

    void UpdateNotFound();
    void UpdateFailed();

private:
    static void PeriodicCheckTimerHandler(System::Layer * layer, void * appState);
    static void OnPlatformEvent(const DeviceLayer::ChipDeviceEvent * event, intptr_t arg);
    void OnPeriodicCheck();

    VendorOTARequestor * mRequestor = nullptr;
    uint32_t mPeriodicTimeoutSec    = kDefaultPeriodicTimeoutSec;
};

} // namespace chip
