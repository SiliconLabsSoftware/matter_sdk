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

#include "cmsis_os2.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConfigurationManager.h>
#include <platform/DeviceInstanceInfoProvider.h>

#include <algorithm>
#include <cinttypes>
#include <cstring>

using chip::app::Clusters::OtaSoftwareUpdateRequestor::OTAChangeReasonEnum;
using chip::app::DataModel::Nullable;
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

    ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL stall %" CHIP_ERROR_FORMAT, CHIP_ERROR_TIMEOUT.Format());
    return true;
}

void HttpsDownloader::ArmStallTimer()
{
    CancelStallTimer();
    LogErrorOnFailure(DeviceLayer::SystemLayer().StartTimer(mTimeout, StallTimerHandler, this));
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
    ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] busy/SNI retry");
    LogErrorOnFailure(self->FetchNextData());
}

void HttpsDownloader::ArmBusyRetryTimer()
{
    CancelBusyRetryTimer();
    LogErrorOnFailure(DeviceLayer::SystemLayer().StartTimer(kBusyRetryTimeout, BusyRetryTimerHandler, this));
}

void HttpsDownloader::CancelBusyRetryTimer()
{
    DeviceLayer::SystemLayer().CancelTimer(BusyRetryTimerHandler, this);
}

void HttpsDownloader::ClearSessionTarget()
{
    memset(mServerEndpoint, 0, sizeof(mServerEndpoint));
    memset(mTlsHostname, 0, sizeof(mTlsHostname));
    memset(mPath, 0, sizeof(mPath));
    mPort = 0;
}

CHIP_ERROR HttpsDownloader::EnsureChunkSlotSem()
{
    if (mChunkSlotSem != nullptr)
    {
        osSemaphoreDelete(static_cast<osSemaphoreId_t>(mChunkSlotSem));
        mChunkSlotSem = nullptr;
    }

    // max_count=1, initial_count=1: first OnGetData Acquire succeeds; CHIP Releases after ProcessBlock is queued.
    mChunkSlotSem = osSemaphoreNew(1, 1, nullptr);
    VerifyOrReturnError(mChunkSlotSem != nullptr, CHIP_ERROR_NO_MEMORY);
    return CHIP_NO_ERROR;
}

void HttpsDownloader::ReleaseChunkSlot()
{
    if (mChunkSlotSem != nullptr)
    {
        osSemaphoreRelease(static_cast<osSemaphoreId_t>(mChunkSlotSem));
    }
}

CHIP_ERROR HttpsDownloader::SetImageURI(CharSpan uri)
{
    VerifyOrReturnError(mUriComposer != nullptr, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(uri.size() > 0 && uri.size() < kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);

    ClearSessionTarget();

    // Leave one byte for NUL after the composer writes into the spans.
    MutableCharSpan serverEndpoint(mServerEndpoint, kMaxHostLen - 1);
    MutableCharSpan tlsHostname(mTlsHostname, kMaxHostLen - 1);
    MutableCharSpan path(mPath, kMaxPathLen - 1);
    ReturnErrorOnFailure(mUriComposer->ComposeImageUri(uri, serverEndpoint, tlsHostname, mPort, path));

    VerifyOrReturnError(serverEndpoint.size() > 0 && tlsHostname.size() > 0 && path.size() > 0, CHIP_ERROR_INVALID_ARGUMENT);
    mServerEndpoint[serverEndpoint.size()] = '\0';
    mTlsHostname[tlsHostname.size()]       = '\0';
    mPath[path.size()]                     = '\0';
    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpsDownloader::QueueGet()
{
    VerifyOrReturnError(mHttpClient != nullptr, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mHttpClient->IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mWaitingHttp, CHIP_ERROR_BUSY);
    VerifyOrReturnError(mServerEndpoint[0] != '\0' && mPath[0] != '\0', CHIP_ERROR_INCORRECT_STATE);
    ReturnErrorOnFailure(EnsureChunkSlotSem());

    mRxSpan       = MutableByteSpan(mRxStaging, sizeof(mRxStaging));
    mRxStagingLen = 0;

    const HttpHost host = {
        .serverEndpoint = mServerEndpoint,
        .tlsHostname    = mTlsHostname,
        .port           = mPort,
    };

    ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] GET queued path=%s", mPath);

    CHIP_ERROR err = mHttpClient->Get(host, mPath, mRxSpan, nullptr, 0, OnGetData, OnGetComplete, this);
    if (err == CHIP_ERROR_BUSY)
    {
        ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] busy retry scheduled");
        ArmBusyRetryTimer();
        return CHIP_NO_ERROR;
    }
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] QueueGet Get=%" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    mWaitingHttp = true;
    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpsDownloader::OnGetData(ByteSpan chunk, void * context)
{
    auto * self = static_cast<HttpsDownloader *>(context);
    VerifyOrReturnError(self != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(chunk.size() > 0, CHIP_NO_ERROR);
    VerifyOrReturnError(chunk.size() <= sizeof(self->mRxStaging), CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(self->mChunkSlotSem != nullptr, CHIP_ERROR_INCORRECT_STATE);

    // Wait until CHIP finished the previous chunk so mRxStaging is free.
    const osStatus_t wait = osSemaphoreAcquire(static_cast<osSemaphoreId_t>(self->mChunkSlotSem), osWaitForever);
    VerifyOrReturnError(wait == osOK, CHIP_ERROR_INTERNAL);

    memcpy(self->mRxStaging, chunk.data(), chunk.size());
    self->mRxStagingLen = static_cast<uint32_t>(chunk.size());

    const CHIP_ERROR schedErr = DeviceLayer::SystemLayer().ScheduleLambda([self] {
        if (self->mState != State::kInProgress)
        {
            self->ReleaseChunkSlot();
            return;
        }

        ByteSpan data(self->mRxStaging, self->mRxStagingLen);
        CHIP_ERROR err = self->HandleBodyChunk(data);
        self->ReleaseChunkSlot();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] HandleBodyChunk=%" CHIP_ERROR_FORMAT, err.Format());
            self->CleanupOnError(OTAChangeReasonEnum::kFailure);
            if (self->mImageProcessor != nullptr)
            {
                LogErrorOnFailure(self->mImageProcessor->Abort());
            }
        }
    });
    if (schedErr != CHIP_NO_ERROR)
    {
        self->ReleaseChunkSlot();
        return schedErr;
    }

    return CHIP_NO_ERROR;
}

