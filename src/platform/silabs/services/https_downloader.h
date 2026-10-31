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

#include "http_client.h"

// sl_http_client.h pulls lwip/sockets.h which #define bind(...). Undef after include so
// later <functional> / CHIP headers do not break.
#ifdef bind
#undef bind
#endif // bind

#include <app/clusters/ota-requestor/OTADownloader.h>
#include <app-common/zap-generated/cluster-enums.h>
#include <lib/core/CHIPError.h>
#include <lib/core/OTAImageHeader.h>
#include <lib/support/Span.h>
#include <system/SystemClock.h>
#include <system/SystemLayer.h>

namespace chip {

/**
 * @brief OTADownloader that fetches a Matter OTA image over HTTPS using HTTP Range GETs.
 *
 * Transport only (BDXDownloader analog). Uses the shared HttpClient on the http_client thread.
 * Receives a URL via SetImageURI; does not know the URL source or where it is stored.
 */
class HttpsDownloader : public OTADownloader
{
public:
    static constexpr uint32_t kDefaultBlockSize               = 1024;
    static constexpr System::Clock::Timeout kDefaultTimeout   = System::Clock::Seconds32(5 * 60);
    static constexpr System::Clock::Timeout kBusyRetryTimeout = System::Clock::Seconds16(1);
    static constexpr size_t kMaxUrlLen                        = 256;
    static constexpr size_t kMaxHostLen                       = 128;
    static constexpr size_t kMaxPathLen                       = 160;

    class StateDelegate
    {
    public:
        virtual void OnDownloadStateChanged(State state,
                                            app::Clusters::OtaSoftwareUpdateRequestor::OTAChangeReasonEnum reason) = 0;
        virtual void OnUpdateProgressChanged(app::DataModel::Nullable<uint8_t> percent)                            = 0;
        virtual ~StateDelegate()                                                                                   = default;
    };

    /**
     * App/example parses the image URI and applies any connect-endpoint override.
     * Pointer is not owned.
     */
    class UriComposer
    {
    public:
        virtual ~UriComposer() = default;
        // Parse uri and fill connect endpoint, TLS hostname, port, and path.
        virtual CHIP_ERROR ComposeImageUri(CharSpan uri, MutableCharSpan & serverEndpoint, MutableCharSpan & tlsHostname,
                                           uint16_t & port, MutableCharSpan & path) = 0;
    };

    void SetStateDelegate(StateDelegate * delegate) { mStateDelegate = delegate; }
    void SetTimeout(System::Clock::Timeout timeout) { mTimeout = timeout; }
    void SetBlockSize(uint32_t blockSize) { mBlockSize = blockSize; }

    /** Shared NWP HttpClient (must already be Start+Init). Pointer is not owned. */
    void SetHttpClient(DeviceLayer::Silabs::HttpClient * client) { mHttpClient = client; }
    DeviceLayer::Silabs::HttpClient * GetHttpClient() const { return mHttpClient; }

    /** URI composer (parse + connect override). Pointer is not owned. */
    void SetUriComposer(UriComposer * composer) { mUriComposer = composer; }

    /** Compose uri into the session target, then use it for Range GETs. */
    CHIP_ERROR SetImageURI(CharSpan uri);

    CHIP_ERROR BeginPrepareDownload() override;
    CHIP_ERROR OnPreparedForDownload(CHIP_ERROR status) override;
    void OnDownloadTimeout() override;
    void EndDownload(CHIP_ERROR reason = CHIP_NO_ERROR) override;
    CHIP_ERROR FetchNextData() override;
    CHIP_ERROR SkipData(uint32_t numBytes) override;

    uint32_t GetNextOffset() const { return mNextOffset; }
    uint32_t GetTargetVersion() const { return mTargetVersion; }
    bool WasLastFailureNotNewer() const { return mLastFailureNotNewer; }

    void NoteConnectivityLost();
    CHIP_ERROR NoteConnectivityRestored();

private:
    CHIP_ERROR QueueRangeGet();
    void ClearSessionTarget();

    void ArmStallTimer();
    void CancelStallTimer();
    bool HasTransferTimedOut();
    static void StallTimerHandler(System::Layer * layer, void * appState);

    void ArmBusyRetryTimer();
    void CancelBusyRetryTimer();
    static void BusyRetryTimerHandler(System::Layer * layer, void * appState);

    static void OnRangeComplete(CHIP_ERROR result, void * context);
    CHIP_ERROR HandleRangeBodyChunk(ByteSpan data, uint16_t httpCode);
    CHIP_ERROR ValidateHeaderIncremental(ByteSpan & block);
    void NotifyProgress();
    void SetState(State state, app::Clusters::OtaSoftwareUpdateRequestor::OTAChangeReasonEnum reason);
    void CleanupOnError(app::Clusters::OtaSoftwareUpdateRequestor::OTAChangeReasonEnum reason);

    StateDelegate * mStateDelegate                = nullptr;
    DeviceLayer::Silabs::HttpClient * mHttpClient = nullptr;
    UriComposer * mUriComposer                    = nullptr;

    char mServerEndpoint[kMaxHostLen]{};
    char mTlsHostname[kMaxHostLen]{};
    char mPath[kMaxPathLen]{};
    uint16_t mPort = 0;

    System::Clock::Timeout mTimeout = kDefaultTimeout;
    uint32_t mBlockSize             = kDefaultBlockSize;
    uint32_t mNextOffset            = 0;
    uint32_t mPrevOffset            = 0;
    uint32_t mTargetVersion         = 0;

    bool mHeaderValidated     = false;
    bool mWaitingHttp         = false;
    bool mConnectivityLost    = false;
    bool mLastFailureNotNewer = false;

    OTAImageHeaderParser mHeaderParser;
    uint8_t mBlockBuf[kDefaultBlockSize]{};
    uint32_t mBlockFilled = 0;

    uint8_t mRxStaging[kDefaultBlockSize]{};
    uint32_t mRxStagingLen = 0;
    MutableByteSpan mRxSpan{ mRxStaging };
};

} // namespace chip
