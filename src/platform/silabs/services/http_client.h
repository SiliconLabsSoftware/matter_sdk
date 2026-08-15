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

#include "matter_service.h"

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif
#include "sl_http_client.h"
#include "sl_status.h"
#ifdef __cplusplus
}
#endif

namespace chip {
namespace DeviceLayer {
namespace Silabs {

/**
 * @brief Configuration for @ref HttpClient::Init.
 *
 * TLS CA material is optional. When @p tlsCaCert is non-null and @p tlsCaCertLen is non-zero,
 * Init loads the PEM into the NWP credential store (once per process).
 */
struct HttpClientConfig
{
    uint8_t certificateIndex = 1;     ///< NWP TLS / HTTPS certificate index.
    bool httpsEnable         = true;  ///< true = HTTPS, false = plain HTTP.
    const char * username    = nullptr; ///< Optional HTTP basic-auth username; nullptr skips auth.
    const char * password    = nullptr; ///< Optional HTTP basic-auth password; nullptr skips auth.

    /** PEM bytes for sl_net_set_credential(SL_NET_SIGNING_CERTIFICATE). nullptr = skip CA load. */
    const uint8_t * tlsCaCert = nullptr;
    size_t tlsCaCertLen       = 0; ///< Byte length of @p tlsCaCert (exclude trailing NUL for string PEMs).
};

/**
 * @brief Per-request destination for Get / Post / Put.
 *
 * The SiWx917 NWP HTTP client needs a connect address (@p serverIp) and a Host / SNI
 * name (@p hostName) separately; they may be the same string.
 */
struct HttpHost
{
    const char * hostName = nullptr; ///< Host header / SNI name. Must not be nullptr for requests.
    const char * serverIp = nullptr; ///< IP (or resolvable address) used to connect. Must not be nullptr.
    uint16_t port         = 0;       ///< Destination port (e.g. 443 or 8443).
};

/**
 * @brief SiWx917 NWP offload HTTPS client usable from Matter C++ code.
 *
 * Lifecycle: Start → Init → Get/Post/Put → WaitForResponse
 * (+ ContinuePut for PUT) → Deinit → Stop.
 * One instance may target multiple hosts by passing a different @ref HttpHost on each verb.
 * Operations are queued to the service thread; the caller waits via @ref WaitForResponse.
 */
class HttpClient : public MatterService
{
public:
    /**
     * @brief Queue NWP HTTP client initialization on the service thread.
     *
     * @param[in] config Client configuration including optional CA PEM and credentials.
     *
     * @retval CHIP_NO_ERROR              Operation queued.
     * @retval CHIP_ERROR_BUSY            Another operation is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Service is not running or client is initialized.
     */
    CHIP_ERROR Init(const HttpClientConfig & config);

    /**
     * @brief Queue NWP HTTP client deinitialization on the service thread.
     *
     * @retval CHIP_NO_ERROR              Operation queued or client already deinitialized.
     * @retval CHIP_ERROR_BUSY            Another operation is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Service is not running.
     */
    CHIP_ERROR Deinit();

    /**
     * @brief Spawn the CMSIS-OS2 service thread and begin waiting for operations.
     *
     * Idempotent if the thread is already running.
     *
     * @retval CHIP_NO_ERROR              Success or already running.
     * @retval CHIP_ERROR_INTERNAL Event or thread creation failed.
     */
    CHIP_ERROR Start() override;

    /**
     * @brief Terminate the service thread if it is running.
     *
     * Safe if already stopped. Returns busy while an operation is outstanding.
     *
     * @retval CHIP_NO_ERROR   Service stopped or already stopped.
     * @retval CHIP_ERROR_BUSY An operation is in progress.
     */
    CHIP_ERROR Stop() override;

    /**
     * @brief Whether the service thread is currently running.
     */
    bool IsRunning() const override;

    /**
     * @brief True while an HTTP exchange is outstanding (waiting on NWP and/or PUT chunks remain).
     */
    bool IsRequestInProgress() const;

    /**
     * @brief True while waiting for an NWP HTTP callback.
     */
    bool IsBusy() const;

    /**
     * @brief True while a PUT body upload is active (may still need @ref ContinuePut).
     */
    bool IsPutInProgress() const;

    /**
     * @brief Block until the queued operation completes.
     *
     * Caller (e.g. demo) owns waiting. No-op with CHIP_NO_ERROR when nothing is pending.
     *
     * @retval CHIP_NO_ERROR       Response OK (or idle).
     * @retval CHIP_ERROR_INTERNAL Response / NWP failure.
     */
    CHIP_ERROR WaitForResponse();

    /**
     * @brief Queue the next PUT body chunk after a successful @ref WaitForResponse.
     *
     * May set busy again when the NWP returns SL_STATUS_IN_PROGRESS; caller must WaitForResponse.
     * When the server signals end-of-data, clears the PUT-in-progress state.
     *
     * @retval CHIP_NO_ERROR              Chunk accepted, PUT finished, or nothing left to write.
     * @retval CHIP_ERROR_INCORRECT_STATE No PUT in progress, or still busy waiting.
     * @retval CHIP_ERROR_INTERNAL        Write failure.
     */
    CHIP_ERROR ContinuePut();

