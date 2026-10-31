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

/**
 *
 * @brief The example code is used to demonstrate the usage of the HTTPS client.
 * The steps are as follows:
 * 1. Start the HTTPS client
 * 2. Initialize the HTTPS client
 * 3. Run the HTTPS offload example
 * 4. Run the HTTPS PUT request to send the index.html file to the server
 * 5. Run the HTTPS GET request to get the index.html file from the server
 * 6. Run the HTTPS POST request to send the post data to the server
 * 7. Complete the HTTPS offload example and return the result
 * 8. Deinitialize the HTTPS client (optional, disabled)
 * 9. Stop the HTTPS client (optional, disabled)
 */

#include "https_offload_example.h"

#include "AppEvent.h"
#include "BaseApplication.h"
#include "cacert.h"
#include "index_html.h"

#include "http_client.h"

#include "cmsis_os2.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
#include <lib/shell/streamer.h>
#include <platform/silabs/services/https_downloader.h>
#include <platform/silabs/services/vendor_ota_requestor.h>

#if (SL_MATTER_GN_BUILD == 0)
#include "sl_matter_ota_config.h"
#endif // SL_MATTER_GN_BUILD

#ifndef SL_MATTER_HTTPS_OTA_ENDPOINT
#define SL_MATTER_HTTPS_OTA_ENDPOINT ""
#endif
#ifndef SL_MATTER_HTTPS_OTA_DEFAULT_URL
#define SL_MATTER_HTTPS_OTA_DEFAULT_URL ""
#endif
#endif // SL_MATTER_ENABLE_HTTPS_OTA

#include <cinttypes>
#include <cstdlib>
#include <cstring>

namespace {

using chip::ByteSpan;
using chip::CharSpan;
using chip::MutableByteSpan;
using chip::DeviceLayer::Silabs::HttpClient;
using chip::DeviceLayer::Silabs::HttpClientConfig;
using chip::DeviceLayer::Silabs::HttpHost;

// TODO: Update the below values to the correct values for your server.
constexpr char kHttpServerEndpoint[] = HTTP_SERVER_ENDPOINT; // DNS name or IPv4
constexpr char kHttpTlsHostname[]    = HTTP_TLS_HOSTNAME;    // SNI / Host header
constexpr uint16_t kHttpPort         = HTTP_PORT;
constexpr char kHttpUrl[]        = HTTP_URL;
constexpr char kHttpClientUser[] = HTTP_USER;
constexpr char kHttpClientPass[] = HTTP_PASS;
constexpr char kHttpPostData[]   = HTTP_POST_DATA;

// HTTPS client instance (process lifetime; not Deinit'd after successful start).
HttpClient gHttpsClient;

// Binary semaphore: CMSIS mutex cannot be released from another thread; NWP/HTTP
// completion callbacks run on the HttpClient service thread.
osSemaphoreId_t gHttpsTaskLock = nullptr;

struct OperationWaitContext
{
    osSemaphoreId_t lock;
    CHIP_ERROR result;
};

void OnOperationDone(CHIP_ERROR result, void * context)
{
    auto * waitCtx  = static_cast<OperationWaitContext *>(context);
    waitCtx->result = result;
    osSemaphoreRelease(waitCtx->lock);
}

CHIP_ERROR WaitForCallback(OperationWaitContext & waitCtx)
{
    const osStatus_t status = osSemaphoreAcquire(waitCtx.lock, osWaitForever);
    VerifyOrReturnError(status == osOK, CHIP_ERROR_INTERNAL);
    return waitCtx.result;
}

CHIP_ERROR RunQueuedOperation(CHIP_ERROR queueResult, OperationWaitContext & waitCtx)
{
    if (queueResult != CHIP_NO_ERROR)
    {
        return queueResult;
    }
    return WaitForCallback(waitCtx);
}

CHIP_ERROR RunHttpsOffloadExample()
{
    if (!gHttpsClient.IsRunning())
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }

    ChipLogProgress(DeviceLayer, "HTTPS starting on offload stack");

    OperationWaitContext waitCtx = { .lock = gHttpsTaskLock, .result = CHIP_NO_ERROR };

    const HttpHost host = {
        .serverEndpoint = kHttpServerEndpoint,
        .tlsHostname    = kHttpTlsHostname,
        .port           = kHttpPort,
    };

