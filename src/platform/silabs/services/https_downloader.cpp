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

#include "https_downloader.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConfigurationManager.h>
#include <platform/DeviceInstanceInfoProvider.h>

// Project OTA config may define HTTPS_OTA_CONNECT_IP.
#if (SL_MATTER_GN_BUILD == 0)
#include "sl_matter_ota_config.h"
#endif

#include <algorithm>
#include <cinttypes>
#include <cstring>

using chip::app::Clusters::OtaSoftwareUpdateRequestor::OTAChangeReasonEnum;
using chip::app::DataModel::Nullable;
using chip::DeviceLayer::Silabs::HttpClient;
using chip::DeviceLayer::Silabs::HttpHost;

namespace chip {

void HttpsDownloader::StallTimerHandler(System::Layer * /* layer */, void * appState)
{
    auto * self = static_cast<HttpsDownloader *>(appState);
    VerifyOrReturn(self != nullptr);

    if (self->HasTransferTimedOut())
    {
        self->OnDownloadTimeout();
    }
    else
    {
        self->ArmStallTimer();
    }
}

bool HttpsDownloader::HasTransferTimedOut()
{
    if (mNextOffset > mPrevOffset)
    {
        mPrevOffset = mNextOffset;
        return false;
    }

    ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL stall 0x%08" PRIX32, CHIP_ERROR_TIMEOUT.AsInteger());
    return true;
}

void HttpsDownloader::ArmStallTimer()
{
    CancelStallTimer();
    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().StartTimer(mTimeout, StallTimerHandler, this);
}

void HttpsDownloader::CancelStallTimer()
{
    DeviceLayer::SystemLayer().CancelTimer(StallTimerHandler, this);
}

void HttpsDownloader::BusyRetryTimerHandler(System::Layer * /* layer */, void * appState)
{
    auto * self = static_cast<HttpsDownloader *>(appState);
    VerifyOrReturn(self != nullptr);
    VerifyOrReturn(self->mState == State::kInProgress);
    ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] busy/SNI retry at offset %" PRIu32, self->mNextOffset);
    TEMPORARY_RETURN_IGNORED self->FetchNextData();
}

void HttpsDownloader::ArmBusyRetryTimer()
{
    CancelBusyRetryTimer();
    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().StartTimer(kBusyRetryTimeout, BusyRetryTimerHandler, this);
}

void HttpsDownloader::CancelBusyRetryTimer()
{
    DeviceLayer::SystemLayer().CancelTimer(BusyRetryTimerHandler, this);
}