    /**
     * @brief Start an HTTPS GET (non-blocking).
     *
     * Rejects if @ref IsRequestInProgress. Response body is written into @p responseBuffer
     * from the NWP callback; buffer must stay valid until @ref WaitForResponse returns.
     *
     * @retval CHIP_NO_ERROR              Request started (caller must WaitForResponse if IsBusy).
     * @retval CHIP_ERROR_BUSY            Another request is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Client not initialized.
     * @retval CHIP_ERROR_INVALID_ARGUMENT Null host or resource.
     * @retval CHIP_ERROR_INTERNAL        Send failure.
     */
    CHIP_ERROR Get(const HttpHost & host, const char * resource, MutableByteSpan & responseBuffer);

    /**
     * @brief Start an HTTPS POST (non-blocking).
     *
     * @p body must remain valid until @ref WaitForResponse returns.
     *
     * @retval CHIP_NO_ERROR              Request started (caller must WaitForResponse if IsBusy).
     * @retval CHIP_ERROR_BUSY            Another request is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Client not initialized.
     * @retval CHIP_ERROR_INVALID_ARGUMENT Null host or resource.
     * @retval CHIP_ERROR_INTERNAL        Send failure.
     */
    CHIP_ERROR Post(const HttpHost & host, const char * resource, ByteSpan body);

    /**
     * @brief Start an HTTPS PUT (non-blocking); body is uploaded via @ref ContinuePut.
     *
     * @p body must remain valid until PUT completes (@ref IsPutInProgress becomes false).
     *
     * @retval CHIP_NO_ERROR              Request started (caller WaitForResponse + ContinuePut).
     * @retval CHIP_ERROR_BUSY            Another request is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Client not initialized.
     * @retval CHIP_ERROR_INVALID_ARGUMENT Null host or resource.
     * @retval CHIP_ERROR_INTERNAL        Send failure.
     */
    CHIP_ERROR Put(const HttpHost & host, const char * resource, ByteSpan body);

private:
    static constexpr size_t kAppBufferLength        = 2000;
    static constexpr size_t kDefaultThreadStackSize = 4 * 1024;
    static constexpr uint8_t kHttpSuccessResponse   = 1;
    static constexpr uint8_t kHttpFailureResponse   = 2;
    static constexpr uint16_t kHttpClientErrorMin   = 400;
    static constexpr uint16_t kHttpServerErrorMax   = 599;

    enum class Operation : uint8_t
    {
        None,
        Init,
        Deinit,
        Get,
        Post,
        Put,
        ContinuePut,
    };

    static constexpr uint32_t kEventOperation = 1u << 0;
    static constexpr uint32_t kEventResponse  = 1u << 1;
    static constexpr uint32_t kEventStop      = 1u << 2;
    static constexpr uint32_t kEventComplete  = 1u << 3;

    static void ServiceThread(void * arg);
    static CHIP_ERROR MapStatus(sl_status_t status);

    /**
     * @brief Resolve the instance a response callback belongs to.
     *
     * The NWP delivers the final PUT server-response callback without the request context,
     * so fall back to the client that owns the in-flight request.
     */
    static HttpClient * ClientFromContext(void * request_context);

    static sl_status_t GetResponseCallback(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                           void * request_context);
    static sl_status_t PostResponseCallback(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                            void * request_context);
    static sl_status_t PutResponseCallback(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                           void * request_context);

    void ResetRequestState();
    CHIP_ERROR QueueOperation(Operation operation);
    void ProcessOperation();
    CHIP_ERROR ProcessInit();
    CHIP_ERROR ProcessDeinit();
    CHIP_ERROR ProcessGet();
    CHIP_ERROR ProcessPost();
    CHIP_ERROR ProcessPut();
    CHIP_ERROR ProcessContinuePut();
    void CompleteOperation(CHIP_ERROR error);
    void SignalResponse();

    sl_http_client_t mClientHandle = 0;

    HttpClientConfig mConfig{};
    void * mThreadId   = nullptr; // osThreadId_t
    void * mEventFlags = nullptr; // osEventFlagsId_t

    sl_http_client_credentials_t * mCredentials = nullptr;

    uint8_t mAppBuffer[kAppBufferLength] = { 0 };
    uint32_t mAppBuffIndex               = 0;

    volatile uint8_t mHttpRspReceived = 0;
    volatile uint8_t mEndOfFile       = 0;
    sl_status_t mCallbackStatus       = SL_STATUS_OK;
    int32_t mHttpOffset               = 0;
    int32_t mHttpChunkLength          = 0;

    MutableByteSpan * mActiveResponseBuffer = nullptr;
    ByteSpan mPutBody;
    HttpHost mPendingHost{};
    const char * mPendingResource = nullptr;
    ByteSpan mPendingBody;

    volatile bool mBusy              = false;
    volatile bool mCompletionPending = false;
    bool mPutActive                   = false;
    Operation mPendingOperation  = Operation::None;
    CHIP_ERROR mOperationResult  = CHIP_NO_ERROR;

    bool mInitialized = false;
    static bool sNwpCaLoaded;

    /** Client owning the request registered with the NWP; used when a callback carries no context. */
    static HttpClient * sActiveClient;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
