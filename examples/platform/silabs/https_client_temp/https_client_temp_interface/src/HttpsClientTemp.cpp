/**
 * @file
 * @brief HTTPS Client Temp task and request lifecycle.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#include "HttpsClientTemp.h"
#include "HttpsClientTempConfig.h"
#include "HttpsClientTempNvmCert.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#include <lwip/err.h>
#include <lwip/ip_addr.h>
#include <lwip/opt.h>
#include <lwip/tcpip.h>

#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>

#include "silabs_utils.h"

#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

#include "HTTPS_transport.h"
#include "HTTPS_client.h"

#ifdef __cplusplus
}
#endif

const char HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA[] = "-----BEGIN CERTIFICATE-----\r\n"
                                               "-----END CERTIFICATE-----\r\n";

const size_t HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA_LEN = sizeof(HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA);

namespace {

static TaskHandle_t httpsClientTempTask         = nullptr;
static EventGroupHandle_t httpsClientTempEvents = nullptr;
static HTTPS_Transport_t * transport            = nullptr;
static httpsClientTemp_ready_cb gReadyCb        = nullptr;
static bool initComplete                        = false;
static bool requestInFlight = false;

static httpsClientTemp_request_t gPendingRequest{};
static bool gPendingRequestValid = false;

const char * HttpsClientTempErrToString(httpsClientTemp_err_t err)
{
    switch (err)
    {
    case HTTPS_CLIENT_TEMP_OK:
        return "OK";
    case HTTPS_CLIENT_TEMP_ERR_INVAL:
        return "INVAL";
    case HTTPS_CLIENT_TEMP_ERR_MEM:
        return "MEM";
    case HTTPS_CLIENT_TEMP_ERR_FAIL:
        return "FAIL";
    case HTTPS_CLIENT_TEMP_ERR_BUSY:
        return "BUSY";
    default:
        return "UNKNOWN";
    }
}

httpsClientTemp_err_t LwIPErrToHttpsClientTemp(err_t err)
{
    switch (err)
    {
    case ERR_OK:
        return HTTPS_CLIENT_TEMP_OK;
    case ERR_ARG:
        return HTTPS_CLIENT_TEMP_ERR_INVAL;
    case ERR_MEM:
        return HTTPS_CLIENT_TEMP_ERR_MEM;
    case ERR_INPROGRESS:
        return HTTPS_CLIENT_TEMP_ERR_BUSY;
    default:
        return HTTPS_CLIENT_TEMP_ERR_FAIL;
    }
}

const char * LwIPErrToString(err_t err)
{
#if LWIP_DEBUG || 1
    return lwip_strerr(err);
#else
    (void) err;
    return "unknown";
#endif
}

void LogLwIPDispatchDiagnostics(const char * phase)
{
    ChipLogProgress(AppServer,
                    "[HTTPS_CLIENT_TEMP] lwip diag (%s): MEM_SIZE=%u MEM_LIBC_MALLOC=%d "
                    "MEMP_NUM_TCP_PCB=%u PBUF_POOL_SIZE=%u heap_free=%u heap_min=%u request_in_flight=%d",
                    phase, static_cast<unsigned>(MEM_SIZE), static_cast<int>(MEM_LIBC_MALLOC),
                    static_cast<unsigned>(MEMP_NUM_TCP_PCB), static_cast<unsigned>(PBUF_POOL_SIZE),
                    static_cast<unsigned>(xPortGetFreeHeapSize()),
                    static_cast<unsigned>(xPortGetMinimumEverFreeHeapSize()), requestInFlight ? 1 : 0);
    ChipLogProgress(AppServer, "[HTTPS_CLIENT_TEMP] lwip diag (%s): tls_config=%p transport=%p", phase,
                    static_cast<void *>(transport ? HTTPS_Transport_GetTlsConfig(transport) : nullptr),
                    static_cast<void *>(transport));
}

void HttpDoneCallback(void * arg, err_t err, int http_status, u32_t rx_bytes)
{
    (void) arg;

    if (err != ERR_OK || http_status < 200 || http_status >= 300)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] request failed: lwip_err=%d (%s) http_status=%d rx=%lu",
                     static_cast<int>(err), LwIPErrToString(err), http_status, static_cast<unsigned long>(rx_bytes));
    }
    else
    {
        ChipLogProgress(AppServer, "[HTTPS_CLIENT_TEMP] request completed: http_status=%d rx=%lu", http_status,
                        static_cast<unsigned long>(rx_bytes));
    }

    requestInFlight = false;

    if (httpsClientTempEvents != nullptr)
    {
        xEventGroupSetBits(httpsClientTempEvents, HTTPS_CLIENT_TEMP_SIGNAL_REQUEST_DONE);
    }
}

struct InitTlsJob
{
    HTTPS_Transport_t * transport;
    const u8_t * ca;
    size_t caLen;
    httpsClientTemp_err_t result;
};

void InitTlsOnTcpipThread(void * context)
{
    auto * job = static_cast<InitTlsJob *>(context);

    if (HTTPS_Transport_IsInitialized(job->transport))
    {
        job->result = HTTPS_CLIENT_TEMP_OK;
        return;
    }

    const u8_t * ca    = job->ca;
    size_t caLen       = job->caLen;

#if HTTPS_CLIENT_TEMP_SKIP_CA_VERIFY
    ca    = nullptr;
    caLen = 0;
    ChipLogProgress(AppServer, "[HTTPS_CLIENT_TEMP] CA verify disabled, no root CA loaded");
#else
    if (ca == nullptr || caLen == 0)
    {
        ca    = reinterpret_cast<const u8_t *>(HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA);
        caLen = HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA_LEN;
    }
#endif

    err_t ret = HTTPS_Transport_SSLConfigure(job->transport, ca, caLen);
    if (ret != ERR_OK)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] HTTPS_Transport_SSLConfigure failed: %d", static_cast<int>(ret));
        job->result = HTTPS_CLIENT_TEMP_ERR_MEM;
        return;
    }

    ChipLogProgress(AppServer, "[HTTPS_CLIENT_TEMP] TLS config OK (ca_len=%u)", static_cast<unsigned>(caLen));
    job->result = HTTPS_CLIENT_TEMP_OK;
}

struct RequestJob
{
    httpsClientTemp_request_t request;
    HTTPS_Transport_t * transport;
    err_t result;
};

void RequestOnTcpipThread(void * context)
{
    auto * job = static_cast<RequestJob *>(context);

    if (!HTTPS_Transport_IsInitialized(job->transport))
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] request rejected: transport not initialized");
        job->result = ERR_VAL;
        return;
    }

    if (requestInFlight)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] request rejected: another request is in flight");
        job->result = ERR_INPROGRESS;
        return;
    }

    if (job->request.host == nullptr || job->request.uri == nullptr)
    {
        job->result = ERR_ARG;
        return;
    }

    const size_t heapBefore     = xPortGetFreeHeapSize();
    const size_t heapMinBefore  = xPortGetMinimumEverFreeHeapSize();
    LogLwIPDispatchDiagnostics("pre-dispatch");

    if (heapBefore < HTTPS_CLIENT_TEMP_MIN_HEAP_FOR_GET)
    {
        ChipLogError(AppServer,
                     "[HTTPS_CLIENT_TEMP] request rejected: insufficient heap (need>=%u free=%u min=%u)",
                     static_cast<unsigned>(HTTPS_CLIENT_TEMP_MIN_HEAP_FOR_GET), static_cast<unsigned>(heapBefore),
                     static_cast<unsigned>(heapMinBefore));
        job->result = ERR_MEM;
        return;
    }

    const uint16_t port = job->request.port == 0 ? HTTPS_CLIENT_TEMP_DEFAULT_PORT : job->request.port;

    ip_addr_t serverAddr;
    if (!ipaddr_aton(job->request.host, &serverAddr))
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] invalid server IP: %s", job->request.host);
        job->result = ERR_ARG;
        return;
    }

    ChipLogProgress(AppServer, "[HTTPS_CLIENT_TEMP] starting GET https://%s:%u%s (forked altcp heap_free=%u heap_min=%u)",
                    job->request.host, port, job->request.uri, static_cast<unsigned>(heapBefore),
                    static_cast<unsigned>(heapMinBefore));

    job->result = HTTPS_Client_Get(job->transport, &serverAddr, port, job->request.host, job->request.uri, HttpDoneCallback,
                                   nullptr);

    const size_t heapAfter    = xPortGetFreeHeapSize();
    const size_t heapMinAfter = xPortGetMinimumEverFreeHeapSize();
    if (job->result == ERR_OK)
    {
        requestInFlight = true;
    }
    else
    {
        LogLwIPDispatchDiagnostics("post-fail");
        ChipLogError(AppServer,
                     "[HTTPS_CLIENT_TEMP] HTTP GET dispatch failed: server=%s port=%u uri=%s lwip_err=%d (%s) "
                     "heap_before=%u heap_after=%u heap_delta=%d heap_min_before=%u heap_min_after=%u",
                     job->request.host, port, job->request.uri, static_cast<int>(job->result), LwIPErrToString(job->result),
                     static_cast<unsigned>(heapBefore), static_cast<unsigned>(heapAfter),
                     static_cast<int>(heapAfter) - static_cast<int>(heapBefore), static_cast<unsigned>(heapMinBefore),
                     static_cast<unsigned>(heapMinAfter));
    }
}

httpsClientTemp_err_t DispatchRequestOnTcpipThread(const httpsClientTemp_request_t * request)
{
    RequestJob job = { *request, transport, ERR_OK };
    err_t schedule = tcpip_callback_wait(RequestOnTcpipThread, &job);
    if (schedule != ERR_OK)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] tcpip_callback_wait failed: lwip_err=%d (%s) heap_free=%u",
                     static_cast<int>(schedule), LwIPErrToString(schedule), static_cast<unsigned>(xPortGetFreeHeapSize()));
        return LwIPErrToHttpsClientTemp(schedule);
    }
    return LwIPErrToHttpsClientTemp(job.result);
}

httpsClientTemp_err_t InitTlsFromTask()
{
    char ca_cert_buf[HTTPS_CLIENT_TEMP_CA_CERT_LENGTH] = { 0 };
    size_t ca_cert_length                              = 0;
    const size_t heapFree                              = xPortGetFreeHeapSize();

    if (heapFree < HTTPS_CLIENT_TEMP_MIN_HEAP_FOR_TLS)
    {
        ChipLogError(AppServer,
                     "[HTTPS_CLIENT_TEMP] TLS init skipped: insufficient heap (need>=%u free=%u min=%u)",
                     static_cast<unsigned>(HTTPS_CLIENT_TEMP_MIN_HEAP_FOR_TLS), static_cast<unsigned>(heapFree),
                     static_cast<unsigned>(xPortGetMinimumEverFreeHeapSize()));
        return HTTPS_CLIENT_TEMP_ERR_MEM;
    }

    VerifyOrReturnError(HttpsClientTempGetRootCA(ca_cert_buf, HTTPS_CLIENT_TEMP_CA_CERT_LENGTH, &ca_cert_length) == CHIP_NO_ERROR,
                        HTTPS_CLIENT_TEMP_ERR_FAIL,
                        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] failed to fetch root CA"));

    InitTlsJob job = { transport, reinterpret_cast<const u8_t *>(ca_cert_buf), ca_cert_length, HTTPS_CLIENT_TEMP_ERR_FAIL };

    err_t schedule = tcpip_callback_wait(InitTlsOnTcpipThread, &job);
    if (schedule != ERR_OK)
    {
        return LwIPErrToHttpsClientTemp(schedule);
    }

    return job.result;
}

void RunPendingRequest()
{
    httpsClientTemp_request_t request = {};
    if (gPendingRequestValid)
    {
        request              = gPendingRequest;
        gPendingRequestValid = false;
    }
    else
    {
        request.host = HTTPS_CLIENT_TEMP_DEFAULT_SERVER;
        request.uri  = HTTPS_CLIENT_TEMP_DEFAULT_URI;
        request.port = HTTPS_CLIENT_TEMP_DEFAULT_PORT;
    }

    if (httpsClientTempEvents != nullptr)
    {
        xEventGroupClearBits(httpsClientTempEvents, HTTPS_CLIENT_TEMP_SIGNAL_REQUEST_DONE);
    }

    httpsClientTemp_err_t err = DispatchRequestOnTcpipThread(&request);
    if (err != HTTPS_CLIENT_TEMP_OK)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] request dispatch failed: %s (host=%s uri=%s port=%u heap_free=%u heap_min=%u)",
                     HttpsClientTempErrToString(err), request.host != nullptr ? request.host : "(null)",
                     request.uri != nullptr ? request.uri : "(null)", request.port,
                     static_cast<unsigned>(xPortGetFreeHeapSize()),
                     static_cast<unsigned>(xPortGetMinimumEverFreeHeapSize()));
        return;
    }

    if (httpsClientTempEvents != nullptr)
    {
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(HTTPS_CLIENT_TEMP_REQUEST_TIMEOUT_MS);
        bool requestDone          = false;

        while (!requestDone)
        {
            TickType_t now     = xTaskGetTickCount();
            TickType_t timeout = (deadline > now) ? (deadline - now) : 0;

            EventBits_t bits = xEventGroupWaitBits(
                httpsClientTempEvents, HTTPS_CLIENT_TEMP_SIGNAL_REQUEST_DONE | HTTPS_CLIENT_TEMP_SIGNAL_MBEDTLS_RX, pdTRUE,
                pdFALSE, timeout);

            if (bits & HTTPS_CLIENT_TEMP_SIGNAL_MBEDTLS_RX)
            {
                https_client_temp_transport_process_mbedtls_rx();
            }

            if (bits & HTTPS_CLIENT_TEMP_SIGNAL_REQUEST_DONE)
            {
                requestDone = true;
            }
            else if (timeout == 0)
            {
                break;
            }
        }

        if (!requestDone)
        {
            requestInFlight = false;
            ChipLogError(AppServer,
                         "[HTTPS_CLIENT_TEMP] request timed out after %u ms (heap_free=%u heap_min=%u) - check TLS/connect/mem",
                         HTTPS_CLIENT_TEMP_REQUEST_TIMEOUT_MS, static_cast<unsigned>(xPortGetFreeHeapSize()),
                         static_cast<unsigned>(xPortGetMinimumEverFreeHeapSize()));
        }
    }
}

static void HttpsClientTempTaskFn(void * args)
{
    bool endLoop = false;
    gReadyCb     = reinterpret_cast<httpsClientTemp_ready_cb>(args);

    ChipLogProgress(AppServer, "[HTTPS_CLIENT_TEMP] task started, delay %u s", HTTPS_CLIENT_TEMP_INIT_DELAY_SEC);
    vTaskDelay(pdMS_TO_TICKS(HTTPS_CLIENT_TEMP_INIT_DELAY_SEC * 1000));

    transport = HTTPS_Transport_Init(httpsClientTempEvents);
    VerifyOrExit(transport != nullptr, ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] failed to init transport"));

    VerifyOrExit(InitTlsFromTask() == HTTPS_CLIENT_TEMP_OK,
                 ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] TLS init failed"));

    initComplete = true;
    ChipLogProgress(AppServer, "[HTTPS_CLIENT_TEMP] TLS client initialized");

    if (gReadyCb != nullptr)
    {
        gReadyCb();
    }

    while (!endLoop)
    {
        EventBits_t event = xEventGroupWaitBits(
            httpsClientTempEvents,
            HTTPS_CLIENT_TEMP_SIGNAL_REQUEST | HTTPS_CLIENT_TEMP_SIGNAL_CONN_CLOSE | HTTPS_CLIENT_TEMP_SIGNAL_MBEDTLS_RX,
            pdTRUE, pdFALSE, portMAX_DELAY);

        if (event & HTTPS_CLIENT_TEMP_SIGNAL_MBEDTLS_RX)
        {
            https_client_temp_transport_process_mbedtls_rx();
        }

        if (event & HTTPS_CLIENT_TEMP_SIGNAL_CONN_CLOSE)
        {
            endLoop = true;
        }
        else if (event & HTTPS_CLIENT_TEMP_SIGNAL_REQUEST)
        {
            RunPendingRequest();
        }
    }

    initComplete = false;

exit:
    if (transport != nullptr)
    {
        HTTPS_Transport_Deinit(transport);
        transport = nullptr;
    }

    if (httpsClientTempEvents != nullptr)
    {
        vEventGroupDelete(httpsClientTempEvents);
        httpsClientTempEvents = nullptr;
    }

    httpsClientTempTask = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

extern "C" {

httpsClientTemp_err_t HttpsClientTempInit(httpsClientTemp_ready_cb ready_cb)
{
    if (httpsClientTempTask != nullptr)
    {
        return HTTPS_CLIENT_TEMP_OK;
    }

    httpsClientTempEvents = xEventGroupCreate();
    if (httpsClientTempEvents == nullptr)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] failed to create event group");
        return HTTPS_CLIENT_TEMP_ERR_MEM;
    }

    if ((pdPASS != xTaskCreate(HttpsClientTempTaskFn, HTTPS_CLIENT_TEMP_TASK_NAME, HTTPS_CLIENT_TEMP_TASK_STACK_SIZE,
                               reinterpret_cast<void *>(ready_cb), HTTPS_CLIENT_TEMP_TASK_PRIORITY, &httpsClientTempTask)) ||
        httpsClientTempTask == nullptr)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] failed to create task");
        vEventGroupDelete(httpsClientTempEvents);
        httpsClientTempEvents = nullptr;
        return HTTPS_CLIENT_TEMP_ERR_MEM;
    }

    ChipLogProgress(AppServer, "[HTTPS_CLIENT_TEMP] task created (stack=%u)", HTTPS_CLIENT_TEMP_TASK_STACK_SIZE);
    return HTTPS_CLIENT_TEMP_OK;
}

httpsClientTemp_err_t HttpsClientTempRequestGet(const httpsClientTemp_request_t * request)
{
    if (request == nullptr || request->host == nullptr || request->uri == nullptr)
    {
        return HTTPS_CLIENT_TEMP_ERR_INVAL;
    }

    if (httpsClientTempTask == nullptr || httpsClientTempEvents == nullptr)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] request rejected: task not running");
        return HTTPS_CLIENT_TEMP_ERR_FAIL;
    }

    if (requestInFlight)
    {
        return HTTPS_CLIENT_TEMP_ERR_BUSY;
    }

    gPendingRequest      = *request;
    gPendingRequestValid = true;
    xEventGroupSetBits(httpsClientTempEvents, HTTPS_CLIENT_TEMP_SIGNAL_REQUEST);

    return HTTPS_CLIENT_TEMP_OK;
}

} // extern "C"
