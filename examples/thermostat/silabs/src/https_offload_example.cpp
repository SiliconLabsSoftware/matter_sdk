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
#endif

#include <cinttypes>
#include <cstring>

namespace {

using chip::ByteSpan;
using chip::CharSpan;
using chip::MutableByteSpan;
using chip::DeviceLayer::Silabs::HttpClient;
using chip::DeviceLayer::Silabs::HttpClientConfig;
using chip::DeviceLayer::Silabs::HttpHost;

// TODO: Update the below values to the correct values for your server.
constexpr char kHttpServerIp[]   = HTTP_SERVER_IP;
constexpr char kHttpHostname[]   = HTTP_HOSTNAME; // used for SNI validation
constexpr uint16_t kHttpPort     = HTTP_PORT;
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
        .hostName = kHttpHostname,
        .serverIp = kHttpServerIp,
        .port     = kHttpPort,
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
using chip::VendorOTARequestor;
using chip::Shell::streamer_get;
using chip::Shell::streamer_printf;

char gPendingOtaUrl[HttpsDownloader::kMaxUrlLen];
size_t gPendingOtaUrlLen = 0;

void PrintHttpsOtaHelp()
{
    streamer_printf(streamer_get(), "Usage: demo https-ota <url|abort|default|status>\r\n");
    streamer_printf(streamer_get(), "  url <url>       Start one-shot HTTPS OTA (no NVM write)\r\n");
    streamer_printf(streamer_get(), "  abort           Cancel in-progress HTTPS OTA\r\n");
    streamer_printf(streamer_get(), "  default <url>   Persist default URL for 24h check (NVM)\r\n");
    streamer_printf(streamer_get(), "  status          Show state / progress / last result\r\n");
}

CHIP_ERROR HttpsOtaStatusCommand()
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

} // namespace

void HttpsRunAppEvent(AppEvent * /* aEvent */)
{
    VerifyOrReturn(SL_STATUS_OK == https_client_demo_run(), ChipLogError(DeviceLayer, "https_client_demo_run failed"));
}

#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
void https_client_bind_ota_downloader(void)
{
    if (!gHttpsClient.IsRunning())
    {
        return;
    }
    auto * dl = chip::VendorOTARequestor::GetInstance().GetHttpsDownloader();
    if (dl == nullptr)
    {
        return;
    }
    dl->SetHttpClient(&gHttpsClient);
}
#endif // SL_MATTER_ENABLE_HTTPS_OTA

sl_status_t https_client_demo_start(void)
{
    if (gHttpsClient.IsRunning())
    {
#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
        https_client_bind_ota_downloader();
        ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] client start: already running");
#endif
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
#endif
        return SL_STATUS_FAIL;
    }

    OperationWaitContext waitCtx = { .lock = gHttpsTaskLock, .result = CHIP_NO_ERROR };

    err = RunQueuedOperation(gHttpsClient.Init(config, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS Init failed: %" CHIP_ERROR_FORMAT, err.Format());
#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] client init: failed");
#endif
        CHIP_ERROR stopErr = gHttpsClient.Stop();
        if (stopErr != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "HTTPS Stop failed: %" CHIP_ERROR_FORMAT, stopErr.Format());
        }
        return SL_STATUS_FAIL;
    }

#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
    https_client_bind_ota_downloader();
    ChipLogDetail(SoftwareUpdate, "[HTTPS-OTA] client start: started");
#endif
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
    sl_status_t st = https_client_demo_start();
    if ((st != SL_STATUS_OK) && (st != SL_STATUS_ALREADY_INITIALIZED))
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] RESULT: FAIL client-start 0x%08" PRIX32,
                     CHIP_ERROR_INCORRECT_STATE.AsInteger());
        return;
    }

    CHIP_ERROR schedErr = chip::DeviceLayer::SystemLayer().ScheduleLambda([] {
        auto * dl = chip::VendorOTARequestor::GetInstance().GetHttpsDownloader();
        if (dl != nullptr)
        {
            dl->SetHttpClient(&gHttpsClient);
        }
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

CHIP_ERROR HttpsOtaDemoCommand(int argc, char ** argv)
{
    if (argc == 0)
    {
        PrintHttpsOtaHelp();
        return CHIP_NO_ERROR;
    }

    if (strcmp(argv[0], "url") == 0)
    {
        VerifyOrReturnError(argc == 2, CHIP_ERROR_INVALID_ARGUMENT);
        const size_t len = strlen(argv[1]);
        VerifyOrReturnError(len > 0 && len < sizeof(gPendingOtaUrl), CHIP_ERROR_INVALID_ARGUMENT);
        memcpy(gPendingOtaUrl, argv[1], len);
        gPendingOtaUrl[len] = '\0';
        gPendingOtaUrlLen   = len;

        ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] session start source=shell-url url=%s", gPendingOtaUrl);

        AppEvent event = {};
        event.Type     = AppEvent::kEventType_Timer;
        event.Handler  = HttpsOtaUrlAppEvent;
        BaseApplication::PostEvent(&event);
        return CHIP_NO_ERROR;
    }

    if (strcmp(argv[0], "abort") == 0)
    {
        VerifyOrReturnError(argc == 1, CHIP_ERROR_INVALID_ARGUMENT);
        TEMPORARY_RETURN_IGNORED chip::DeviceLayer::SystemLayer().ScheduleLambda(
            [] { chip::VendorOTARequestor::GetInstance().CancelImageUpdate(); });
        return CHIP_NO_ERROR;
    }

    if (strcmp(argv[0], "default") == 0)
    {
        VerifyOrReturnError(argc == 2, CHIP_ERROR_INVALID_ARGUMENT);
        const size_t len = strlen(argv[1]);
        VerifyOrReturnError(len > 0 && len < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
        return VendorOTARequestor::GetInstance().SetDefaultUrl(CharSpan(argv[1], len));
    }

    if (strcmp(argv[0], "status") == 0)
    {
        VerifyOrReturnError(argc == 1, CHIP_ERROR_INVALID_ARGUMENT);
        return HttpsOtaStatusCommand();
    }

    PrintHttpsOtaHelp();
    return CHIP_ERROR_INVALID_ARGUMENT;
}

#endif // SL_MATTER_ENABLE_HTTPS_OTA
