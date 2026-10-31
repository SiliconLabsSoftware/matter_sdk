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

#include "vendor_ota_requestor.h"
#include "http_client.h"

#include <app/CommandHandler.h>
#include <app/clusters/ota-requestor/CodegenIntegration.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/SilabsConfig.h>
#include <protocols/interaction_model/StatusCode.h>

#include <cinttypes>

using namespace chip::app::Clusters::OtaSoftwareUpdateRequestor;
using chip::app::DataModel::Nullable;
using chip::app::DataModel::NullNullable;
using chip::DeviceLayer::Internal::SilabsConfig;
using chip::Protocols::InteractionModel::Status;

namespace chip {
namespace {

VendorOTARequestor gVendorOTARequestor;

} // namespace

VendorOTARequestor & VendorOTARequestor::GetInstance()
{
    return gVendorOTARequestor;
}

const char * VendorOTARequestor::LastResultReasonToString(LastResultReason reason)
{
    switch (reason)
    {
    case LastResultReason::kOk:
        return "ok";
    case LastResultReason::kAborted:
        return "aborted";
    case LastResultReason::kVersionMismatch:
        return "version-mismatch";
    case LastResultReason::kStall:
        return "stall";
    case LastResultReason::kBusy:
        return "busy";
    case LastResultReason::kHttp:
        return "http";
    case LastResultReason::kNoUrl:
        return "no-url";
    case LastResultReason::kClientStart:
        return "client-start";
    }
    return "unknown";
}

void VendorOTARequestor::SetLastResult(CHIP_ERROR err, LastResultReason reason)
{
    mLastResultError  = err;
    mLastResultReason = reason;
}

CHIP_ERROR VendorOTARequestor::GetLastResult(CHIP_ERROR & err, LastResultReason & reason) const
{
    err    = mLastResultError;
    reason = mLastResultReason;
    return CHIP_NO_ERROR;
}

void VendorOTARequestor::Init(HttpsDownloader & httpsDownloader,
                              DeviceLayer::Silabs::MultiOTA::OTAMultiImageProcessorImpl & imageProcessor,
                              OTARequestorInterface & matterRequestor, OTADownloader & bdxDownloader,
                              DefaultOTARequestorStorage & matterStorage, VendorOtaMatterGate & gate,
                              VendorOTARequestorDriver & vendorDriver)
{
    mHttpsDownloader = &httpsDownloader;
    mImageProcessor  = &imageProcessor;
    mMatterRequestor = &matterRequestor;
    mBdxDownloader   = &bdxDownloader;
    mMatterStorage   = &matterStorage;
    mGate            = &gate;
    mVendorDriver    = &vendorDriver;

    mHttpsDownloader->SetStateDelegate(this);
    mHttpsDownloader->SetTimeout(HttpsDownloader::kDefaultTimeout);
    mHttpsDownloader->SetBlockSize(HttpsDownloader::kDefaultBlockSize);
    ApplyBindings();
}

void VendorOTARequestor::ApplyBindings()
{
    VerifyOrReturn(mHttpsDownloader != nullptr);
    if (mHttpClient != nullptr)
    {
        mHttpsDownloader->SetHttpClient(mHttpClient);
    }
    if (mAppDelegate != nullptr)
    {
        mHttpsDownloader->SetUriComposer(mAppDelegate);
    }
}

void VendorOTARequestor::SetAppDelegate(AppDelegate * delegate)
{
    mAppDelegate = delegate;
    ApplyBindings();
}

void VendorOTARequestor::SetHttpClient(DeviceLayer::Silabs::HttpClient * client)
{
    mHttpClient = client;
    ApplyBindings();
}

CHIP_ERROR VendorOTARequestor::GetPeriodicUrl(MutableCharSpan & outUrl)
{
    VerifyOrReturnError(mAppDelegate != nullptr, CHIP_ERROR_NOT_FOUND);
    return mAppDelegate->GetPeriodicUrl(outUrl);
}

bool VendorOTARequestor::IsMatterIdle() const
{
    VerifyOrReturnValue(mMatterRequestor != nullptr, false);
    return mMatterRequestor->GetCurrentUpdateState() == OTAUpdateStateEnum::kIdle;
}

CHIP_ERROR VendorOTARequestor::TakeOwnership()
{
    VerifyOrReturnError(mGate != nullptr && mImageProcessor != nullptr && mHttpsDownloader != nullptr &&
                            mBdxDownloader != nullptr && mMatterRequestor != nullptr,
                        CHIP_ERROR_INCORRECT_STATE);

    mGate->SetActive(true);
    SetRequestorInstance(this);
    mImageProcessor->SetOTADownloader(mHttpsDownloader);
    mHttpsDownloader->SetImageProcessorDelegate(mImageProcessor);
    mActive = true;
    return CHIP_NO_ERROR;
}

void VendorOTARequestor::RestoreMatterOwnership()
{
    if ((mImageProcessor != nullptr) && (mBdxDownloader != nullptr))
    {
        mImageProcessor->SetOTADownloader(mBdxDownloader);
        mBdxDownloader->SetImageProcessorDelegate(mImageProcessor);
    }

    if (mMatterRequestor != nullptr)
    {
        SetRequestorInstance(mMatterRequestor);
    }

    if (mGate != nullptr)
    {
        mGate->SetActive(false);
    }

    mActive      = false;
    mUpdateState = OTAUpdateStateEnum::kIdle;
    mProgress    = NullNullable;
}

void VendorOTARequestor::RecordState(OTAUpdateStateEnum state, OTAChangeReasonEnum reason)
{
    mUpdateState = state;
    Nullable<uint32_t> target;
    if ((state == OTAUpdateStateEnum::kDownloading) || (state == OTAUpdateStateEnum::kApplying))
    {
        target.SetNonNull(mTargetVersion);
    }

    GetOTARequestorAttributes().SetUpdateState(state, reason, target);
    if (state != OTAUpdateStateEnum::kDownloading)
    {
        mProgress = NullNullable;
        TEMPORARY_RETURN_IGNORED GetOTARequestorAttributes().SetUpdateStateProgress(NullNullable);
    }

    if (mMatterStorage != nullptr)
    {
        TEMPORARY_RETURN_IGNORED mMatterStorage->StoreCurrentUpdateState(state);
    }
}

CHIP_ERROR VendorOTARequestor::PersistApplyingForConfirm(uint32_t targetVersion)
{
    VerifyOrReturnError(mMatterStorage != nullptr, CHIP_ERROR_INCORRECT_STATE);
    mTargetVersion = targetVersion;
    ReturnErrorOnFailure(mMatterStorage->StoreTargetVersion(targetVersion));
    RecordState(OTAUpdateStateEnum::kApplying, OTAChangeReasonEnum::kSuccess);
    return SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_HttpsOtaSkipNotify, true);
}

