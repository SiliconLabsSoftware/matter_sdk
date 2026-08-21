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

#include "cacert.h"
#include "index_html.h"

#include "http_client.h"

#include "cmsis_os2.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>

#include <cstring>

namespace {

using chip::ByteSpan;
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

// HTTPS client instance.
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

} // namespace

sl_status_t https_client_demo_start(void)
{
    if (gHttpsClient.IsRunning())
    {
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
        .certificateIndex = 1,
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
        return SL_STATUS_FAIL;
    }

    OperationWaitContext waitCtx = { .lock = gHttpsTaskLock, .result = CHIP_NO_ERROR };

    err = RunQueuedOperation(gHttpsClient.Init(config, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS Init failed: %" CHIP_ERROR_FORMAT, err.Format());
        CHIP_ERROR stopErr = gHttpsClient.Stop();
        if (stopErr != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "HTTPS Stop failed: %" CHIP_ERROR_FORMAT, stopErr.Format());
        }
        return SL_STATUS_FAIL;
    }

    err = RunHttpsOffloadExample();

    /* NOTE: Deinit and Stop are not needed for the HTTP client.
        CHIP_ERROR deinitErr = RunQueuedOperation(gHttpsClient.Deinit(OnOperationDone, &waitCtx), waitCtx);
        if (deinitErr != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "HTTPS deinit failed: %" CHIP_ERROR_FORMAT, deinitErr.Format());
        }

        CHIP_ERROR stopErr = gHttpsClient.Stop();
        if (stopErr != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "HTTPS Stop failed: %" CHIP_ERROR_FORMAT, stopErr.Format());
        }
    */
    return (err == CHIP_NO_ERROR
            // && deinitErr == CHIP_NO_ERROR
            // && stopErr == CHIP_NO_ERROR
            )
        ? SL_STATUS_OK
        : SL_STATUS_FAIL;
}
