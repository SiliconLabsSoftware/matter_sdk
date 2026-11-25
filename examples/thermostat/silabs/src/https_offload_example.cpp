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
 * 4. Run the HTTPS PUT request, with its own headers, to send the index.html file to the server
 * 5. Run the HTTPS GET request, with its own headers, to get the index.html file from the server
 * 6. Run the HTTPS POST request, with its own headers, to send the post data to the server
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
#include <platform/silabs/services/https_downloader.h>
#include <platform/silabs/services/vendor_ota_requestor.h>

#if (SL_MATTER_GN_BUILD == 0)
#include "sl_matter_https_ota_config.h"
#endif // SL_MATTER_GN_BUILD
#endif // SL_MATTER_ENABLE_HTTPS_OTA

#include <cinttypes>
#include <cstring>

namespace {

using chip::ByteSpan;
using chip::CharSpan;
using chip::MutableByteSpan;
using chip::DeviceLayer::Silabs::HttpClient;
using chip::DeviceLayer::Silabs::HttpClientConfig;
using chip::DeviceLayer::Silabs::HttpHeader;
using chip::DeviceLayer::Silabs::HttpHost;

// TODO: Update the below values to the correct values for your server.
constexpr char kHttpServerEndpoint[] = HTTP_SERVER_ENDPOINT; // DNS name or IPv4
constexpr char kHttpTlsHostname[]    = HTTP_TLS_HOSTNAME;    // SNI / Host header
constexpr uint16_t kHttpPort         = HTTP_PORT;
constexpr char kHttpUrl[]            = HTTP_URL;
constexpr char kHttpClientUser[]     = HTTP_USER;
constexpr char kHttpClientPass[]     = HTTP_PASS;
constexpr char kHttpPostData[]       = HTTP_POST_DATA;

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

    // Each verb owns its headers. Expect these on the test server log
    // (see slc/tools/https_header_test_server.py).
    static constexpr HttpHeader kPutHeaders[] = {
        { "X-Matter-Demo", "https-offload" },
        { "X-Test-Verb", "PUT" },
        { "X-Test-Id", "1" },
    };
    static constexpr HttpHeader kGetHeaders[] = {
        { "X-Test-Verb", "GET" },
    };
    static constexpr HttpHeader kPostHeaders[] = {
        { "X-Test-Verb", "POST" },
        { "Content-Type", "application/x-www-form-urlencoded" },
    };

    const ByteSpan putBody(reinterpret_cast<const uint8_t *>(sl_index), sizeof(sl_index) - 1);
    CHIP_ERROR err = RunQueuedOperation(gHttpsClient.Put(host, kHttpUrl, putBody, kPutHeaders,
                                                         sizeof(kPutHeaders) / sizeof(kPutHeaders[0]), OnOperationDone, &waitCtx),
                                        waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS PUT failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    ChipLogProgress(DeviceLayer, "HTTPS PUT request success");

    // Sized for the served index.html body; a smaller buffer truncates the response.
    uint8_t responseBuf[sizeof(sl_index)] = { 0 };
    MutableByteSpan responseSpan(responseBuf);
    err = RunQueuedOperation(gHttpsClient.Get(host, kHttpUrl, responseSpan, kGetHeaders,
                                              sizeof(kGetHeaders) / sizeof(kGetHeaders[0]), OnOperationDone, &waitCtx),
                             waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HTTPS GET failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    ChipLogProgress(DeviceLayer, "HTTPS GET request success");

    const ByteSpan postBody(reinterpret_cast<const uint8_t *>(kHttpPostData), strlen(kHttpPostData));
    err = RunQueuedOperation(gHttpsClient.Post(host, kHttpUrl, postBody, kPostHeaders,
                                               sizeof(kPostHeaders) / sizeof(kPostHeaders[0]), OnOperationDone, &waitCtx),
                             waitCtx);
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
using chip::operator""_span;

// Expected URL: https://<host>[:port]/path — host is DNS or IPv4 (no IPv6).
constexpr CharSpan kHttpsScheme     = "https://"_span;
constexpr uint16_t kDefaultHttpsPort = 443;

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

/**
 * App owns URI composition and the periodic URL store.
 */
class HttpsOtaApp : public VendorOTARequestor::AppDelegate
{
public:
    HttpsOtaApp()
    {
        if (SL_MATTER_HTTPS_OTA_DEFAULT_URL[0] != '\0')
        {
            LogErrorOnFailure(SetPeriodicUrl(CharSpan::fromCharString(SL_MATTER_HTTPS_OTA_DEFAULT_URL)));
        }
    }

    CHIP_ERROR SetPeriodicUrl(CharSpan url)
    {
        VerifyOrReturnError(url.size() > 0 && url.size() < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
        MutableCharSpan out(mPeriodicUrl, sizeof(mPeriodicUrl));
        ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan(url, out));
        mPeriodicUrlLen = out.size();
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetPeriodicUrl(MutableCharSpan & outUrl) override
    {
        VerifyOrReturnError(mPeriodicUrlLen > 0, CHIP_ERROR_NOT_FOUND);
        return CopyCharSpanToMutableCharSpan(CharSpan(mPeriodicUrl, mPeriodicUrlLen), outUrl);
    }

    /**
     * Parse https://<host>[:port]/path into the downloader session fields.
     * Host may be a DNS name or IPv4 literal (IPv6 is not supported).
     * serverEndpoint and tlsHostname both receive the same host string: TCP
     * connect and TLS SNI / Host header use the URL host (no endpoint override).
     */
    CHIP_ERROR ComposeImageUri(CharSpan uri, MutableCharSpan & serverEndpoint, MutableCharSpan & tlsHostname, uint16_t & port,
                               MutableCharSpan & path) override
    {
        VerifyOrReturnError(uri.size() > 0 && uri.size() < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(uri.size() >= kHttpsScheme.size(), CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(uri.SubSpan(0, kHttpsScheme.size()).data_equal(kHttpsScheme), CHIP_ERROR_INVALID_ARGUMENT);

        // 1. Default port 443 until an explicit :port is seen in the authority.
        port             = kDefaultHttpsPort;
        CharSpan rest    = uri.SubSpan(kHttpsScheme.size());
        size_t slashPos  = 0;
        const bool hasSlash = FindFirst(rest, '/', slashPos);

        // 2. Split authority vs path. Optional :port must appear before the first '/'.
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

        // 3. Host → tlsHostname (SNI / Host) and serverEndpoint (DNS or IPv4 connect).
        VerifyOrReturnError(host.size() > 0 && host.size() < HttpsDownloader::kMaxHostLen, CHIP_ERROR_INVALID_ARGUMENT);
        ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan(host, tlsHostname));
        ReturnErrorOnFailure(CopyCharSpanToMutableCharSpan(host, serverEndpoint));

        // 4. Path including leading '/', or "/" when the URL has no path component.
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

private:
    char mPeriodicUrl[HttpsDownloader::kMaxUrlLen]{};
    size_t mPeriodicUrlLen = 0;
};

HttpsOtaApp gHttpsOtaApp;

void RegisterHttpsOtaApp()
{
    auto & req = VendorOTARequestor::GetInstance();
    LogErrorOnFailure(req.SetAppDelegate(&gHttpsOtaApp));
    LogErrorOnFailure(req.SetHttpClient(&gHttpsClient));
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

CHIP_ERROR HttpsOtaAppSetPeriodicUrl(chip::CharSpan url)
{
    return gHttpsOtaApp.SetPeriodicUrl(url);
}

#endif // SL_MATTER_ENABLE_HTTPS_OTA