CHIP_ERROR VendorOTARequestor::StartUpdate(CharSpan httpsUrl)
{
    // 1. Reject if a HTTPS session is already active or Matter BDX is not idle.
    if (mActive)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] StartUpdate: session already active");
        return CHIP_ERROR_BUSY;
    }
    // Uninitialized VendorOTARequestor (OTAConfig::Init not run) must not be reported as busy.
    if ((mHttpsDownloader == nullptr) || (mMatterRequestor == nullptr))
    {
        SetLastResult(CHIP_ERROR_INCORRECT_STATE, LastResultReason::kClientStart);
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL not-initialized 0x%08" PRIX32,
                     CHIP_ERROR_INCORRECT_STATE.AsInteger());
        return CHIP_ERROR_INCORRECT_STATE;
    }
    if (!IsMatterIdle())
    {
        SetLastResult(CHIP_ERROR_BUSY, LastResultReason::kBusy);
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL busy 0x%08" PRIX32, CHIP_ERROR_BUSY.AsInteger());
        return CHIP_ERROR_BUSY;
    }

    // 2. Require a running HttpClient bound to the downloader.
    auto * client = mHttpsDownloader->GetHttpClient();
    if ((client == nullptr) || !client->IsRunning())
    {
        SetLastResult(CHIP_ERROR_INCORRECT_STATE, LastResultReason::kClientStart);
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL client-start 0x%08" PRIX32,
                     CHIP_ERROR_INCORRECT_STATE.AsInteger());
        return CHIP_ERROR_INCORRECT_STATE;
    }

    if (mVendorDriver != nullptr)
    {
        mVendorDriver->StopPeriodicCheckTimer();
    }

    // 3. Take OTA ownership, set the session URL, and begin prepare/download.
    CHIP_ERROR err = TakeOwnership();
    if (err != CHIP_NO_ERROR)
    {
        SetLastResult(err, LastResultReason::kHttp);
        if (mVendorDriver != nullptr)
        {
            mVendorDriver->UpdateFailed();
        }
        return err;
    }

    err = mHttpsDownloader->SetImageURI(httpsUrl);
    if (err != CHIP_NO_ERROR)
    {
        RestoreMatterOwnership();
        SetLastResult(err, LastResultReason::kNoUrl);
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL no-url 0x%08" PRIX32, err.AsInteger());
        if (mVendorDriver != nullptr)
        {
            mVendorDriver->UpdateFailed();
        }
        return err;
    }

    mTargetVersion = 0;
    RecordState(OTAUpdateStateEnum::kDownloading, OTAChangeReasonEnum::kSuccess);

    err = mHttpsDownloader->BeginPrepareDownload();
    if (err != CHIP_NO_ERROR)
    {
        RestoreMatterOwnership();
        SetLastResult(err, LastResultReason::kHttp);
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL http 0x%08" PRIX32, err.AsInteger());
        if (mVendorDriver != nullptr)
        {
            mVendorDriver->UpdateFailed();
        }
        return err;
    }

    return CHIP_NO_ERROR;
}