    const ByteSpan putBody(reinterpret_cast<const uint8_t *>(sl_index), sizeof(sl_index) - 1);
    CHIP_ERROR err = RunQueuedOperation(gHttpsClient.Put(host, kHttpUrl, putBody, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS PUT failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    ChipLogProgress(DeviceLayer, "HTTPS PUT request success");

    // Sized for the served index.html body; a smaller buffer truncates the response.
    uint8_t responseBuf[sizeof(sl_index)] = { 0 };
    MutableByteSpan responseSpan(responseBuf);
    err = RunQueuedOperation(gHttpsClient.Get(host, kHttpUrl, responseSpan, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS GET failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    ChipLogProgress(DeviceLayer, "HTTPS GET request success");

    const ByteSpan postBody(reinterpret_cast<const uint8_t *>(kHttpPostData), strlen(kHttpPostData));
    err = RunQueuedOperation(gHttpsClient.Post(host, kHttpUrl, postBody, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS POST failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    ChipLogProgress(DeviceLayer, "HTTPS POST request success");

    ChipLogProgress(DeviceLayer, "HTTPS demo completed");
    return CHIP_NO_ERROR;
}

#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA

using chip::HttpsDownloader;
using chip::MutableCharSpan;
using chip::VendorOTARequestor;
using chip::Shell::streamer_get;
using chip::Shell::streamer_printf;

char gPendingOtaUrl[HttpsDownloader::kMaxUrlLen];
size_t gPendingOtaUrlLen = 0;

/**
 * App owns URI composition (parse + connect-endpoint override) and the periodic URL store.
 */
class HttpsOtaApp : public VendorOTARequestor::AppDelegate
{
public:
    HttpsOtaApp()
    {
        if (SL_MATTER_HTTPS_OTA_DEFAULT_URL[0] != '\0')
        {
            TEMPORARY_RETURN_IGNORED SetPeriodicUrl(CharSpan::fromCharString(SL_MATTER_HTTPS_OTA_DEFAULT_URL));
        }
    }

    CHIP_ERROR SetPeriodicUrl(CharSpan url)
    {
        VerifyOrReturnError(url.size() > 0 && url.size() < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
        memcpy(mPeriodicUrl, url.data(), url.size());
        mPeriodicUrl[url.size()] = '\0';
        mPeriodicUrlLen          = url.size();
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetPeriodicUrl(MutableCharSpan & outUrl) override
    {
        VerifyOrReturnError(mPeriodicUrlLen > 0, CHIP_ERROR_NOT_FOUND);
        VerifyOrReturnError(outUrl.size() >= mPeriodicUrlLen, CHIP_ERROR_BUFFER_TOO_SMALL);
        memcpy(outUrl.data(), mPeriodicUrl, mPeriodicUrlLen);
        outUrl.reduce_size(mPeriodicUrlLen);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR ComposeImageUri(CharSpan uri, MutableCharSpan & serverEndpoint, MutableCharSpan & tlsHostname, uint16_t & port,
                               MutableCharSpan & path) override
    {
        VerifyOrReturnError(uri.size() > 0 && uri.size() < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);

        char urlStorage[HttpsDownloader::kMaxUrlLen];
        memcpy(urlStorage, uri.data(), uri.size());
        urlStorage[uri.size()] = '\0';

        const char * s = urlStorage;
        VerifyOrReturnError(strncmp(s, "https://", 8) == 0, CHIP_ERROR_INVALID_ARGUMENT);
        port = 443;
        s += 8;

        const char * slash = strchr(s, '/');
        const char * colon = strchr(s, ':');
        size_t hostLen     = 0;

        if ((colon != nullptr) && ((slash == nullptr) || (colon < slash)))
        {
            hostLen = static_cast<size_t>(colon - s);
            port    = static_cast<uint16_t>(atoi(colon + 1));
        }
        else
        {
            hostLen = (slash != nullptr) ? static_cast<size_t>(slash - s) : strlen(s);
        }

        VerifyOrReturnError(hostLen > 0 && hostLen < HttpsDownloader::kMaxHostLen, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(tlsHostname.size() >= hostLen, CHIP_ERROR_BUFFER_TOO_SMALL);
        memcpy(tlsHostname.data(), s, hostLen);
        tlsHostname.reduce_size(hostLen);

        char hostNul[HttpsDownloader::kMaxHostLen];
        memcpy(hostNul, s, hostLen);
        hostNul[hostLen] = '\0';

        const char * connectHost =
            (SL_MATTER_HTTPS_OTA_ENDPOINT[0] != '\0') ? SL_MATTER_HTTPS_OTA_ENDPOINT : hostNul;
        const size_t connectLen = strlen(connectHost);
        VerifyOrReturnError(connectLen > 0 && connectLen < HttpsDownloader::kMaxHostLen, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(serverEndpoint.size() >= connectLen, CHIP_ERROR_BUFFER_TOO_SMALL);
        memcpy(serverEndpoint.data(), connectHost, connectLen);
        serverEndpoint.reduce_size(connectLen);

        if (slash != nullptr)
        {
            const size_t pathLen = strlen(slash);
            VerifyOrReturnError(pathLen > 0 && pathLen < HttpsDownloader::kMaxPathLen, CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(path.size() >= pathLen, CHIP_ERROR_BUFFER_TOO_SMALL);
            memcpy(path.data(), slash, pathLen);
            path.reduce_size(pathLen);
        }
        else
        {
            VerifyOrReturnError(path.size() >= 1, CHIP_ERROR_BUFFER_TOO_SMALL);
            path.data()[0] = '/';
            path.reduce_size(1);
        }

        return CHIP_NO_ERROR;
    }

private:
    char mPeriodicUrl[HttpsDownloader::kMaxUrlLen]{};
    size_t mPeriodicUrlLen = 0;
};

HttpsOtaApp gHttpsOtaApp;

void RegisterHttpsOtaApp()
{
    auto & req = VendorOTARequestor::GetInstance();
    req.SetAppDelegate(&gHttpsOtaApp);
    req.SetHttpClient(&gHttpsClient);
}

#endif // SL_MATTER_ENABLE_HTTPS_OTA

} // namespace

void HttpsRunAppEvent(AppEvent * /* aEvent */)
{
    VerifyOrReturn(SL_STATUS_OK == https_client_demo_run(), ChipLogError(DeviceLayer, "https_client_demo_run failed"));
}

sl_status_t https_client_demo_start(void)
{
    if (gHttpsClient.IsRunning())
    {
#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
        RegisterHttpsOtaApp();
        ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] client start: already running");
#endif // SL_MATTER_ENABLE_HTTPS_OTA
        return SL_STATUS_ALREADY_INITIALIZED;
    }

    if (gHttpsTaskLock == nullptr)
    {
        // max_count=1, initial_count=0: Acquire blocks until completion callback Releases.
        gHttpsTaskLock = osSemaphoreNew(1, 0, nullptr);
        if (gHttpsTaskLock == nullptr)
        {
            ChipLogError(DeviceLayer, "HTTPS demo lock create failed");
            return SL_STATUS_FAIL;
        }
    }

    VerifyOrReturnError(sizeof(kCaCertExample) > 0, SL_STATUS_FAIL, ChipLogError(DeviceLayer, "CA certificate is not set"));

    const HttpClientConfig config = {
        // Enable HTTPS client.
        .httpsEnable  = true,
        .username     = kHttpClientUser,
        .password     = kHttpClientPass,
        .tlsCaCert    = reinterpret_cast<const uint8_t *>(kCaCertExample),
        .tlsCaCertLen = sizeof(kCaCertExample) - 1,
    };

    CHIP_ERROR err = gHttpsClient.Start();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS Start failed: %" CHIP_ERROR_FORMAT, err.Format());
#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] client start: failed");
#endif // SL_MATTER_ENABLE_HTTPS_OTA
        return SL_STATUS_FAIL;
    }

    OperationWaitContext waitCtx = { .lock = gHttpsTaskLock, .result = CHIP_NO_ERROR };

    err = RunQueuedOperation(gHttpsClient.Init(config, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS Init failed: %" CHIP_ERROR_FORMAT, err.Format());
#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] client init: failed");
#endif // SL_MATTER_ENABLE_HTTPS_OTA
        CHIP_ERROR stopErr = gHttpsClient.Stop();
        if (stopErr != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "HTTPS Stop failed: %" CHIP_ERROR_FORMAT, stopErr.Format());
        }
        return SL_STATUS_FAIL;
    }

#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
    RegisterHttpsOtaApp();
    ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] client start: started");
#endif // SL_MATTER_ENABLE_HTTPS_OTA
    return SL_STATUS_OK;
}

sl_status_t https_client_demo_run(void)
{
    CHIP_ERROR err = RunHttpsOffloadExample();
    if (err != CHIP_NO_ERROR)
    {
        return SL_STATUS_FAIL;
    }
    return SL_STATUS_OK;
}

#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA

void HttpsOtaUrlAppEvent(AppEvent * /* aEvent */)
{
    // HttpClient + AppDelegate are registered from StartMatterServicesAppEvent /
    // https_client_demo_start on connectivity. StartUpdate fails with incorrect-state
    // if the client is not running yet (same contract as the periodic check).
    CHIP_ERROR schedErr = chip::DeviceLayer::SystemLayer().ScheduleLambda([] {
        CHIP_ERROR err =
            chip::VendorOTARequestor::GetInstance().StartUpdate(chip::CharSpan(gPendingOtaUrl, gPendingOtaUrlLen));
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] StartUpdate failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    });
    if (schedErr != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] ScheduleLambda failed: %" CHIP_ERROR_FORMAT, schedErr.Format());
    }
}

CHIP_ERROR HttpsOtaQueueUrl(const char * url)
{
    VerifyOrReturnError(url != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const size_t len = strlen(url);
    VerifyOrReturnError(len > 0 && len < sizeof(gPendingOtaUrl), CHIP_ERROR_INVALID_ARGUMENT);
    memcpy(gPendingOtaUrl, url, len);
    gPendingOtaUrl[len] = '\0';
    gPendingOtaUrlLen   = len;

    ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] using passed url=%s", gPendingOtaUrl);

    AppEvent event = {};
    event.Type     = AppEvent::kEventType_Timer;
    event.Handler  = HttpsOtaUrlAppEvent;
    BaseApplication::PostEvent(&event);
    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpsOtaAbort(void)
{
    TEMPORARY_RETURN_IGNORED chip::DeviceLayer::SystemLayer().ScheduleLambda(
        [] { chip::VendorOTARequestor::GetInstance().CancelImageUpdate(); });
    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpsOtaSetPeriodicUrl(const char * url)
{
    VerifyOrReturnError(url != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const size_t len = strlen(url);
    VerifyOrReturnError(len > 0 && len < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
    return gHttpsOtaApp.SetPeriodicUrl(CharSpan(url, len));
}

CHIP_ERROR HttpsOtaStatusCommand(void)
{
    auto & req = VendorOTARequestor::GetInstance();
    auto * dl  = req.GetHttpsDownloader();
    auto * ip  = req.GetImageProcessor();

    using OTAUpdateStateEnum = chip::OTARequestorInterface::OTAUpdateStateEnum;
    const char * stateStr    = "idle";
    switch (req.GetCurrentUpdateState())
    {
    case OTAUpdateStateEnum::kIdle:
        stateStr = "idle";
        break;
    case OTAUpdateStateEnum::kQuerying:
        stateStr = "querying";
        break;
    case OTAUpdateStateEnum::kDownloading:
        stateStr = "downloading";
        break;
    case OTAUpdateStateEnum::kApplying:
        stateStr = "applying";
        break;
    default:
        stateStr = "other";
        break;
    }

    const uint32_t offset = (dl != nullptr) ? dl->GetNextOffset() : 0;
    const uint64_t bytes  = (ip != nullptr) ? ip->GetBytesDownloaded() : 0;
    auto percent          = (ip != nullptr) ? ip->GetPercentComplete() : chip::app::DataModel::Nullable<uint8_t>{};

    streamer_printf(streamer_get(), "HTTPS OTA state=%s offset=%" PRIu32 " downloaded=%" PRIu64, stateStr, offset, bytes);
    if (percent.IsNull())
    {
        streamer_printf(streamer_get(), " total=unknown percent=n/a");
    }
    else
    {
        streamer_printf(streamer_get(), " percent=%u", percent.Value());
    }
    streamer_printf(streamer_get(), " target=%" PRIu32, req.GetTargetVersion());

    CHIP_ERROR lastErr;
    VendorOTARequestor::LastResultReason reason;
    TEMPORARY_RETURN_IGNORED req.GetLastResult(lastErr, reason);
    streamer_printf(streamer_get(), " last=%s 0x%08" PRIX32 "\r\n", VendorOTARequestor::LastResultReasonToString(reason),
                    lastErr.AsInteger());
    return CHIP_NO_ERROR;
}

#endif // SL_MATTER_ENABLE_HTTPS_OTA
