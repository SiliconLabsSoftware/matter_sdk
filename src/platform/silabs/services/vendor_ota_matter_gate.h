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

#include <app-common/zap-generated/cluster-enums.h>
#include <app/clusters/ota-requestor/DefaultOTARequestorDriver.h>

namespace chip {

/**
 * Matter OTA requestor driver that can be gated while vendor HTTPS OTA owns the processor.
 * Also skips NotifyUpdateApplied when an HTTPS apply set kConfigKey_HttpsOtaSkipNotify.
 */
class SilabsOTARequestorDriver : public DeviceLayer::DefaultOTARequestorDriver
{
public:
    void SetVendorOtaGate(bool active);

    void ProcessAnnounceOTAProviders(
        const ProviderLocationType & providerLocation,
        app::Clusters::OtaSoftwareUpdateRequestor::AnnouncementReasonEnum announcementReason) override;
    void SendQueryImage() override;
    CHIP_ERROR UpdateNotFound(UpdateNotFoundReason reason, System::Clock::Seconds32 delay) override;
    void Init(OTARequestorInterface * requestor, OTAImageProcessorInterface * processor);

private:
    bool mVendorOtaGate = false;
};

class VendorOtaMatterGate
{
public:
    void Init(SilabsOTARequestorDriver & driver) { mDriver = &driver; }

    void SetActive(bool active)
    {
        if (mDriver != nullptr)
        {
            mDriver->SetVendorOtaGate(active);
        }
    }

private:
    SilabsOTARequestorDriver * mDriver = nullptr;
};

} // namespace chip