void VendorOTARequestor::CancelImageUpdate()
{
    // No-op when HTTPS does not own the processor (avoids aborting Matter BDX).
    VerifyOrReturn(mActive);

    if (mHttpsDownloader != nullptr)
    {
        mHttpsDownloader->EndDownload(CHIP_ERROR_CONNECTION_ABORTED);
    }
    if (mImageProcessor != nullptr)
    {
        TEMPORARY_RETURN_IGNORED mImageProcessor->Abort();
    }

    RecordState(OTAUpdateStateEnum::kIdle, OTAChangeReasonEnum::kFailure);
    RestoreMatterOwnership();
    SetLastResult(CHIP_ERROR_CONNECTION_ABORTED, LastResultReason::kAborted);
    ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL aborted 0x%08" PRIX32, CHIP_ERROR_CONNECTION_ABORTED.AsInteger());

    if (mVendorDriver != nullptr)
    {
        mVendorDriver->UpdateFailed();
    }
}

void VendorOTARequestor::Reset()
{
    CancelImageUpdate();
}

void VendorOTARequestor::OnDownloadStateChanged(OTADownloader::State state, OTAChangeReasonEnum reason)
{
    // 1. Download complete: persist apply markers and hand off to the image processor.
    if (state == OTADownloader::State::kComplete)
    {
        mTargetVersion            = mHttpsDownloader->GetTargetVersion();
        CHIP_ERROR persistErr = PersistApplyingForConfirm(mTargetVersion);
        if (persistErr != CHIP_NO_ERROR)
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] PersistApplyingForConfirm failed %" CHIP_ERROR_FORMAT, persistErr.Format());
        }
        SetLastResult(CHIP_NO_ERROR, LastResultReason::kOk);
        ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] RESULT: PASS");
        if (mImageProcessor != nullptr)
        {
            CHIP_ERROR applyErr = mImageProcessor->Apply();
            if (applyErr != CHIP_NO_ERROR)
            {
                ChipLogError(SoftwareUpdate, "[HTTPS-OTA] ImageProcessor::Apply failed %" CHIP_ERROR_FORMAT, applyErr.Format());
            }
        }
        return;
    }

    // 2. Failed idle transition: restore Matter ownership and re-arm the periodic check.
    if ((state == OTADownloader::State::kIdle) && (reason != OTAChangeReasonEnum::kSuccess))
    {
        if (mActive)
        {
            const bool notNewer = (mHttpsDownloader != nullptr) && mHttpsDownloader->WasLastFailureNotNewer();

            RecordState(OTAUpdateStateEnum::kIdle, reason);
            RestoreMatterOwnership();

            if (reason == OTAChangeReasonEnum::kTimeOut)
            {
                SetLastResult(CHIP_ERROR_TIMEOUT, LastResultReason::kStall);
            }
            else if (notNewer)
            {
                SetLastResult(CHIP_ERROR_VERSION_MISMATCH, LastResultReason::kVersionMismatch);
            }
            else
            {
                SetLastResult(CHIP_ERROR_INTERNAL, LastResultReason::kHttp);
            }

            if (mVendorDriver != nullptr)
            {
                if (notNewer)
                {
                    // Matter maps available-but-not-newer to UpdateNotFound(kUpToDate).
                    mVendorDriver->UpdateNotFound();
                }
                else
                {
                    mVendorDriver->UpdateFailed();
                }
            }
        }
    }
}

