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
#include <cstring>

#if (SL_MATTER_GN_BUILD == 0)
#include "sl_matter_https_ota_config.h"
#endif // SL_MATTER_GN_BUILD

#ifndef SL_MATTER_HTTPS_OTA_DEFAULT_URL
#define SL_MATTER_HTTPS_OTA_DEFAULT_URL ""
#endif
#ifndef SL_MATTER_HTTP_TLS_HOSTNAME
#define SL_MATTER_HTTP_TLS_HOSTNAME ""
#endif
#ifndef SL_MATTER_HTTP_TLS_CA_CERTIFICATE
#define SL_MATTER_HTTP_TLS_CA_CERTIFICATE ""
#endif
#ifndef SL_MATTER_HTTP_CLIENT_USERNAME
#define SL_MATTER_HTTP_CLIENT_USERNAME ""
#endif
#ifndef SL_MATTER_HTTP_CLIENT_PASSWORD
#define SL_MATTER_HTTP_CLIENT_PASSWORD ""
#endif

using namespace chip::app::Clusters::OtaSoftwareUpdateRequestor;
using chip::app::DataModel::Nullable;
using chip::app::DataModel::NullNullable;
using chip::DeviceLayer::Internal::SilabsConfig;
using chip::DeviceLayer::Silabs::HttpClientConfig;
using chip::Protocols::InteractionModel::Status;
using chip::operator""_span;

namespace chip {
namespace {

VendorOTARequestor gVendorOTARequestor;

constexpr CharSpan kHttpsScheme      = "https://"_span;
constexpr uint16_t kDefaultHttpsPort = 443;

constexpr char kHttpTlsCaCertificate[] = SL_MATTER_HTTP_TLS_CA_CERTIFICATE;

// Empty string -> 0 (unused); otherwise include the trailing NUL for PEM parsers.
size_t PemCredentialLen(const char * pem, size_t maxLen)
{
    const size_t len = strnlen(pem, maxLen);
    return len == 0 ? 0 : len + 1;
}

bool FindFirst(CharSpan span, char ch, size_t & outIndex)
{
    for (size_t i = 0; i < span.size(); ++i)
    {
        if (span[i] == ch)
        {
            outIndex = i;
            return true;
        }
    }
    return false;
}

CHIP_ERROR ParsePort(CharSpan digits, uint16_t & port)
{
    VerifyOrReturnError(digits.size() > 0, CHIP_ERROR_INVALID_ARGUMENT);
    uint32_t value = 0;
    for (char c : digits)
    {
        VerifyOrReturnError((c >= '0') && (c <= '9'), CHIP_ERROR_INVALID_ARGUMENT);
        value = (value * 10u) + static_cast<uint32_t>(c - '0');
        VerifyOrReturnError(value <= 65535u, CHIP_ERROR_INVALID_ARGUMENT);
    }
    port = static_cast<uint16_t>(value);
    return CHIP_NO_ERROR;
}

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
    mHttpsDownloader->SetHttpClient(&mHttpClient);
    mHttpsDownloader->SetUriComposer(this);

    if (SL_MATTER_HTTPS_OTA_DEFAULT_URL[0] != '\0')
    {
        LogErrorOnFailure(SetPeriodicUrl(CharSpan::fromCharString(SL_MATTER_HTTPS_OTA_DEFAULT_URL)));
    }

    if (DeviceLayer::ConnectivityMgr().IsWiFiStationConnected())
    {
        LogErrorOnFailure(EnsureHttpClientStarted());
    }
}

CHIP_ERROR VendorOTARequestor::GetPeriodicUrl(MutableCharSpan & outUrl)
{
    VerifyOrReturnError(mPeriodicUrlLen > 0, CHIP_ERROR_NOT_FOUND);
    return CopyCharSpanToMutableCharSpan(CharSpan(mPeriodicUrl, mPeriodicUrlLen), outUrl);
}

CHIP_ERROR VendorOTARequestor::SetPeriodicUrl(CharSpan url)
{
    VerifyOrReturnError(url.size() > 0 && url.size() < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
    MutableCharSpan out(mPeriodicUrl, sizeof(mPeriodicUrl));
    ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan(url, out));
    mPeriodicUrlLen = out.size();
    return CHIP_NO_ERROR;
}

CHIP_ERROR VendorOTARequestor::ComposeImageUri(CharSpan uri, MutableCharSpan & serverEndpoint, MutableCharSpan & tlsHostname,
                                               uint16_t & port, MutableCharSpan & path)
{
    VerifyOrReturnError(uri.size() > 0 && uri.size() < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(uri.size() >= kHttpsScheme.size(), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(uri.SubSpan(0, kHttpsScheme.size()).data_equal(kHttpsScheme), CHIP_ERROR_INVALID_ARGUMENT);

    port                = kDefaultHttpsPort;
    CharSpan rest       = uri.SubSpan(kHttpsScheme.size());
    size_t slashPos     = 0;
    const bool hasSlash = FindFirst(rest, '/', slashPos);

    CharSpan authority = hasSlash ? rest.SubSpan(0, slashPos) : rest;
    CharSpan pathSpan  = hasSlash ? rest.SubSpan(slashPos) : CharSpan();

    size_t colonPos = 0;
    CharSpan host;
    if (FindFirst(authority, ':', colonPos))
    {
        host = authority.SubSpan(0, colonPos);
        ReturnErrorOnFailure(ParsePort(authority.SubSpan(colonPos + 1), port));
    }
    else
    {
        host = authority;
    }

    VerifyOrReturnError(host.size() > 0 && host.size() < HttpsDownloader::kMaxHostLen, CHIP_ERROR_INVALID_ARGUMENT);
    ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan(host, serverEndpoint));

    // Optional compile-time SNI / Host override (e.g. URL uses IPv4, cert uses DNS name).
    if (SL_MATTER_HTTP_TLS_HOSTNAME[0] != '\0')
    {
        ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan(CharSpan::fromCharString(SL_MATTER_HTTP_TLS_HOSTNAME), tlsHostname));
    }
    else
    {
        ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan(host, tlsHostname));
    }

