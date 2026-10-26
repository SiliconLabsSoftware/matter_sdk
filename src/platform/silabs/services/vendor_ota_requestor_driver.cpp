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

#include "vendor_ota_requestor_driver.h"
#include "https_downloader.h"
#include "vendor_ota_requestor.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <cinttypes>
#include <cstring>

namespace chip {

void VendorOTARequestorDriver::Init(VendorOTARequestor * requestor)
{
    mRequestor = requestor;

    ChipLogProgress(SoftwareUpdate, "[DEBUG-HTTPS-OTA] VendorOTARequestorDriver::Init timeout=%" PRIu32 "s", mPeriodicTimeoutSec);

    // Same role as DefaultOTARequestorDriver::Init starting the periodic query timer.
    StartPeriodicCheckTimer();

    // Matter ConnectivityManager has no dedicated callback API; PlatformMgr events are the
    // supported hook (same pattern as BaseApplication / AWS init).
    TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().AddEventHandler(OnPlatformEvent, reinterpret_cast<intptr_t>(this));
}

void VendorOTARequestorDriver::SetPeriodicCheckTimeout(uint32_t timeoutSec)
{
    if (timeoutSec != 0)
    {
        mPeriodicTimeoutSec = timeoutSec;
    }
}

void VendorOTARequestorDriver::StartPeriodicCheckTimer()
{
    StopPeriodicCheckTimer();
    ChipLogProgress(SoftwareUpdate, "[DEBUG-HTTPS-OTA] StartPeriodicCheckTimer timeout=%" PRIu32 "s", mPeriodicTimeoutSec);
    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().StartTimer(System::Clock::Seconds32(mPeriodicTimeoutSec),
                                                                   PeriodicCheckTimerHandler, this);
}

void VendorOTARequestorDriver::StopPeriodicCheckTimer()
{
    DeviceLayer::SystemLayer().CancelTimer(PeriodicCheckTimerHandler, this);
}

void VendorOTARequestorDriver::PeriodicCheckTimerHandler(System::Layer * /* layer */, void * appState)
{
    static_cast<VendorOTARequestorDriver *>(appState)->OnPeriodicCheck();
}

void VendorOTARequestorDriver::OnPlatformEvent(const DeviceLayer::ChipDeviceEvent * event, intptr_t arg)
{
    auto * self = reinterpret_cast<VendorOTARequestorDriver *>(arg);
    VerifyOrReturn(self != nullptr && self->mRequestor != nullptr);
    VerifyOrReturn(event->Type == DeviceLayer::DeviceEventType::kInternetConnectivityChange);

    const bool established = (event->InternetConnectivityChange.IPv4 == DeviceLayer::kConnectivity_Established) ||
        (event->InternetConnectivityChange.IPv6 == DeviceLayer::kConnectivity_Established);
    ChipLogProgress(SoftwareUpdate, "[DEBUG-HTTPS-OTA] OnPlatformEvent connectivity established=%d", established ? 1 : 0);
    self->mRequestor->OnWiFiConnectivityChanged(established);
}

void VendorOTARequestorDriver::OnPeriodicCheck()
{
    VerifyOrReturn(mRequestor != nullptr);

    char urlBuf[HttpsDownloader::kMaxUrlLen];
    MutableCharSpan url(urlBuf);
    CHIP_ERROR     err = mRequestor->GetDefaultUrl(url);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] periodic re-arm: no URL");
        StartPeriodicCheckTimer();
        return;
    }

    // Copy into a durable buffer for StartUpdate (urlBuf is stack).
    char sessionUrl[HttpsDownloader::kMaxUrlLen];
    VerifyOrReturn(url.size() < sizeof(sessionUrl));
    memcpy(sessionUrl, url.data(), url.size());
    sessionUrl[url.size()] = '\0';

    ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] session start source=periodic url=%.*s", static_cast<int>(url.size()),
                    sessionUrl);

    err = mRequestor->StartUpdate(CharSpan(sessionUrl, url.size()));
    if ((err == CHIP_ERROR_BUSY) || (err != CHIP_NO_ERROR))
    {
        if (err == CHIP_ERROR_BUSY)
        {
            ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] periodic re-arm: busy");
        }
        else
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] periodic re-arm: start failed %" CHIP_ERROR_FORMAT, err.Format());
        }
        StartPeriodicCheckTimer();
    }
}

void VendorOTARequestorDriver::UpdateNotFound()
{
    StartPeriodicCheckTimer();
}

void VendorOTARequestorDriver::UpdateFailed()
{
    StartPeriodicCheckTimer();
}

void VendorOTARequestorDriver::UpdateDownloaded()
{
    StartPeriodicCheckTimer();
}

} // namespace chip