void VendorOTARequestor::OnUpdateProgressChanged(Nullable<uint8_t> percent)
{
    mProgress = percent;
    TEMPORARY_RETURN_IGNORED GetOTARequestorAttributes().SetUpdateStateProgress(percent);
}

void VendorOTARequestor::OnWiFiConnectivityChanged(bool established)
{
    VerifyOrReturn(mActive && (mHttpsDownloader != nullptr));
    if (!established)
    {
        mHttpsDownloader->NoteConnectivityLost();
    }
    else
    {
        TEMPORARY_RETURN_IGNORED mHttpsDownloader->NoteConnectivityRestored();
    }
}

void VendorOTARequestor::HandleAnnounceOTAProvider(
    app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
    const app::Clusters::OtaSoftwareUpdateRequestor::Commands::AnnounceOTAProvider::DecodableType & /* commandData */)
{
    ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] active: ignoring AnnounceOTAProvider");
    if (commandObj != nullptr)
    {
        commandObj->AddStatus(commandPath, Status::Success);
    }
}

void VendorOTARequestor::OnFabricRemoved(FabricIndex /* fabricIndex */)
{
    ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] active: ignoring OnFabricRemoved");
}

CHIP_ERROR VendorOTARequestor::GetUpdateStateAttribute(EndpointId /* endpointId */, OTAUpdateStateEnum & state)
{
    state = mUpdateState;
    return CHIP_NO_ERROR;
}

CHIP_ERROR VendorOTARequestor::GetUpdateStateProgressAttribute(EndpointId /* endpointId */, Nullable<uint8_t> & progress)
{
    progress = mProgress;
    return CHIP_NO_ERROR;
}

CHIP_ERROR VendorOTARequestor::TriggerImmediateQuery(FabricIndex /* fabricIndex */)
{
    return CHIP_ERROR_BUSY;
}

void VendorOTARequestor::TriggerImmediateQueryInternal() {}
void VendorOTARequestor::DownloadUpdate() {}
void VendorOTARequestor::DownloadUpdateDelayedOnUserConsent() {}
void VendorOTARequestor::ApplyUpdate() {}
void VendorOTARequestor::NotifyUpdateApplied() {}

CHIP_ERROR VendorOTARequestor::ClearDefaultOtaProviderList(FabricIndex /* fabricIndex */)
{
    return CHIP_ERROR_BUSY;
}

void VendorOTARequestor::SetCurrentProviderLocation(ProviderLocationType /* providerLocation */) {}

void VendorOTARequestor::SetMetadataForProvider(ByteSpan /* metadataForProvider */) {}

void VendorOTARequestor::GetProviderLocation(Optional<ProviderLocationType> & providerLocation)
{
    providerLocation.ClearValue();
}

CHIP_ERROR VendorOTARequestor::AddDefaultOtaProvider(const ProviderLocationType & /* providerLocation */)
{
    return CHIP_ERROR_BUSY;
}

ProviderLocationList::Iterator VendorOTARequestor::GetDefaultOTAProviderListIterator()
{
    return mEmptyProviderList.Begin();
}

} // namespace chip
