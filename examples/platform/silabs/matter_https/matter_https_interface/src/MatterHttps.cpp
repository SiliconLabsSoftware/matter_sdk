/**
 * @file
 * @brief Matter abstraction layer for HTTPS client.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#include "MatterHttps.h"
#include "MatterHttpsConfig.h"
#include "matter_https_altcp_defer.h"
#include "matter_https_tls_diag.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#include <lwip/altcp_tls.h>
#include <lwip/apps/http_client.h>
#include <lwip/err.h>
#include <lwip/opt.h>
#include <lwip/sys.h>
#include <lwip/tcpip.h>

#if MATTER_HTTPS_USE_DNS && !LWIP_DNS
#error "MATTER_HTTPS_USE_DNS requires LWIP_DNS=1 in the build (component define or lwipopts)"
#endif

#if !MATTER_HTTPS_USE_DNS
#include <lwip/ip_addr.h>
#endif

#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>

#include <cstdio>
#include <cstring>

#define MATTER_HTTPS_PRINTF(fmt, ...) printf("[MATTER_HTTPS] " fmt "\n", ##__VA_ARGS__)
#define MATTER_HTTPS_ERR_PRINTF(fmt, ...) printf("[MATTER_HTTPS][error] " fmt "\n", ##__VA_ARGS__)

const char MATTER_HTTPS_DEFAULT_ROOT_CA[] = "-----BEGIN CERTIFICATE-----\r\n"
                                            "-----END CERTIFICATE-----\r\n";

const size_t MATTER_HTTPS_DEFAULT_ROOT_CA_LEN = sizeof(MATTER_HTTPS_DEFAULT_ROOT_CA);

constexpr EventBits_t kMatterHttpsMbedtlsRxEventBit = (1 << 1);

static EventGroupHandle_t gMatterHttpsNotifyEvents = nullptr;

extern "C" void MatterHttpsNotifyMbedtlsRx(void)
{
    if (gMatterHttpsNotifyEvents != nullptr)
    {
        xEventGroupSetBits(gMatterHttpsNotifyEvents, kMatterHttpsMbedtlsRxEventBit);
    }
}

namespace {

constexpr EventBits_t kSignalHttpsDoRequest = (1 << 0);
constexpr EventBits_t kSignalHttpsMbedtlsRx = kMatterHttpsMbedtlsRxEventBit;

struct HttpcContext
{
    bool initialized             = false;
    bool requestInFlight         = false;
    altcp_tls_config * tlsConfig = nullptr;
    altcp_allocator_t allocator{};
    httpc_state_t * state = nullptr;
};

HttpcContext gHttpc;

static TaskHandle_t matterHttpsTask         = nullptr;
static EventGroupHandle_t matterHttpsEvents = nullptr;

static const char * gRootCaPem = nullptr;
static size_t gRootCaPemLen    = 0;
static matterHttps_request_t gPendingRequest{};
static bool gPendingRequestValid  = false;
static bool gLastRequestFailed    = false;
static volatile bool gRequestDone = false;

extern "C" void uartFlushTxQueue(void);

#if LWIP_TCPIP_CORE_LOCKING
struct TcpipSyncContext
{
    tcpip_callback_fn worker;
    void * arg;
    sys_sem_t doneSem;
};

static void TcpipSyncTrampoline(void * ctx)
{
    auto * sync = static_cast<TcpipSyncContext *>(ctx);
    sync->worker(sync->arg);
    sys_sem_signal(&sync->doneSem);
}

static err_t RunOnTcpipThreadSync(tcpip_callback_fn worker, void * arg)
{
    TcpipSyncContext sync = { worker, arg, {} };
    err_t err             = sys_sem_new(&sync.doneSem, 0);
    if (err != ERR_OK)
    {
        return err;
    }

    err = tcpip_callback(TcpipSyncTrampoline, &sync);
    if (err != ERR_OK)
    {
        sys_sem_free(&sync.doneSem);
        return err;
    }

    sys_arch_sem_wait(&sync.doneSem, 0);
    sys_sem_free(&sync.doneSem);
    return ERR_OK;
}
#else
static err_t RunOnTcpipThreadSync(tcpip_callback_fn worker, void * arg)
{
    return tcpip_callback_wait(worker, arg);
}
#endif

void LogMbedtlsBuildFlags();
void LogTlsDebugConfig()
{
#if defined(LWIP_DEBUG) && LWIP_DEBUG
    const int lwipDebug = 1;
#else
    const int lwipDebug = 0;
#endif
#if defined(LWIP_ALTCP_TLS_MBEDTLS) && LWIP_ALTCP_TLS_MBEDTLS
    const int altcpTlsMbedtls = 1;
#else
    const int altcpTlsMbedtls = 0;
#endif
#if defined(ALTCP_MBEDTLS_DEBUG) && (ALTCP_MBEDTLS_DEBUG != LWIP_DBG_OFF)
    const int altcpMbedtlsDebug = 1;
#else
    const int altcpMbedtlsDebug = 0;
#endif
#if defined(ALTCP_MBEDTLS_LIB_DEBUG) && (ALTCP_MBEDTLS_LIB_DEBUG != LWIP_DBG_OFF)
    const int altcpMbedtlsLibDebug = 1;
#else
    const int altcpMbedtlsLibDebug = 0;
#endif
#if defined(MBEDTLS_DEBUG_C)
    const int mbedtlsDebugC = 1;
#else
    const int mbedtlsDebugC = 0;
#endif
#if defined(ALTCP_MBEDTLS_LIB_DEBUG_LEVEL_MIN)
    const int altcpMbedtlsLibDebugLevelMin = ALTCP_MBEDTLS_LIB_DEBUG_LEVEL_MIN;
#else
    const int altcpMbedtlsLibDebugLevelMin = -1;
#endif

    MATTER_HTTPS_PRINTF(
        "TLS debug: LWIP_DEBUG=%d LWIP_ALTCP_TLS_MBEDTLS=%d ALTCP_MBEDTLS_DEBUG=%d ALTCP_MBEDTLS_LIB_DEBUG=%d "
        "ALTCP_MBEDTLS_LIB_DEBUG_LEVEL_MIN=%d MBEDTLS_DEBUG_C=%d (altcp sets mbedtls_debug threshold=4 when MBEDTLS_DEBUG_C)",
        lwipDebug, altcpTlsMbedtls, altcpMbedtlsDebug, altcpMbedtlsLibDebug, altcpMbedtlsLibDebugLevelMin, mbedtlsDebugC);
    MATTER_HTTPS_PRINTF(
        "TLS logs: [MATTER_HTTPS][tls]=printf handshake errors; [lwip][ALTCP_TLS]/[MBEDTLS]=LwIPLog on tcpip thread");
}

const char * MatterHttpsErrToString(matterHttps_err_t err)
{
    switch (err)
    {
    case MATTER_HTTPS_OK:
        return "OK";
    case MATTER_HTTPS_ERR_INVAL:
        return "INVAL";
    case MATTER_HTTPS_ERR_MEM:
        return "MEM";
    case MATTER_HTTPS_ERR_FAIL:
        return "FAIL";
    case MATTER_HTTPS_ERR_BUSY:
        return "BUSY";
    default:
        return "UNKNOWN";
    }
}

const char * HttpcResultToString(httpc_result_t result)
{
    switch (result)
    {
    case HTTPC_RESULT_OK:
        return "OK";
    case HTTPC_RESULT_ERR_UNKNOWN:
        return "ERR_UNKNOWN";
    case HTTPC_RESULT_ERR_CONNECT:
        return "ERR_CONNECT";
    case HTTPC_RESULT_ERR_HOSTNAME:
        return "ERR_HOSTNAME";
    case HTTPC_RESULT_ERR_CLOSED:
        return "ERR_CLOSED";
    case HTTPC_RESULT_ERR_TIMEOUT:
        return "ERR_TIMEOUT";
    case HTTPC_RESULT_ERR_SVR_RESP:
        return "ERR_SVR_RESP";
    case HTTPC_RESULT_ERR_MEM:
        return "ERR_MEM";
    case HTTPC_RESULT_LOCAL_ABORT:
        return "LOCAL_ABORT";
    case HTTPC_RESULT_ERR_CONTENT_LEN:
        return "ERR_CONTENT_LEN";
    default:
        return "UNKNOWN";
    }
}

void LogMbedtlsBuildFlags()
{
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    const int x509CrtParse = 1;
#else
    const int x509CrtParse = 0;
#endif
#if defined(MBEDTLS_ASN1_PARSE_C)
    const int asn1Parse = 1;
#else
    const int asn1Parse = 0;
#endif
#if defined(MBEDTLS_PEM_PARSE_C)
    const int pemParse = 1;
#else
    const int pemParse = 0;
#endif
#if defined(MBEDTLS_ECDSA_C)
    const int ecdsa = 1;
#else
    const int ecdsa = 0;
#endif
#if defined(MBEDTLS_ECP_C)
    const int ecp = 1;
#else
    const int ecp = 0;
#endif
#if defined(MBEDTLS_RSA_C)
    const int rsa = 1;
#else
    const int rsa = 0;
#endif
#if defined(MBEDTLS_PK_PARSE_C)
    const int pkParse = 1;
#else
    const int pkParse = 0;
#endif
#if defined(MBEDTLS_PSA_CRYPTO_C)
    const int psaCryptoC = 1;
#else
    const int psaCryptoC = 0;
#endif
#if defined(MBEDTLS_USE_PSA_CRYPTO)
    const int usePsaCrypto = 1;
#else
    const int usePsaCrypto = 0;
#endif

    MATTER_HTTPS_PRINTF("mbedtls build: X509_CRT_PARSE=%d ASN1_PARSE=%d PEM=%d ECDSA=%d ECP=%d RSA=%d PK_PARSE=%d", x509CrtParse,
                        asn1Parse, pemParse, ecdsa, ecp, rsa, pkParse);
    MATTER_HTTPS_PRINTF("mbedtls PSA: MBEDTLS_PSA_CRYPTO_C=%d MBEDTLS_USE_PSA_CRYPTO=%d (expect 1,0 for HTTPS TLS)", psaCryptoC,
                        usePsaCrypto);
}

void LogCertSummary(const unsigned char * cert, size_t certLen)
{
    if (cert == nullptr || certLen == 0)
    {
        MATTER_HTTPS_PRINTF("CA PEM: none (heap_free=%u)", static_cast<unsigned>(xPortGetFreeHeapSize()));
        return;
    }

    size_t pemLen = certLen;
    if (cert[pemLen - 1] == '\0')
    {
        pemLen--;
    }

    const bool hasBegin = (strstr(reinterpret_cast<const char *>(cert), "-----BEGIN CERTIFICATE-----") != nullptr);
    const bool hasEnd   = (strstr(reinterpret_cast<const char *>(cert), "-----END CERTIFICATE-----") != nullptr);

    MATTER_HTTPS_PRINTF("CA PEM: total_len=%u pem_len=%u has_begin=%d has_end=%d heap_free=%u", static_cast<unsigned>(certLen),
                        static_cast<unsigned>(pemLen), hasBegin ? 1 : 0, hasEnd ? 1 : 0,
                        static_cast<unsigned>(xPortGetFreeHeapSize()));
}

int ProbeMbedtlsX509CrtParse(const unsigned char * cert, size_t certLen, const char * label)
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);

    const int ret = mbedtls_x509_crt_parse(&crt, cert, certLen);
    if (ret != 0)
    {
        char errbuf[96];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        MATTER_HTTPS_ERR_PRINTF("%s failed: ret=%d (-0x%04X) %s", label, ret, -ret, errbuf);
    }
    else
    {
        MATTER_HTTPS_PRINTF("%s OK: version=%d", label, crt.version);
    }

    mbedtls_x509_crt_free(&crt);
    return ret;
}

void LogTlsInitFailureDiagnostics(const unsigned char * cert, size_t certLen)
{
    LogMbedtlsBuildFlags();
    LogCertSummary(cert, certLen);

    if (cert == nullptr || certLen == 0)
    {
        return;
    }

    (void) ProbeMbedtlsX509CrtParse(cert, certLen, "mbedtls_x509_crt_parse(total_len)");
    if (cert[certLen - 1] == '\0')
    {
        (void) ProbeMbedtlsX509CrtParse(cert, certLen - 1, "mbedtls_x509_crt_parse(without_null)");
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

// void LogMbedtlsSslBufferConfig()
// {
// #if defined(MBEDTLS_SSL_IN_CONTENT_LEN) && defined(MBEDTLS_SSL_OUT_CONTENT_LEN) && defined(MBEDTLS_SSL_MAX_CONTENT_LEN)
//     ChipLogProgress(AppServer, "[MATTER_HTTPS] mbedtls SSL_IN_CONTENT_LEN=%d SSL_OUT_CONTENT_LEN=%d SSL_MAX_CONTENT_LEN=%d",
//                     MBEDTLS_SSL_IN_CONTENT_LEN, MBEDTLS_SSL_OUT_CONTENT_LEN, MBEDTLS_SSL_MAX_CONTENT_LEN);
// #else
//     ChipLogProgress(AppServer, "[MATTER_HTTPS] mbedtls SSL buffer size macros not all defined in this build");
// #endif
// }

void LogLwIPDispatchDiagnostics(const char * phase)
{
    ChipLogProgress(AppServer, "[MATTER_HTTPS] lwip diag phase=%s", phase);
    ChipLogProgress(AppServer, "[MATTER_HTTPS] lwip diag MEM_SIZE=%u", static_cast<unsigned>(MEM_SIZE));
    ChipLogProgress(AppServer, "[MATTER_HTTPS] lwip diag tls_config=%p", static_cast<void *>(gHttpc.tlsConfig));
    ChipLogProgress(AppServer, "[MATTER_HTTPS] lwip diag request_in_flight=%d", gHttpc.requestInFlight);
    ChipLogProgress(AppServer, "[MATTER_HTTPS] lwip diag heap_free=%u", static_cast<unsigned>(xPortGetFreeHeapSize()));
}

matterHttps_err_t LwIPErrToMatterHttps(err_t err)
{
    switch (err)
    {
    case ERR_OK:
        return MATTER_HTTPS_OK;
    case ERR_ARG:
        return MATTER_HTTPS_ERR_INVAL;
    case ERR_MEM:
        return MATTER_HTTPS_ERR_MEM;
    case ERR_INPROGRESS:
        return MATTER_HTTPS_ERR_BUSY;
    default:
        return MATTER_HTTPS_ERR_FAIL;
    }
}

void MatterHttpsFlushLogsAfterTlsError()
{
#if MATTER_HTTPS_TLS_ERROR_LOG_FLUSH_MS > 0
    vTaskDelay(pdMS_TO_TICKS(MATTER_HTTPS_TLS_ERROR_LOG_FLUSH_MS));
#endif
    uartFlushTxQueue();
}

err_t HeadersDone(httpc_state_t * connection, void * arg, struct pbuf * hdr, u16_t hdr_len, u32_t content_len)
{
    (void) connection;
    (void) arg;
    (void) hdr;
    ChipLogProgress(AppServer, "[MATTER_HTTPS] headers received: hdr_len=%u content_len=%lu", hdr_len,
                    static_cast<unsigned long>(content_len));
    return ERR_OK;
}

void Result(void * arg, httpc_result_t result, u32_t rx_content_len, u32_t srv_res, err_t err)
{
    (void) arg;

    gLastRequestFailed = (result != HTTPC_RESULT_OK || err != ERR_OK);

    if (gLastRequestFailed)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] request failed: result=%s (%d) http_status=%lu rx=%lu lwip_err=%d",
                     HttpcResultToString(result), static_cast<int>(result), static_cast<unsigned long>(srv_res),
                     static_cast<unsigned long>(rx_content_len), static_cast<int>(err));
    }
    else
    {
        ChipLogProgress(AppServer, "[MATTER_HTTPS] request completed: http_status=%lu rx=%lu", static_cast<unsigned long>(srv_res),
                        static_cast<unsigned long>(rx_content_len));
    }

    gHttpc.requestInFlight = false;
    gHttpc.state           = nullptr;

    matter_https_altcp_defer_reset();
    gRequestDone = true;
}

err_t Receive(void * arg, struct altcp_pcb * conn, struct pbuf * p, err_t err)
{
    (void) arg;
    (void) conn;

    if (err != ERR_OK)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] receive callback error: lwip_err=%d", static_cast<int>(err));
    }

    if (p != nullptr)
    {
        ChipLogProgress(AppServer, "[MATTER_HTTPS] received %u bytes", p->tot_len);
        altcp_recved(conn, p->tot_len);
        pbuf_free(p);
    }

    return err;
}

struct InitTlsJob
{
    const unsigned char * cert;
    size_t certLen;
    matterHttps_err_t result;
};

void InitTlsOnTcpipThread(void * context)
{
    auto * job = static_cast<InitTlsJob *>(context);

    if (gHttpc.initialized)
    {
        job->result = MATTER_HTTPS_OK;
        return;
    }

    gHttpc.tlsConfig = altcp_tls_create_config_client(job->cert, job->certLen);
    if (gHttpc.tlsConfig == nullptr)
    {
        job->result = MATTER_HTTPS_ERR_MEM;
        return;
    }

    gHttpc.allocator.alloc = altcp_tls_alloc;
    gHttpc.allocator.arg   = gHttpc.tlsConfig;
    gHttpc.initialized     = true;
    job->result            = MATTER_HTTPS_OK;
}

struct RequestJob
{
    matterHttps_request_t request;
    err_t result = ERR_OK;
};

void RequestOnTcpipThread(void * context)
{
    auto * job = static_cast<RequestJob *>(context);

    if (!gHttpc.initialized)
    {
        job->result = ERR_VAL;
        return;
    }

    if (gHttpc.requestInFlight)
    {
        job->result = ERR_INPROGRESS;
        return;
    }

    httpc_connection_t settings = {};
    settings.altcp_allocator    = &gHttpc.allocator;
    settings.headers_done_fn    = HeadersDone;
    settings.result_fn          = Result;

    const uint16_t port = job->request.port == 0 ? MATTER_HTTPS_DEFAULT_PORT : job->request.port;
#if MATTER_HTTPS_USE_DNS
    job->result = httpc_get_file_dns(job->request.host, port, job->request.uri, &settings, Receive, nullptr, &gHttpc.state);
#else
    ip_addr_t serverAddr;
    if (job->request.host == nullptr || !ipaddr_aton(job->request.host, &serverAddr))
    {
        job->result = ERR_ARG;
        return;
    }
    job->result = httpc_get_file(&serverAddr, port, job->request.uri, &settings, Receive, nullptr, &gHttpc.state);
#endif

    if (job->result == ERR_OK)
    {
        gHttpc.requestInFlight = true;
    }
}

matterHttps_err_t DispatchRequestOnTcpipThread(const matterHttps_request_t * request)
{
    RequestJob job          = { *request, ERR_OK };
    const uint16_t port     = job.request.port == 0 ? MATTER_HTTPS_DEFAULT_PORT : job.request.port;
    const size_t heapBefore = xPortGetFreeHeapSize();

    LogLwIPDispatchDiagnostics("pre-dispatch");
#if MATTER_HTTPS_USE_DNS
    ChipLogProgress(AppServer, "[MATTER_HTTPS] starting GET https://%s:%u%s (dns=1 heap_free=%u)", job.request.host, port,
                    job.request.uri, static_cast<unsigned>(heapBefore));
#else
    if (job.request.host == nullptr)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] invalid server IP: (null)");
        return MATTER_HTTPS_ERR_INVAL;
    }
    ChipLogProgress(AppServer, "[MATTER_HTTPS] starting GET https://%s:%u%s (dns=0 heap_free=%u)", job.request.host, port,
                    job.request.uri, static_cast<unsigned>(heapBefore));
#endif

    err_t schedule         = RunOnTcpipThreadSync(RequestOnTcpipThread, &job);
    const size_t heapAfter = xPortGetFreeHeapSize();
    if (schedule != ERR_OK)
    {
        return LwIPErrToMatterHttps(schedule);
    }

    if (job.result != ERR_OK)
    {
        LogLwIPDispatchDiagnostics("post-fail");
        ChipLogError(AppServer,
                     "[MATTER_HTTPS] HTTP GET dispatch failed: server=%s port=%u uri=%s lwip_err=%d (%s) heap_before=%u "
                     "heap_after=%u delta=%d",
                     job.request.host, port, job.request.uri, static_cast<int>(job.result), LwIPErrToString(job.result),
                     static_cast<unsigned>(heapBefore), static_cast<unsigned>(heapAfter),
                     static_cast<int>(heapAfter) - static_cast<int>(heapBefore));
        ChipLogError(AppServer,
                     "[MATTER_HTTPS] see [HTTPC] / [ALTCP_TLS] logs for mem_malloc, pbuf_alloc, altcp_new, "
                     "altcp_tcp_new, altcp_alloc, mbedtls_ssl_setup");
    }

    return LwIPErrToMatterHttps(job.result);
}

matterHttps_err_t InitTlsFromTask()
{
    InitTlsJob job = { reinterpret_cast<const unsigned char *>(gRootCaPem), gRootCaPemLen, MATTER_HTTPS_ERR_FAIL };
#if MATTER_HTTPS_SKIP_CA_VERIFY
    job.cert    = nullptr;
    job.certLen = 0;
    ChipLogProgress(AppServer, "[MATTER_HTTPS] CA verify disabled, no root CA loaded");
#else
    if (job.cert == nullptr || job.certLen == 0)
    {
        job.cert    = reinterpret_cast<const unsigned char *>(MATTER_HTTPS_DEFAULT_ROOT_CA);
        job.certLen = MATTER_HTTPS_DEFAULT_ROOT_CA_LEN;
        ChipLogProgress(AppServer, "[MATTER_HTTPS] using built-in root CA (len=%u)", static_cast<unsigned>(job.certLen));
    }
    else
    {
        ChipLogProgress(AppServer, "[MATTER_HTTPS] using provided root CA (len=%u)", static_cast<unsigned>(job.certLen));
    }
#endif

    constexpr int kMaxTlsInitAttempts = 3;
    for (int attempt = 1; attempt <= kMaxTlsInitAttempts; ++attempt)
    {
        const size_t heapBefore = xPortGetFreeHeapSize();
        job.result              = MATTER_HTTPS_ERR_FAIL;
        err_t schedule          = RunOnTcpipThreadSync(InitTlsOnTcpipThread, &job);
        const size_t heapAfter  = xPortGetFreeHeapSize();
        if (schedule != ERR_OK)
        {
            return LwIPErrToMatterHttps(schedule);
        }
        if (job.result == MATTER_HTTPS_OK)
        {
            ChipLogProgress(AppServer, "[MATTER_HTTPS] TLS config OK (cert_len=%u heap_before=%u heap_after=%u)",
                            static_cast<unsigned>(job.certLen), static_cast<unsigned>(heapBefore),
                            static_cast<unsigned>(heapAfter));
            return MATTER_HTTPS_OK;
        }

        MATTER_HTTPS_ERR_PRINTF("altcp_tls_create_config_client failed (cert_len=%u heap_before=%u heap_after=%u)",
                                static_cast<unsigned>(job.certLen), static_cast<unsigned>(heapBefore),
                                static_cast<unsigned>(heapAfter));
        LogTlsInitFailureDiagnostics(job.cert, job.certLen);
        ChipLogError(AppServer, "[MATTER_HTTPS] TLS init attempt %d/%d failed: %s (heap_free=%u)", attempt, kMaxTlsInitAttempts,
                     MatterHttpsErrToString(job.result), static_cast<unsigned>(xPortGetFreeHeapSize()));
        MatterHttpsFlushLogsAfterTlsError();
        if (attempt < kMaxTlsInitAttempts)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    return job.result;
}

void RunPendingRequest()
{
    matterHttps_request_t request = {};
    if (gPendingRequestValid)
    {
        request              = gPendingRequest;
        gPendingRequestValid = false;
    }
    else
    {
        request.host = MATTER_HTTPS_DEFAULT_SERVER;
        request.uri  = MATTER_HTTPS_DEFAULT_URI;
        request.port = MATTER_HTTPS_DEFAULT_PORT;
    }

    matter_https_tls_clear_last_error();
    gLastRequestFailed = false;
    gRequestDone       = false;
    matter_https_altcp_defer_reset();

    matterHttps_err_t err = DispatchRequestOnTcpipThread(&request);
    if (err != MATTER_HTTPS_OK)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] request dispatch failed: %s (heap_free=%u)", MatterHttpsErrToString(err),
                     static_cast<unsigned>(xPortGetFreeHeapSize()));
        matter_https_tls_print_last_error();
        MatterHttpsFlushLogsAfterTlsError();
        return;
    }

    const TickType_t waitDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
    while (!gRequestDone && xTaskGetTickCount() < waitDeadline)
    {
        TickType_t now     = xTaskGetTickCount();
        TickType_t timeout = (waitDeadline > now) ? (waitDeadline - now) : 0;

        EventBits_t bits =
            xEventGroupWaitBits(matterHttpsEvents, kSignalHttpsMbedtlsRx, pdTRUE, pdFALSE, timeout);

        if (bits & kSignalHttpsMbedtlsRx)
        {
            ChipLogProgress(AppServer, "[MATTER_HTTPS] defer wake heap_free=%u",
                            static_cast<unsigned>(xPortGetFreeHeapSize()));
            matter_https_altcp_process_pending();
            continue;
        }

        if (timeout == 0)
        {
            break;
        }
    }

    if (!gRequestDone)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] request timed out waiting for completion (heap_free=%u)",
                     static_cast<unsigned>(xPortGetFreeHeapSize()));
    }

    matter_https_tls_print_last_error();

    if (gLastRequestFailed || matter_https_tls_get_last_handshake_error() != 0)
    {
        MatterHttpsFlushLogsAfterTlsError();
    }
}

static void MatterHttpsTaskFn(void * args)
{
    (void) args;

    ChipLogProgress(AppServer, "[MATTER_HTTPS] task started, delay %u s", MATTER_HTTPS_INIT_DELAY_SEC);
    LogTlsDebugConfig();
    LogMbedtlsBuildFlags();
    vTaskDelay(pdMS_TO_TICKS(MATTER_HTTPS_INIT_DELAY_SEC * 1000));

    matterHttps_err_t initErr = InitTlsFromTask();
    if (initErr != MATTER_HTTPS_OK)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] MatterHttpsInit failed: %s", MatterHttpsErrToString(initErr));
        goto exit;
    }

    ChipLogProgress(AppServer, "[MATTER_HTTPS] TLS client initialized (stack_hwm=%u)",
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(matterHttpsTask)));
    RunPendingRequest();

    while (true)
    {
        EventBits_t event = xEventGroupWaitBits(matterHttpsEvents, kSignalHttpsDoRequest, pdTRUE, pdFALSE, portMAX_DELAY);
        if (event & kSignalHttpsDoRequest)
        {
            RunPendingRequest();
        }
    }

exit:
    if (matterHttpsEvents != nullptr)
    {
        vEventGroupDelete(matterHttpsEvents);
        matterHttpsEvents        = nullptr;
        gMatterHttpsNotifyEvents = nullptr;
    }
    matterHttpsTask = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

extern "C" {

matterHttps_err_t MatterHttpsInit(const char * rootCaPem, size_t rootCaPemLen)
{
    VerifyOrReturnError(matterHttpsTask == nullptr, MATTER_HTTPS_OK);

    gRootCaPem    = rootCaPem;
    gRootCaPemLen = rootCaPemLen;

    matterHttpsEvents = xEventGroupCreate();
    if (!matterHttpsEvents)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] failed to create event group");
        return MATTER_HTTPS_ERR_FAIL;
    }
    gMatterHttpsNotifyEvents = matterHttpsEvents;

    matter_https_altcp_defer_init();
    matter_https_altcp_defer_set_notify(MatterHttpsNotifyMbedtlsRx);

    if ((pdPASS != xTaskCreate(MatterHttpsTaskFn, MATTER_HTTPS_TASK_NAME, MATTER_HTTPS_TASK_STACK_SIZE, nullptr,
                               MATTER_HTTPS_TASK_PRIORITY, &matterHttpsTask)) ||
        !matterHttpsTask)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] failed to create task");
        vEventGroupDelete(matterHttpsEvents);
        matterHttpsEvents        = nullptr;
        gMatterHttpsNotifyEvents = nullptr;
        return MATTER_HTTPS_ERR_MEM;
    }

    return MATTER_HTTPS_OK;
}

matterHttps_err_t MatterHttpsRequestGet(const matterHttps_request_t * request)
{
    if (request == nullptr || request->host == nullptr || request->uri == nullptr)
    {
        return MATTER_HTTPS_ERR_INVAL;
    }

    if (matterHttpsTask == nullptr || matterHttpsEvents == nullptr)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] request rejected: task not running");
        return MATTER_HTTPS_ERR_FAIL;
    }

    if (gHttpc.requestInFlight)
    {
        return MATTER_HTTPS_ERR_BUSY;
    }

    gPendingRequest      = *request;
    gPendingRequestValid = true;
    xEventGroupSetBits(matterHttpsEvents, kSignalHttpsDoRequest);

    return MATTER_HTTPS_OK;
}

} // extern "C"