    if (pathSpan.size() > 0)
    {
        VerifyOrReturnError(pathSpan.size() < HttpsDownloader::kMaxPathLen, CHIP_ERROR_INVALID_ARGUMENT);
        ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan(pathSpan, path));
    }
    else
    {
        ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan("/"_span, path));
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR VendorOTARequestor::EnsureHttpClientStarted()
{
    if (mClientReady)
    {
        return CHIP_NO_ERROR;
    }
    if (mClientInitPending)
    {
        return CHIP_NO_ERROR;
    }
    if (mHttpClient.IsRunning())
    {
        // Start succeeded earlier but Init never completed; fall through to Init.
    }
    else
    {
        CHIP_ERROR err = mHttpClient.Start();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] client start: failed %" CHIP_ERROR_FORMAT, err.Format());
            return err;
        }
    }

    const char * user             = SL_MATTER_HTTP_CLIENT_USERNAME;
    const char * pass             = SL_MATTER_HTTP_CLIENT_PASSWORD;
    const size_t caLen            = PemCredentialLen(kHttpTlsCaCertificate, sizeof(kHttpTlsCaCertificate));
    const HttpClientConfig config = {
        .httpsEnable  = true,
        .username     = (user[0] != '\0') ? user : nullptr,
        .password     = (pass[0] != '\0') ? pass : nullptr,
        .tlsCaCert    = (caLen > 0) ? reinterpret_cast<const uint8_t *>(kHttpTlsCaCertificate) : nullptr,
        .tlsCaCertLen = caLen,
    };

    mClientInitPending = true;
    CHIP_ERROR err     = mHttpClient.Init(config, OnHttpClientInitDone, this);
    if (err != CHIP_NO_ERROR)
    {
        mClientInitPending = false;
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] client init: queue failed %" CHIP_ERROR_FORMAT, err.Format());
        LogErrorOnFailure(mHttpClient.Stop());
        return err;
    }
    return CHIP_NO_ERROR;
}

void VendorOTARequestor::OnHttpClientInitDone(CHIP_ERROR result, void * context)
{
    auto * self              = static_cast<VendorOTARequestor *>(context);
    self->mClientInitPending = false;
    if (result == CHIP_NO_ERROR)
    {
        self->mClientReady = true;
        ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] client start: started");
        return;
    }

    ChipLogError(SoftwareUpdate, "[HTTPS-OTA] client init: failed %" CHIP_ERROR_FORMAT, result.Format());
    LogErrorOnFailure(self->mHttpClient.Stop());
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
        LogErrorOnFailure(GetOTARequestorAttributes().SetUpdateStateProgress(NullNullable));
    }

    if (mMatterStorage != nullptr)
    {
        LogErrorOnFailure(mMatterStorage->StoreCurrentUpdateState(state));
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
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL not-initialized %" CHIP_ERROR_FORMAT,
                     CHIP_ERROR_INCORRECT_STATE.Format());
        return CHIP_ERROR_INCORRECT_STATE;
    }
    if (!DeviceLayer::ConnectivityMgr().IsWiFiStationConnected())
    {
        SetLastResult(CHIP_ERROR_NOT_CONNECTED, LastResultReason::kClientStart);
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL not-connected %" CHIP_ERROR_FORMAT,
                     CHIP_ERROR_NOT_CONNECTED.Format());
        return CHIP_ERROR_NOT_CONNECTED;
    }
    if (!IsMatterIdle())
    {
        SetLastResult(CHIP_ERROR_BUSY, LastResultReason::kBusy);
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL busy %" CHIP_ERROR_FORMAT, CHIP_ERROR_BUSY.Format());
        return CHIP_ERROR_BUSY;
    }

    // 2. Require owned HttpClient Start+Init to have completed.
    if (!mClientReady || !mHttpClient.IsRunning())
    {
        SetLastResult(CHIP_ERROR_INCORRECT_STATE, LastResultReason::kClientStart);
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL client-start %" CHIP_ERROR_FORMAT,
                     CHIP_ERROR_INCORRECT_STATE.Format());
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
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL no-url %" CHIP_ERROR_FORMAT, err.Format());
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
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL http %" CHIP_ERROR_FORMAT, err.Format());
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
        LogErrorOnFailure(mImageProcessor->Abort());
    }

    RecordState(OTAUpdateStateEnum::kIdle, OTAChangeReasonEnum::kFailure);
    RestoreMatterOwnership();
    SetLastResult(CHIP_ERROR_CONNECTION_ABORTED, LastResultReason::kAborted);
    ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL aborted %" CHIP_ERROR_FORMAT, CHIP_ERROR_CONNECTION_ABORTED.Format());

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
        mTargetVersion        = mHttpsDownloader->GetTargetVersion();
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
    LogErrorOnFailure(GetOTARequestorAttributes().SetUpdateStateProgress(percent));
}

void VendorOTARequestor::OnWiFiConnectivityChanged(bool established)
{
    if (established)
    {
        LogErrorOnFailure(EnsureHttpClientStarted());
    }

    VerifyOrReturn(mActive && (mHttpsDownloader != nullptr));
    if (!established)
    {
        mHttpsDownloader->NoteConnectivityLost();
    }
    else
    {
        LogErrorOnFailure(mHttpsDownloader->NoteConnectivityRestored());
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
