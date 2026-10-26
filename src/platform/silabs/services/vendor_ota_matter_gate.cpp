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
#include <lib/support/TypeTraits.h>
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
    ChipLogProgress(SoftwareUpdate, "[DEBUG-HTTPS-OTA] SetVendorOtaGate active=%d", active ? 1 : 0);
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
    ChipLogProgress(SoftwareUpdate, "[DEBUG-MATTER-OTA] ProcessAnnounceOTAProviders gate=%d", mVendorOtaGate ? 1 : 0);
    if (mVendorOtaGate)
    {
        ChipLogProgress(SoftwareUpdate, "Ignoring AnnounceOTAProvider; HTTPS OTA gated");
        return;
    }
    DefaultOTARequestorDriver::ProcessAnnounceOTAProviders(providerLocation, announcementReason);
}

void SilabsOTARequestorDriver::SendQueryImage()
{
    ChipLogProgress(SoftwareUpdate, "[DEBUG-MATTER-OTA] SilabsOTARequestorDriver::SendQueryImage gate=%d", mVendorOtaGate ? 1 : 0);
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
    ChipLogProgress(SoftwareUpdate, "[DEBUG-MATTER-OTA] SilabsOTARequestorDriver::Init IsFirstImageRun=%d state=%u", firstRun ? 1 : 0,
                    to_underlying(state));

    if (firstRun)
    {
        ChipLogProgress(SoftwareUpdate, "[DEBUG-MATTER-OTA] SilabsOTARequestorDriver::Init branch=confirm");
        TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().ScheduleLambda([this] {
            CHIP_ERROR error = mImageProcessor->ConfirmCurrentImage();
            ChipLogProgress(SoftwareUpdate, "[DEBUG-MATTER-OTA] ConfirmCurrentImage=%" CHIP_ERROR_FORMAT, error.Format());
            if (error != CHIP_NO_ERROR)
            {
                ChipLogError(SoftwareUpdate, "Failed to confirm image: %" CHIP_ERROR_FORMAT, error.Format());
                mRequestor->Reset();
                return;
            }

            bool skipNotify = false;
            CHIP_ERROR skipErr =
                SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_HttpsOtaSkipNotify, skipNotify);
            ChipLogProgress(SoftwareUpdate, "[DEBUG-HTTPS-OTA] HttpsOtaSkipNotify read=%" CHIP_ERROR_FORMAT " value=%d",
                            skipErr.Format(), skipNotify ? 1 : 0);
            if ((skipErr == CHIP_NO_ERROR) && skipNotify)
            {
                TEMPORARY_RETURN_IGNORED SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_HttpsOtaSkipNotify, false);
                ChipLogProgress(SoftwareUpdate, "HTTPS OTA: skipped NotifyUpdateApplied");
            }
            else if (mSendNotifyUpdateApplied)
            {
                TEMPORARY_RETURN_IGNORED mRequestor->NotifyUpdateApplied();
            }
        });
    }
    else if (state != OTAUpdateStateEnum::kIdle)
    {
        ChipLogProgress(SoftwareUpdate, "[DEBUG-MATTER-OTA] SilabsOTARequestorDriver::Init branch=reset");
        mRequestor->Reset();
    }
    else
    {
        ChipLogProgress(SoftwareUpdate, "[DEBUG-MATTER-OTA] SilabsOTARequestorDriver::Init branch=periodic");
        StartSelectedTimer(SelectedTimer::kPeriodicQueryTimer);
    }
}

} // namespace chip