CHIP_ERROR HttpsDownloader::ParseUrl(CharSpan uri)
{
    VerifyOrReturnError(uri.size() > 0 && uri.size() < kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
    memcpy(mUrlStorage, uri.data(), uri.size());
    mUrlStorage[uri.size()] = '\0';

    const char * s = mUrlStorage;
    mUrl           = ParsedUrl{};

    VerifyOrReturnError(strncmp(s, "https://", 8) == 0, CHIP_ERROR_INVALID_ARGUMENT);
    mUrl.port = 443;
    s += 8;

    const char * slash = strchr(s, '/');
    const char * colon = strchr(s, ':');
    size_t hostLen     = 0;

    if ((colon != nullptr) && ((slash == nullptr) || (colon < slash)))
    {
        hostLen   = static_cast<size_t>(colon - s);
        mUrl.port = static_cast<uint16_t>(atoi(colon + 1));
    }
    else
    {
        hostLen = (slash != nullptr) ? static_cast<size_t>(slash - s) : strlen(s);
    }

    VerifyOrReturnError(hostLen > 0 && hostLen < kMaxHostLen, CHIP_ERROR_INVALID_ARGUMENT);
    memcpy(mUrl.host, s, hostLen);
    mUrl.host[hostLen] = '\0';

    if (slash != nullptr)
    {
        VerifyOrReturnError(strlen(slash) < kMaxPathLen, CHIP_ERROR_INVALID_ARGUMENT);
        strcpy(mUrl.path, slash);
    }
    else
    {
        strcpy(mUrl.path, "/");
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpsDownloader::SetImageURI(CharSpan uri)
{
    return ParseUrl(uri);
}

CHIP_ERROR HttpsDownloader::QueueRangeGet()
{
    VerifyOrReturnError(mHttpClient != nullptr, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mHttpClient->IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mWaitingHttp, CHIP_ERROR_BUSY);

    mRxSpan = MutableByteSpan(mRxStaging, sizeof(mRxStaging));
    mRxStagingLen = 0;

    // Optional dotted-IPv4 connect override (HTTPS_OTA_CONNECT_IP): skips DNS while
    // keeping mUrl.host for TLS SNI / Host header.
    const bool bypassDns   = (HTTPS_OTA_CONNECT_IP[0] != '\0');
    const char * connectIp = bypassDns ? HTTPS_OTA_CONNECT_IP : mUrl.host;
    const HttpHost host    = {
        .hostName = mUrl.host,
        .serverIp = connectIp,
        .port     = mUrl.port,
    };

    const uint32_t endInclusive = mNextOffset + mBlockSize - 1;
    ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] range queued offset=%" PRIu32 "-%" PRIu32, mNextOffset, endInclusive);
    if ((mNextOffset == 0) && bypassDns)
    {
        ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] DNS bypass connect=%s sni=%s", connectIp, mUrl.host);
    }

    CHIP_ERROR err =
        mHttpClient->GetRange(host, mUrl.path, mNextOffset, endInclusive, mRxSpan, OnRangeComplete, this);
    if (err == CHIP_ERROR_BUSY)
    {
        ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] busy retry scheduled");
        ArmBusyRetryTimer();
        return CHIP_NO_ERROR;
    }
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] QueueRangeGet GetRange=%" CHIP_ERROR_FORMAT, err.Format());
    }
    ReturnErrorOnFailure(err);

    mWaitingHttp = true;
    return CHIP_NO_ERROR;
}

void HttpsDownloader::OnRangeComplete(CHIP_ERROR result, void * context)
{
    auto * self = static_cast<HttpsDownloader *>(context);
    VerifyOrReturn(self != nullptr);

    // Completion callback runs on http_client; hop ProcessBlock work to CHIP.
    const uint16_t httpCode =
        (self->mHttpClient != nullptr) ? self->mHttpClient->GetLastHttpResponseCode() : 0;
    const uint32_t len = static_cast<uint32_t>(self->mRxSpan.size());

    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().ScheduleLambda([self, result, httpCode, len]() {
        self->mWaitingHttp = false;

        if (self->mState != State::kInProgress)
        {
            return;
        }

        if (result == CHIP_ERROR_BUSY)
        {
            ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] SNI/busy retry scheduled");
            self->ArmBusyRetryTimer();
            return;
        }

        if (result != CHIP_NO_ERROR)
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL http 0x%08" PRIX32, result.AsInteger());
            self->CleanupOnError(OTAChangeReasonEnum::kFailure);
            if (self->mImageProcessor != nullptr)
            {
                TEMPORARY_RETURN_IGNORED self->mImageProcessor->Abort();
            }
            return;
        }

        self->mRxStagingLen = len;
        ByteSpan data(self->mRxStaging, self->mRxStagingLen);
        CHIP_ERROR err = self->HandleRangeBodyChunk(data, httpCode);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] HandleRangeBodyChunk=%" CHIP_ERROR_FORMAT, err.Format());
            self->CleanupOnError(OTAChangeReasonEnum::kFailure);
            if (self->mImageProcessor != nullptr)
            {
                TEMPORARY_RETURN_IGNORED self->mImageProcessor->Abort();
            }
        }
    });
}

