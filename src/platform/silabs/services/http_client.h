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
    uint8_t certificateIndex = SL_HTTPS_CLIENT_DEFAULT_CERTIFICATE_INDEX; ///< NWP TLS / HTTPS certificate index.
    bool httpsEnable         = true;                                      ///< true = HTTPS, false = plain HTTP.
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
 * @brief Completion callback for a queued HttpClient operation.
 *
 * Invoked on the HttpClient service thread when the operation finishes (success or failure).
 *
 * @param[in] result  CHIP_NO_ERROR on success; otherwise the operation error.
 * @param[in] context User pointer passed to the operation that started this work.
 */
using HttpOperationCallback = void (*)(CHIP_ERROR result, void * context);

/**
 * @brief SiWx917 NWP offload HTTPS client usable from Matter C++ code.
 *
 * Lifecycle: Start → Init → Get/Post/Put → Deinit → Stop.
 * One instance may target multiple hosts by passing a different @ref HttpHost on each verb.
 * Operations are queued to the service thread and report completion via @ref HttpOperationCallback.
 */
class HttpClient : public MatterService
{
public:
    /**
     * @brief Queue NWP HTTP client initialization on the service thread.
     *
     * @param[in] config  Client configuration including optional CA PEM and credentials.
     * @param[in] callback Completion callback; may be nullptr.
     * @param[in] context  Passed to @p callback.
     *
     * @retval CHIP_NO_ERROR              Operation queued.
     * @retval CHIP_ERROR_BUSY            Another operation is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Service is not running or client is initialized.
     */
    CHIP_ERROR Init(const HttpClientConfig & config, HttpOperationCallback callback, void * context = nullptr);

    /**
     * @brief Queue NWP HTTP client deinitialization on the service thread.
     *
     * @param[in] callback Completion callback; may be nullptr.
     * @param[in] context  Passed to @p callback.
     *
     * @retval CHIP_NO_ERROR              Operation queued or client already deinitialized.
     * @retval CHIP_ERROR_BUSY            Another operation is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Service is not running.
     */
    CHIP_ERROR Deinit(HttpOperationCallback callback, void * context = nullptr);

    /**
     * @brief Spawn the CMSIS-OS2 service thread and begin waiting for operations.
     *
     * Idempotent if the thread is already running.
     *
     * @retval CHIP_NO_ERROR       Success or already running.
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
     * @brief True while a queued operation has not yet completed.
     */
    bool IsBusy() const;

    /**
     * @brief Start an HTTPS GET.
     *
     * Response body is written into @p responseBuffer from the NWP callback; the buffer must
     * remain valid until @p callback is invoked.
     *
     * @param[in]     host            Target host / IP / port.
     * @param[in]     resource        Request path (e.g. "/index.html"). Must not be nullptr.
     * @param[in,out] responseBuffer  Buffer for response body; size reduced to bytes received.
     * @param[in]     callback        Completion callback; may be nullptr.
     * @param[in]     context         Passed to @p callback.
     *
     * @retval CHIP_NO_ERROR              Request queued.
     * @retval CHIP_ERROR_BUSY            Another request is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Client not initialized.
     * @retval CHIP_ERROR_INVALID_ARGUMENT Null host or resource.
     */
    CHIP_ERROR Get(const HttpHost & host, const char * resource, MutableByteSpan & responseBuffer, HttpOperationCallback callback,
                   void * context = nullptr);

    /**
     * @brief Start an HTTPS POST.
     *
     * @p body must remain valid until @p callback is invoked. Bodies larger than
     * @c SL_HTTP_CLIENT_MAX_WRITE_BUFFER_LENGTH are uploaded in 900-byte chunks.
     *
     * @param[in] host     Target host / IP / port.
     * @param[in] resource Request path. Must not be nullptr.
     * @param[in] body     Request body bytes (may be empty).
     * @param[in] callback Completion callback; may be nullptr.
     * @param[in] context  Passed to @p callback.
     *
     * @retval CHIP_NO_ERROR              Request queued.
     * @retval CHIP_ERROR_BUSY            Another request is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Client not initialized.
     * @retval CHIP_ERROR_INVALID_ARGUMENT Null host or resource.
     */
    CHIP_ERROR Post(const HttpHost & host, const char * resource, ByteSpan body, HttpOperationCallback callback,
                    void * context = nullptr);