void HttpsDownloader::OnGetComplete(CHIP_ERROR result, void * context)
{
    auto * self = static_cast<HttpsDownloader *>(context);
    VerifyOrReturn(self != nullptr);

    const uint16_t httpCode = (self->mHttpClient != nullptr) ? self->mHttpClient->GetLastHttpResponseCode() : 0;

    LogErrorOnFailure(DeviceLayer::SystemLayer().ScheduleLambda([self, result, httpCode]() {
        // Drain the last in-flight chunk (if any) before finalize / cleanup.
        if (self->mChunkSlotSem != nullptr)
        {
            const osStatus_t wait = osSemaphoreAcquire(static_cast<osSemaphoreId_t>(self->mChunkSlotSem), osWaitForever);
            if (wait == osOK)
            {
                osSemaphoreRelease(static_cast<osSemaphoreId_t>(self->mChunkSlotSem));
            }
        }

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
            if (self->mConnectivityLost)
            {
                ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] Get error during connectivity loss (ignored)");
                return;
            }
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL http %" CHIP_ERROR_FORMAT " code=%u", result.Format(),
                         httpCode);
            self->CleanupOnError(OTAChangeReasonEnum::kFailure);
            if (self->mImageProcessor != nullptr)
            {
                LogErrorOnFailure(self->mImageProcessor->Abort());
            }
            return;
        }

        if ((httpCode != 0) && (httpCode != 200))
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL http-code=%u", httpCode);
            self->CleanupOnError(OTAChangeReasonEnum::kFailure);
            if (self->mImageProcessor != nullptr)
            {
                LogErrorOnFailure(self->mImageProcessor->Abort());
            }
            return;
        }

        if (!self->mHeaderValidated || (self->mNextOffset == 0))
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL empty-or-incomplete");
            self->CleanupOnError(OTAChangeReasonEnum::kFailure);
            if (self->mImageProcessor != nullptr)
            {
                LogErrorOnFailure(self->mImageProcessor->Abort());
            }
            return;
        }

        CHIP_ERROR err = self->mImageProcessor->Finalize();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] Finalize=%" CHIP_ERROR_FORMAT, err.Format());
            self->CleanupOnError(OTAChangeReasonEnum::kFailure);
            if (self->mImageProcessor != nullptr)
            {
                LogErrorOnFailure(self->mImageProcessor->Abort());
            }
            return;
        }

        self->CancelStallTimer();
        self->CancelBusyRetryTimer();
        self->ClearSessionTarget();
        self->SetState(State::kComplete, OTAChangeReasonEnum::kSuccess);
    }));
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