CHIP_ERROR HttpsDownloader::ValidateHeaderIncremental(ByteSpan & block)
{
    if (mHeaderValidated)
    {
        return CHIP_NO_ERROR;
    }

    OTAImageHeader header;
    CHIP_ERROR err = mHeaderParser.AccumulateAndDecode(block, header);
    if (err == CHIP_ERROR_BUFFER_TOO_SMALL)
    {
        return CHIP_NO_ERROR;
    }
    ReturnErrorOnFailure(err);

    uint16_t vendorId       = 0;
    uint16_t productId      = 0;
    uint32_t currentVersion = 0;
    ReturnErrorOnFailure(DeviceLayer::GetDeviceInstanceInfoProvider()->GetVendorId(vendorId));
    ReturnErrorOnFailure(DeviceLayer::GetDeviceInstanceInfoProvider()->GetProductId(productId));
    ReturnErrorOnFailure(DeviceLayer::ConfigurationMgr().GetSoftwareVersion(currentVersion));

    if (header.mVendorId != vendorId)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] ValidateHeader vendor mismatch");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    if (header.mProductId != productId)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] ValidateHeader product mismatch");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    VerifyOrReturnError(header.mSoftwareVersion > currentVersion, CHIP_ERROR_VERSION_MISMATCH);

    mTargetVersion   = header.mSoftwareVersion;
    mHeaderValidated = true;
    mHeaderParser.Clear();

    ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] header accepted: vendor=%u product=%u version=%" PRIu32, vendorId, productId,
                    mTargetVersion);
    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpsDownloader::HandleRangeBodyChunk(ByteSpan data, uint16_t httpCode)
{
    VerifyOrReturnError(mState == State::kInProgress, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mImageProcessor != nullptr, CHIP_ERROR_INCORRECT_STATE);

    if (httpCode == 416)
    {
        ReturnErrorOnFailure(mImageProcessor->Finalize());
        CancelStallTimer();
        CancelBusyRetryTimer();
        SetState(State::kComplete, OTAChangeReasonEnum::kSuccess);
        return CHIP_NO_ERROR;
    }

    VerifyOrReturnError((httpCode == 200) || (httpCode == 206), CHIP_ERROR_INVALID_ARGUMENT);

    const uint32_t space = mBlockSize - mBlockFilled;
    const uint32_t take  = static_cast<uint32_t>(std::min(data.size(), static_cast<size_t>(space)));
    memcpy(mBlockBuf + mBlockFilled, data.data(), take);
    mBlockFilled += take;

    // GetRange returns one complete response body per request (end_of_data handled in HttpClient).
    ByteSpan block(mBlockBuf, mBlockFilled);
    ByteSpan forValidate = block;
    {
        CHIP_ERROR headerErr = ValidateHeaderIncremental(forValidate);
        if (headerErr == CHIP_ERROR_VERSION_MISMATCH)
        {
            mLastFailureNotNewer = true;
            CancelStallTimer();
            CancelBusyRetryTimer();
            if (mImageProcessor != nullptr)
            {
                TEMPORARY_RETURN_IGNORED mImageProcessor->Abort();
            }
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL version-mismatch 0x%08" PRIX32,
                         CHIP_ERROR_VERSION_MISMATCH.AsInteger());
            SetState(State::kIdle, OTAChangeReasonEnum::kFailure);
            return CHIP_NO_ERROR;
        }
        ReturnErrorOnFailure(headerErr);
    }

    ByteSpan processSpan(mBlockBuf, mBlockFilled);
    CHIP_ERROR procErr = mImageProcessor->ProcessBlock(processSpan);
    if (procErr != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] ProcessBlock=%" CHIP_ERROR_FORMAT, procErr.Format());
    }
    ReturnErrorOnFailure(procErr);

    mNextOffset += mBlockFilled;
    NotifyProgress();

    const bool shortRead = (mBlockFilled < mBlockSize);
    mBlockFilled         = 0;

    if (shortRead)
    {
        ReturnErrorOnFailure(mImageProcessor->Finalize());
        CancelStallTimer();
        CancelBusyRetryTimer();
        SetState(State::kComplete, OTAChangeReasonEnum::kSuccess);
    }

    return CHIP_NO_ERROR;
}

void HttpsDownloader::NotifyProgress()
{
    if ((mStateDelegate != nullptr) && (mImageProcessor != nullptr))
    {
        mStateDelegate->OnUpdateProgressChanged(mImageProcessor->GetPercentComplete());
    }
}