    /**
     * @brief Start an HTTPS PUT with chunked body upload handled on the service thread.
     *
     * @p body must remain valid until @p callback is invoked.
     *
     * @param[in] host     Target host / IP / port.
     * @param[in] resource Request path. Must not be nullptr.
     * @param[in] body     Full request body to upload.
     * @param[in] callback Completion callback; may be nullptr.
     * @param[in] context  Passed to @p callback.
     *
     * @retval CHIP_NO_ERROR              Request queued.
     * @retval CHIP_ERROR_BUSY            Another request is in progress.
     * @retval CHIP_ERROR_INCORRECT_STATE Client not initialized.
     * @retval CHIP_ERROR_INVALID_ARGUMENT Null host or resource.
     */
    CHIP_ERROR Put(const HttpHost & host, const char * resource, ByteSpan body, HttpOperationCallback callback,
                   void * context = nullptr);

private:
    static constexpr size_t kDefaultThreadStackSize = 4 * 1024;
    // HTTP request status codes.
    static constexpr uint8_t kHttpSuccessResponse = 1;
    static constexpr uint8_t kHttpFailureResponse = 2;
    // HTTP client error codes.
    static constexpr uint16_t kHttpClientErrorMin = 400;
    static constexpr uint16_t kHttpServerErrorMax = 599;
    // HTTP server indicators for NWP HTTP client end_of_data
    // GET/POST (indications from server):
    //   0: More response data is expected.
    //   1: This is the final chunk of the response.
    // PUT:
    //   Acknowledgment for transmitted data (notification from NWP):
    //     0: Further data transmission is expected.
    //     1: Data transmission is complete.
    //   Server response (data received from server after transmission):
    //     8: More response data is expected.
    //     9: This is the final chunk of the response. See @ref SL_HTTP_CLIENT_PUT_SERVER_RESPONSE_END_OF_DATA.

    /**
     * @brief HTTP server indicators for NWP HTTP client end_of_data
     */
    enum class EndOfData : uint8_t
    {
        MoreData        = 0,
        EndOfData       = 1,
        MoreDataServer  = 8,
        EndOfDataServer = SL_HTTP_CLIENT_PUT_SERVER_RESPONSE_END_OF_DATA,
    };

    enum class Operation : uint8_t
    {
        None,
        Init,
        Deinit,
        Get,
        Post,
        Put,
        // TODO: Add Delete operation.
        // TODO: Add Head operation.
    };
    // event flags for the service thread
    static constexpr uint32_t kEventOperation = 1u << 0;
    static constexpr uint32_t kEventResponse  = 1u << 1;
    static constexpr uint32_t kEventStop      = 1u << 2;
    /**
     * @brief Service thread function.
     *
     * This function is the entry point for the service thread.
     * It is used to process operations and handle responses.
     *
     * @param[in] arg The argument.
     */
    static void ServiceThread(void * arg);
    /*
     * @brief Map the NWP status to a CHIP_ERROR.
     *
     * @param[in] status The NWP status.
     *
     * @retval CHIP_NO_ERROR The status is OK.
     * @retval CHIP_ERROR_INTERNAL The status is not OK.
     */
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
    CHIP_ERROR QueueOperation(Operation operation, HttpOperationCallback callback, void * context);
    void ProcessOperation();
    void HandleNwpResponse();
    CHIP_ERROR ProcessInit();
    CHIP_ERROR ProcessDeinit();
    CHIP_ERROR ProcessGet();
    CHIP_ERROR ProcessPost();
    CHIP_ERROR ProcessPut();
    CHIP_ERROR ProcessContinueChunkedBody();
    bool NeedsChunkedBody() const;

    /**
     * @brief Complete an operation.
     *
     * This function is called when an operation is completed.
     * It is used to clear the chunked active flag and pending body
     * and to call the callback function.
     *
     * @param[in] error The error code.
     */
    void CompleteOperation(CHIP_ERROR error);
    /**
     * @brief Signal a response.
     *
     * This function is called when a response is received.
     * It is used to signal the response to the service thread.
     *
     * @param[in] response The response.
     */
    void SignalResponse();

    sl_http_client_t mClientHandle = 0;

    HttpClientConfig mConfig{};
    void * mThreadId   = nullptr; // osThreadId_t
    void * mEventFlags = nullptr; // osEventFlagsId_t

    sl_http_client_credentials_t * mCredentials = nullptr;

    uint32_t mResponseBytesWritten = 0;

    volatile uint8_t mHttpRspReceived = 0;
    volatile EndOfData mEndOfData     = EndOfData::EndOfData;
    sl_status_t mCallbackStatus       = SL_STATUS_OK;
    int32_t mHttpOffset               = 0;
    int32_t mHttpChunkLength          = 0;

    MutableByteSpan * mActiveResponseBuffer = nullptr;
    HttpHost mPendingHost{};
    const char * mPendingResource = nullptr;
    ByteSpan mPendingBody;

    volatile bool mBusy         = false;
    bool mChunkedActive         = false;
    Operation mPendingOperation = Operation::None;

    HttpOperationCallback mUserCallback = nullptr;
    void * mUserCallbackContext         = nullptr;

    bool mInitialized = false;
    static bool sNwpCaLoaded;

    /** Client owning the request registered with the NWP; used when a callback carries no context. */
    static HttpClient * sActiveClient;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
