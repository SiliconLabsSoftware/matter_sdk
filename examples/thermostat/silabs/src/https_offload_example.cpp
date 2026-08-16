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

#include "https_offload_example.h"

#include "http_client.h"

#include "cacert.pem.h"
#include "cmsis_os2.h"
#include "index.html.h"

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

constexpr char kHttpServerIp[]   = "192.168.0.191";
constexpr char kHttpHostname[]   = "example.com";
constexpr uint16_t kHttpPort     = 8443;
constexpr char kHttpUrl[]        = "/index.html";
constexpr char kHttpClientUser[] = "admin";
constexpr char kHttpClientPass[] = "admin";
constexpr char kHttpPostData[] =
    "employee_name=MR.REDDY&employee_id=RSXYZ123&designation=Engineer&company=SILABS&location=Hyderabad";

HttpClient gHttpClient;

// Binary semaphore: CMSIS mutex cannot be released from another thread; NWP/HTTP
// completion callbacks run on the HttpClient service thread.
osSemaphoreId_t gDoneLock = nullptr;

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

    OperationWaitContext waitCtx = { .lock = gDoneLock, .result = CHIP_NO_ERROR };

    const HttpHost host = {
        .hostName = kHttpHostname,
        .serverIp = kHttpServerIp,
        .port     = kHttpPort,
    };

    const ByteSpan putBody(reinterpret_cast<const uint8_t *>(sl_index), sizeof(sl_index) - 1);
    CHIP_ERROR err = RunQueuedOperation(gHttpClient.Put(host, kHttpUrl, putBody, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS PUT failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    ChipLogProgress(DeviceLayer, "HTTPS PUT request success");

    // Sized for the served index.html body; a smaller buffer truncates the response.
    uint8_t responseBuf[sizeof(sl_index)] = { 0 };
    MutableByteSpan responseSpan(responseBuf);
    err = RunQueuedOperation(gHttpClient.Get(host, kHttpUrl, responseSpan, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS GET failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    ChipLogProgress(DeviceLayer, "HTTPS GET request success");

    const ByteSpan postBody(reinterpret_cast<const uint8_t *>(kHttpPostData), strlen(kHttpPostData));
    err = RunQueuedOperation(gHttpClient.Post(host, kHttpUrl, postBody, OnOperationDone, &waitCtx), waitCtx);
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

extern "C" sl_status_t http_client_demo_start(void)
{
    if (gHttpClient.IsRunning())
    {
        return SL_STATUS_ALREADY_INITIALIZED;
    }

    if (gDoneLock == nullptr)
    {
        // max_count=1, initial_count=0: Acquire blocks until completion callback Releases.
        gDoneLock = osSemaphoreNew(1, 0, nullptr);
        if (gDoneLock == nullptr)
        {
            ChipLogError(DeviceLayer, "HTTPS demo lock create failed");
            return SL_STATUS_FAIL;
        }
    }

    const HttpClientConfig config = {
        .certificateIndex = 1,
        .httpsEnable      = true,
        .username         = kHttpClientUser,
        .password         = kHttpClientPass,
        .tlsCaCert        = reinterpret_cast<const uint8_t *>(cacert),
        .tlsCaCertLen     = sizeof(cacert) - 1,
    };

    CHIP_ERROR err = gHttpClient.Start();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS Start failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    OperationWaitContext waitCtx = { .lock = gDoneLock, .result = CHIP_NO_ERROR };
    err                          = RunQueuedOperation(gHttpClient.Init(config, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS Init failed: %" CHIP_ERROR_FORMAT, err.Format());
        CHIP_ERROR stopErr = gHttpClient.Stop();
        if (stopErr != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "HTTPS Stop failed: %" CHIP_ERROR_FORMAT, stopErr.Format());
        }
        return SL_STATUS_FAIL;
    }

    err = RunHttpsOffloadExample();

    CHIP_ERROR deinitErr = RunQueuedOperation(gHttpClient.Deinit(OnOperationDone, &waitCtx), waitCtx);
    if (deinitErr != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS deinit failed: %" CHIP_ERROR_FORMAT, deinitErr.Format());
    }

    CHIP_ERROR stopErr = gHttpClient.Stop();
    if (stopErr != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS Stop failed: %" CHIP_ERROR_FORMAT, stopErr.Format());
    }

    return (err == CHIP_NO_ERROR && deinitErr == CHIP_NO_ERROR && stopErr == CHIP_NO_ERROR) ? SL_STATUS_OK : SL_STATUS_FAIL;
}