CHIP_ERROR HttpsDownloader::BeginPrepareDownload()
{
    VerifyOrReturnError(mState == State::kIdle, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mImageProcessor != nullptr, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mHttpClient != nullptr && mHttpClient->IsRunning(), CHIP_ERROR_INCORRECT_STATE);

    mNextOffset          = 0;
    mPrevOffset          = 0;
    mHeaderValidated     = false;
    mTargetVersion       = 0;
    mLastFailureNotNewer = false;
    mHeaderParser.Init();
    mConnectivityLost = false;
    mWaitingHttp      = false;
    mBlockFilled      = 0;

    ArmStallTimer();
    SetState(State::kPreparing, OTAChangeReasonEnum::kSuccess);
    return mImageProcessor->PrepareDownload();
}

CHIP_ERROR HttpsDownloader::OnPreparedForDownload(CHIP_ERROR status)
{
    VerifyOrReturnError(mState == State::kPreparing, CHIP_ERROR_INCORRECT_STATE);

    if (status != CHIP_NO_ERROR)
    {
        CancelStallTimer();
        CancelBusyRetryTimer();
        SetState(State::kIdle, OTAChangeReasonEnum::kFailure);
        return status;
    }

    SetState(State::kInProgress, OTAChangeReasonEnum::kSuccess);
    return FetchNextData();
}

CHIP_ERROR HttpsDownloader::FetchNextData()
{
    VerifyOrReturnError(mState == State::kInProgress, CHIP_ERROR_INCORRECT_STATE);
    if (mConnectivityLost)
    {
        ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] waiting for Wi-Fi before offset %" PRIu32, mNextOffset);
        return CHIP_NO_ERROR;
    }

    return QueueRangeGet();
}

CHIP_ERROR HttpsDownloader::SkipData(uint32_t numBytes)
{
    VerifyOrReturnError(mState == State::kInProgress, CHIP_ERROR_INCORRECT_STATE);
    mNextOffset += numBytes;
    return FetchNextData();
}

void HttpsDownloader::OnDownloadTimeout()
{
    CancelStallTimer();
    CancelBusyRetryTimer();
    mWaitingHttp = false;

    if ((mState == State::kInProgress) || (mState == State::kPreparing))
    {
        if (mImageProcessor != nullptr)
        {
            TEMPORARY_RETURN_IGNORED mImageProcessor->Abort();
        }
        SetState(State::kIdle, OTAChangeReasonEnum::kTimeOut);
    }
}

void HttpsDownloader::EndDownload(CHIP_ERROR reason)
{
    CancelStallTimer();
    CancelBusyRetryTimer();
    mWaitingHttp = false;

    if ((mState == State::kInProgress) || (mState == State::kPreparing))
    {
        if ((mImageProcessor != nullptr) && (reason != CHIP_NO_ERROR))
        {
            TEMPORARY_RETURN_IGNORED mImageProcessor->Abort();
        }
        SetState(State::kIdle, (reason == CHIP_NO_ERROR) ? OTAChangeReasonEnum::kSuccess : OTAChangeReasonEnum::kFailure);
    }
}

void HttpsDownloader::CleanupOnError(OTAChangeReasonEnum reason)
{
    CancelStallTimer();
    CancelBusyRetryTimer();
    mWaitingHttp = false;
    SetState(State::kIdle, reason);
}

void HttpsDownloader::SetState(State state, OTAChangeReasonEnum reason)
{
    mState = state;
    if (mStateDelegate != nullptr)
    {
        mStateDelegate->OnDownloadStateChanged(state, reason);
    }
}

void HttpsDownloader::NoteConnectivityLost()
{
    mConnectivityLost = true;
}

CHIP_ERROR HttpsDownloader::NoteConnectivityRestored()
{
    if (!mConnectivityLost || (mState != State::kInProgress))
    {
        return CHIP_NO_ERROR;
    }

    mConnectivityLost = false;
    ArmStallTimer();
    return FetchNextData();
}

} // namespace chip