CHIP_ERROR HttpsDownloader::HandleBodyChunk(ByteSpan data)
{
    VerifyOrReturnError(mState == State::kInProgress, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mImageProcessor != nullptr, CHIP_ERROR_INCORRECT_STATE);

    if (mBytesToSkip > 0)
    {
        const size_t skip = std::min(data.size(), static_cast<size_t>(mBytesToSkip));
        data              = data.SubSpan(skip);
        mBytesToSkip -= static_cast<uint32_t>(skip);
        if (data.size() == 0)
        {
            return CHIP_NO_ERROR;
        }
    }

    ByteSpan forValidate = data;
    {
        CHIP_ERROR headerErr = ValidateHeaderIncremental(forValidate);
        if (headerErr == CHIP_ERROR_VERSION_MISMATCH)
        {
            mLastFailureNotNewer = true;
            CancelStallTimer();
            CancelBusyRetryTimer();
            if (mImageProcessor != nullptr)
            {
                LogErrorOnFailure(mImageProcessor->Abort());
            }
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL version-mismatch %" CHIP_ERROR_FORMAT,
                         CHIP_ERROR_VERSION_MISMATCH.Format());
            SetState(State::kIdle, OTAChangeReasonEnum::kFailure);
            return CHIP_NO_ERROR;
        }
        ReturnErrorOnFailure(headerErr);
    }

    ByteSpan processSpan = data;
    CHIP_ERROR procErr   = mImageProcessor->ProcessBlock(processSpan);
    if (procErr != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] ProcessBlock=%" CHIP_ERROR_FORMAT, procErr.Format());
    }
    ReturnErrorOnFailure(procErr);

    mNextOffset += static_cast<uint32_t>(data.size());
    NotifyProgress();
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
    mBytesToSkip         = 0;
    mHeaderValidated     = false;
    mTargetVersion       = 0;
    mLastFailureNotNewer = false;
    mHeaderParser.Init();
    mConnectivityLost = false;
    mWaitingHttp      = false;
    mRxStagingLen     = 0;

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

    // Image processor pulls FetchNextData after each ProcessBlock; the body stream is already
    // in flight after the first QueueGet.
    if (mWaitingHttp)
    {
        return CHIP_NO_ERROR;
    }

    return QueueGet();
}

CHIP_ERROR HttpsDownloader::SkipData(uint32_t numBytes)
{
    VerifyOrReturnError(mState == State::kInProgress, CHIP_ERROR_INCORRECT_STATE);
    mBytesToSkip += numBytes;
    mNextOffset += numBytes;
    return CHIP_NO_ERROR;
}

void HttpsDownloader::OnDownloadTimeout()
{
    CancelStallTimer();
    CancelBusyRetryTimer();
    mWaitingHttp = false;
    ClearSessionTarget();

    if ((mState == State::kInProgress) || (mState == State::kPreparing))
    {
        if (mImageProcessor != nullptr)
        {
            LogErrorOnFailure(mImageProcessor->Abort());
        }
        SetState(State::kIdle, OTAChangeReasonEnum::kTimeOut);
    }
}

void HttpsDownloader::EndDownload(CHIP_ERROR reason)
{
    CancelStallTimer();
    CancelBusyRetryTimer();
    mWaitingHttp = false;
    ClearSessionTarget();

    if ((mState == State::kInProgress) || (mState == State::kPreparing))
    {
        if ((mImageProcessor != nullptr) && (reason != CHIP_NO_ERROR))
        {
            LogErrorOnFailure(mImageProcessor->Abort());
        }
        SetState(State::kIdle, (reason == CHIP_NO_ERROR) ? OTAChangeReasonEnum::kSuccess : OTAChangeReasonEnum::kFailure);
    }
}

void HttpsDownloader::CleanupOnError(OTAChangeReasonEnum reason)
{
    CancelStallTimer();
    CancelBusyRetryTimer();
    mWaitingHttp = false;
    ClearSessionTarget();
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
    CancelStallTimer();
}

CHIP_ERROR HttpsDownloader::NoteConnectivityRestored()
{
    if (!mConnectivityLost || (mState != State::kInProgress))
    {
        return CHIP_NO_ERROR;
    }

    mConnectivityLost = false;

    // Full-body GET cannot resume mid-file without Range; fail the session.
    if (mWaitingHttp || (mNextOffset > 0))
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL no-resume-after-disconnect");
        CleanupOnError(OTAChangeReasonEnum::kFailure);
        if (mImageProcessor != nullptr)
        {
            LogErrorOnFailure(mImageProcessor->Abort());
        }
        return CHIP_ERROR_CONNECTION_ABORTED;
    }

    ArmStallTimer();
    return FetchNextData();
}

} // namespace chip
