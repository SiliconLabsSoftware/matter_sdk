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

#include "vendor_ota_matter_gate.h"

#include <app/clusters/ota-requestor/OTARequestorInterface.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/OTAImageProcessor.h>
#include <platform/silabs/SilabsConfig.h>

using chip::SelectedTimer;
using chip::DeviceLayer::Internal::SilabsConfig;

namespace chip {

using OTAUpdateStateEnum = OTARequestorInterface::OTAUpdateStateEnum;

void SilabsOTARequestorDriver::SetVendorOtaGate(bool active)
{
    mVendorOtaGate = active;
    if (active)
    {
        StopPeriodicQueryTimer();
    }
    else
    {
        RekickPeriodicQueryTimer();
    }
}

void SilabsOTARequestorDriver::ProcessAnnounceOTAProviders(
    const ProviderLocationType & providerLocation,
    app::Clusters::OtaSoftwareUpdateRequestor::AnnouncementReasonEnum announcementReason)
{
    if (mVendorOtaGate)
    {
        ChipLogProgress(SoftwareUpdate, "Ignoring AnnounceOTAProvider; HTTPS OTA gated");
        return;
    }
    DefaultOTARequestorDriver::ProcessAnnounceOTAProviders(providerLocation, announcementReason);
}

void SilabsOTARequestorDriver::SendQueryImage()
{
    if (mVendorOtaGate)
    {
        ChipLogProgress(SoftwareUpdate, "Skipping QueryImage; HTTPS OTA gated");
        return;
    }
    DefaultOTARequestorDriver::SendQueryImage();
}

CHIP_ERROR SilabsOTARequestorDriver::UpdateNotFound(UpdateNotFoundReason reason, System::Clock::Seconds32 delay)
{
    ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] Matter UpdateNotFound reason=%u", static_cast<unsigned>(reason));
    return DefaultOTARequestorDriver::UpdateNotFound(reason, delay);
}

void SilabsOTARequestorDriver::Init(OTARequestorInterface * requestor, OTAImageProcessorInterface * processor)
{
    mRequestor                = requestor;
    mImageProcessor           = processor;
    mProviderRetryCount       = 0;
    mInvalidSessionRetryCount = 0;

    const bool firstRun = mImageProcessor->IsFirstImageRun();
    const auto state    = mRequestor->GetCurrentUpdateState();

    if (firstRun)
    {
        TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().ScheduleLambda([this] {
            CHIP_ERROR error = mImageProcessor->ConfirmCurrentImage();
            if (error != CHIP_NO_ERROR)
            {
                ChipLogError(SoftwareUpdate, "Failed to confirm image: %" CHIP_ERROR_FORMAT, error.Format());
                mRequestor->Reset();
                return;
            }

            bool skipNotify = false;
            CHIP_ERROR skipErr =
                SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_HttpsOtaSkipNotify, skipNotify);
            if ((skipErr == CHIP_NO_ERROR) && skipNotify)
            {
                TEMPORARY_RETURN_IGNORED SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_HttpsOtaSkipNotify, false);
                ChipLogProgress(SoftwareUpdate, "HTTPS OTA: skipped NotifyUpdateApplied");
                // Matter path leaves kApplying until NotifyUpdateApplied / Reset. HTTPS has no
                // provider notify, so clear shared requestor state so announces are accepted.
                mRequestor->Reset();
            }
            else if (mSendNotifyUpdateApplied)
            {
                TEMPORARY_RETURN_IGNORED mRequestor->NotifyUpdateApplied();
            }
        });
    }
    else if (state != OTAUpdateStateEnum::kIdle)
    {
        mRequestor->Reset();
    }
    else
    {
        StartSelectedTimer(SelectedTimer::kPeriodicQueryTimer);
    }
}

} // namespace chip
