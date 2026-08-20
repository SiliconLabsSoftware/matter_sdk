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

#include "http_client.h"

#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "sl_net.h"
#ifdef __cplusplus
}
#endif

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include <cstdlib>
#include <cstring>

namespace chip {
namespace DeviceLayer {
namespace Silabs {

bool HttpClient::sNwpCaLoaded          = false;
HttpClient * HttpClient::sActiveClient = nullptr;

HttpClient * HttpClient::ClientFromContext(void * request_context)
{
    return (request_context != nullptr) ? static_cast<HttpClient *>(request_context) : sActiveClient;
}

CHIP_ERROR HttpClient::MapStatus(sl_status_t status)
{
    if (status == SL_STATUS_OK)
    {
        return CHIP_NO_ERROR;
    }
    if (status == SL_STATUS_ALLOCATION_FAILED)
    {
        return CHIP_ERROR_NO_MEMORY;
    }
    return CHIP_ERROR_INTERNAL;
}

bool HttpClient::IsRunning() const
{
    return mThreadId != nullptr;
}

bool HttpClient::IsBusy() const
{
    return mBusy;
}

void HttpClient::ServiceThread(void * arg)
{
    auto * self = static_cast<HttpClient *>(arg);
    VerifyOrReturn(self != nullptr);

    auto eventFlags = static_cast<osEventFlagsId_t>(self->mEventFlags);
    while (true)
    {
        uint32_t events =
            osEventFlagsWait(eventFlags, kEventOperation | kEventResponse | kEventStop, osFlagsWaitAny, osWaitForever);
        if ((events & osFlagsError) != 0)
        {
            ChipLogError(DeviceLayer, "HTTPS service event wait failed: 0x%lx", static_cast<unsigned long>(events));
            break;
        }
        if ((events & kEventStop) != 0)
        {
            break;
        }
        if ((events & kEventOperation) != 0)
        {
            self->ProcessOperation();
        }
        if ((events & kEventResponse) != 0)
        {
            self->HandleNwpResponse();
        }
    }
    osEventFlagsDelete(static_cast<osEventFlagsId_t>(self->mEventFlags));
    self->mEventFlags = nullptr;
    self->mThreadId   = nullptr;
    osThreadTerminate(osThreadGetId());
}

CHIP_ERROR HttpClient::Start()
{
    if (mThreadId != nullptr)
    {
        return CHIP_NO_ERROR;
    }

    mEventFlags = osEventFlagsNew(nullptr);
    VerifyOrReturnError(mEventFlags != nullptr, CHIP_ERROR_INTERNAL);

    osThreadAttr_t attrs = {};
    attrs.name           = "http_client";
    attrs.stack_size     = kDefaultThreadStackSize;
    attrs.priority       = osPriorityBelowNormal;

    mThreadId = osThreadNew(ServiceThread, this, &attrs);
    if (mThreadId == nullptr)
    {
        osEventFlagsDelete(static_cast<osEventFlagsId_t>(mEventFlags));
        mEventFlags = nullptr;
        return CHIP_ERROR_INTERNAL;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpClient::Stop()
{
    VerifyOrReturnError(mThreadId != nullptr, CHIP_NO_ERROR);
    VerifyOrReturnError(mEventFlags != nullptr, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);

    osEventFlagsSet(static_cast<osEventFlagsId_t>(mEventFlags), kEventStop);
    return CHIP_NO_ERROR;
}

void HttpClient::ResetRequestState()
{
    mResponseBytesWritten = 0;
    mEndOfFile            = 0;
    mHttpRspReceived      = 0;
    mCallbackStatus       = SL_STATUS_OK;
    mHttpOffset           = 0;
    mHttpChunkLength      = 0;
    mActiveResponseBuffer = nullptr;
    mPendingBody          = ByteSpan();
    mPendingHost          = {};
    mPendingResource      = nullptr;
    mBusy                 = false;
    mChunkedActive        = false;
}

CHIP_ERROR HttpClient::QueueOperation(Operation operation, HttpOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);

    mPendingOperation    = operation;
    mUserCallback        = callback;
    mUserCallbackContext = context;
    mBusy                = true;
    osEventFlagsSet(static_cast<osEventFlagsId_t>(mEventFlags), kEventOperation);
    return CHIP_NO_ERROR;
}

bool HttpClient::NeedsChunkedBody() const
{
    return mPendingBody.size() > SL_HTTP_CLIENT_MAX_WRITE_BUFFER_LENGTH;
}

CHIP_ERROR HttpClient::ProcessContinueChunkedBody()
{
    if (mEndOfFile == kHttpSuccessResponse)
    {
        mChunkedActive = false;
        mPendingBody   = ByteSpan();
        return CHIP_NO_ERROR;
    }

    const int32_t totalBodyLen = static_cast<int32_t>(mPendingBody.size());
    mHttpChunkLength           = ((totalBodyLen - mHttpOffset) > SL_HTTP_CLIENT_MAX_WRITE_BUFFER_LENGTH)
                  ? SL_HTTP_CLIENT_MAX_WRITE_BUFFER_LENGTH
                  : (totalBodyLen - mHttpOffset);

    if (mHttpChunkLength > 0)
    {
        sl_status_t status = sl_http_client_write_chunked_data(
            &mClientHandle, const_cast<uint8_t *>(mPendingBody.data() + mHttpOffset), static_cast<uint32_t>(mHttpChunkLength), 0);
        if (status == SL_STATUS_IN_PROGRESS)
        {
            mHttpOffset += mHttpChunkLength;
            return CHIP_ERROR_IN_PROGRESS;
        }
        VerifyOrReturnError(status == SL_STATUS_OK, MapStatus(status));
        mHttpOffset += mHttpChunkLength;
        return CHIP_NO_ERROR;
    }

    // No more body bytes; wait for final server response.
    mHttpRspReceived = 0;
    return CHIP_ERROR_IN_PROGRESS;
}

void HttpClient::HandleNwpResponse()
{
    CHIP_ERROR result = (mHttpRspReceived == kHttpSuccessResponse) ? CHIP_NO_ERROR : MapStatus(mCallbackStatus);
    mHttpRspReceived  = 0;

    if (result != CHIP_NO_ERROR)
    {
        CompleteOperation(result);
        return;
    }

    if (!mChunkedActive)
    {
        CompleteOperation(CHIP_NO_ERROR);
        return;
    }

    // Drive POST/PUT chunking on the service thread until NWP needs another wait or upload finishes.
    do
    {
        result = ProcessContinueChunkedBody();
    } while ((result == CHIP_NO_ERROR) && mChunkedActive);

    if (result == CHIP_ERROR_IN_PROGRESS)
    {
        return;
    }

    CompleteOperation(result);
}

CHIP_ERROR HttpClient::Init(const HttpClientConfig & config, HttpOperationCallback callback, void * context)
{
    VerifyOrReturnError(!mInitialized, CHIP_ERROR_INCORRECT_STATE);
    mConfig = config;
    return QueueOperation(Operation::Init, callback, context);
}

CHIP_ERROR HttpClient::ProcessInit()
{
    VerifyOrReturnError(!mInitialized, CHIP_ERROR_INCORRECT_STATE);

    if ((mConfig.tlsCaCert != nullptr) && (mConfig.tlsCaCertLen > 0) && !sNwpCaLoaded)
    {
        sl_status_t status =
            sl_net_set_credential(static_cast<sl_net_credential_id_t>(SL_NET_TLS_SERVER_CREDENTIAL_ID(mConfig.certificateIndex)),
                                  SL_NET_SIGNING_CERTIFICATE, mConfig.tlsCaCert, mConfig.tlsCaCertLen);
        if (status != SL_STATUS_OK)
        {
            ChipLogError(DeviceLayer, "HTTPS TLS CA load failed: 0x%lx", static_cast<unsigned long>(status));
            return MapStatus(status);
        }
        sNwpCaLoaded = true;
        ChipLogProgress(DeviceLayer, "HTTPS loaded TLS CA at index %u", mConfig.certificateIndex);
    }

    if ((mConfig.username != nullptr) && (mConfig.password != nullptr))
    {
        const uint16_t usernameLength = static_cast<uint16_t>(strlen(mConfig.username));
        const uint16_t passwordLength = static_cast<uint16_t>(strlen(mConfig.password));
        const uint32_t credentialSize = sizeof(sl_http_client_credentials_t) + usernameLength + passwordLength;

        auto * credentials = static_cast<sl_http_client_credentials_t *>(malloc(credentialSize));
        VerifyOrReturnError(credentials != nullptr, CHIP_ERROR_NO_MEMORY);
        memset(credentials, 0, credentialSize);
        credentials->username_length = usernameLength;
        credentials->password_length = passwordLength;
        memcpy(&credentials->data[0], mConfig.username, usernameLength);
        memcpy(&credentials->data[usernameLength], mConfig.password, passwordLength);

        sl_status_t status = sl_net_set_credential(static_cast<sl_net_credential_id_t>(SL_NET_HTTP_CLIENT_CREDENTIAL_ID(0)),
                                                   SL_NET_HTTP_CLIENT_CREDENTIAL, credentials, credentialSize);
        if (status != SL_STATUS_OK)
        {
            free(credentials);
            return MapStatus(status);
        }
        mCredentials = credentials;
    }

    sl_http_client_configuration_t clientConfiguration = {};
    clientConfiguration.network_interface              = SL_NET_WIFI_CLIENT_INTERFACE;
    clientConfiguration.ip_version                     = SL_IPV4;
    clientConfiguration.http_version                   = SL_HTTP_V_1_1;
    clientConfiguration.https_enable                   = mConfig.httpsEnable;
    clientConfiguration.tls_version                    = SL_TLS_V_1_2;
    clientConfiguration.certificate_index              = mConfig.certificateIndex;

    sl_status_t status = sl_http_client_init(&clientConfiguration, &mClientHandle);
    if (status != SL_STATUS_OK)
    {
        if (mCredentials != nullptr)
        {
            free(mCredentials);
            mCredentials = nullptr;
        }
        return MapStatus(status);
    }

    sl_http_client_tcp_tls_advanced_options_t tcpTlsOpts = {
        .tcp_keepalive_initial_time_sec   = 120,
        .tcp_max_retry_count              = 5,
        .max_retransmission_timeout_value = 2,
        .ssl_ciphers_bitmap               = 0,
        .ssl_ext_ciphers_bitmap           = 0,
    };
    status = sl_http_client_set_tcp_tls_advanced_configuration(&mClientHandle, &tcpTlsOpts);
    if (status != SL_STATUS_OK)
    {
        sl_http_client_deinit(&mClientHandle);
        mClientHandle = 0;
        if (mCredentials != nullptr)
        {
            free(mCredentials);
            mCredentials = nullptr;
        }
        return MapStatus(status);
    }

    mInitialized = true;
    ChipLogProgress(DeviceLayer, "HTTPS client init success");
    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpClient::Deinit(HttpOperationCallback callback, void * context)
{
    if (!mInitialized)
    {
        if (callback != nullptr)
        {
            callback(CHIP_NO_ERROR, context);
        }
        return CHIP_NO_ERROR;
    }
    return QueueOperation(Operation::Deinit, callback, context);
}

CHIP_ERROR HttpClient::ProcessDeinit()
{
    if (mClientHandle != 0)
    {
        sl_status_t status = sl_http_client_deinit(&mClientHandle);
        mClientHandle      = 0;
        if (status != SL_STATUS_OK)
        {
            ChipLogError(DeviceLayer, "HTTPS client deinit failed: 0x%lx", static_cast<unsigned long>(status));
            return MapStatus(status);
        }
    }

    if (mCredentials != nullptr)
    {
        free(mCredentials);
        mCredentials = nullptr;
    }

    ResetRequestState();
    mInitialized = false;

    if (sActiveClient == this)
    {
        sActiveClient = nullptr;
    }
    ChipLogProgress(DeviceLayer, "HTTPS client deinit success");
    return CHIP_NO_ERROR;
}

void HttpClient::CompleteOperation(CHIP_ERROR error)
{
    if (error != CHIP_NO_ERROR)
    {
        mChunkedActive = false;
        mPendingBody   = ByteSpan();
    }

    HttpOperationCallback callback = mUserCallback;
    void * context                 = mUserCallbackContext;

    mUserCallback        = nullptr;
    mUserCallbackContext = nullptr;
    mPendingOperation    = Operation::None;
    mBusy                = false;

    if (callback != nullptr)
    {
        callback(error, context);
    }
}

void HttpClient::SignalResponse()
{
    if (mEventFlags != nullptr)
    {
        osEventFlagsSet(static_cast<osEventFlagsId_t>(mEventFlags), kEventResponse);
    }
}

void HttpClient::ProcessOperation()
{
    CHIP_ERROR result = CHIP_ERROR_INTERNAL;

    switch (mPendingOperation)
    {
    case Operation::Init:
        result = ProcessInit();
        break;
    case Operation::Deinit:
        result = ProcessDeinit();
        break;
    case Operation::Get:
        result = ProcessGet();
        break;
    case Operation::Post:
        result = ProcessPost();
        break;
    case Operation::Put:
        result = ProcessPut();
        break;
    case Operation::None:
        result = CHIP_ERROR_INCORRECT_STATE;
        break;
    }

    if (result != CHIP_ERROR_IN_PROGRESS)
    {
        CompleteOperation(result);
    }
}

sl_status_t HttpClient::GetResponseCallback(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                            void * request_context)
{
    (void) client;
    (void) event;

    auto * self = ClientFromContext(request_context);
    VerifyOrReturnError(self != nullptr, SL_STATUS_FAIL);

    sl_http_client_response_t * getResponse = static_cast<sl_http_client_response_t *>(data);
    self->mCallbackStatus                   = getResponse->status;

    ChipLogProgress(DeviceLayer, "HTTPS GET response: status=0x%lX http_code=%u data_len=%u",
                    static_cast<unsigned long>(getResponse->status), getResponse->http_response_code, getResponse->data_length);

    if ((getResponse->status != SL_STATUS_OK) && (getResponse->status != SL_STATUS_IN_PROGRESS))
    {
        self->mHttpRspReceived = kHttpFailureResponse;
        self->mCallbackStatus  = SL_STATUS_FAIL;
        self->SignalResponse();
        return getResponse->status;
    }

    if ((getResponse->http_response_code >= kHttpClientErrorMin) && (getResponse->http_response_code <= kHttpServerErrorMax) &&
        (getResponse->http_response_code != 0))
    {
        self->mHttpRspReceived = kHttpFailureResponse;
        self->mCallbackStatus  = SL_STATUS_FAIL;
        self->SignalResponse();
        return getResponse->status;
    }

    if (getResponse->data_length)
    {
        if (self->mActiveResponseBuffer != nullptr)
        {
            // Count only bytes stored in the caller buffer, so reduce_size below stays in range.
            const size_t size    = self->mActiveResponseBuffer->size();
            const size_t avail   = (size > self->mResponseBytesWritten) ? size - self->mResponseBytesWritten : 0;
            const size_t copyLen = (getResponse->data_length < avail) ? getResponse->data_length : avail;
            if (copyLen > 0)
            {
                memcpy(self->mActiveResponseBuffer->data() + self->mResponseBytesWritten, getResponse->data_buffer, copyLen);
                self->mResponseBytesWritten += static_cast<uint32_t>(copyLen);
            }
            if (copyLen < getResponse->data_length)
            {
                ChipLogError(DeviceLayer, "HTTPS GET response truncated, dropped %u bytes",
                             static_cast<unsigned>(getResponse->data_length - copyLen));
            }
        }
    }

    if (getResponse->end_of_data)
    {
        if (self->mActiveResponseBuffer != nullptr)
        {
            self->mActiveResponseBuffer->reduce_size(self->mResponseBytesWritten);
        }
        self->mHttpRspReceived      = kHttpSuccessResponse;
        self->mResponseBytesWritten = 0;
        self->SignalResponse();
    }

    return SL_STATUS_OK;
}

sl_status_t HttpClient::PostResponseCallback(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                             void * request_context)
{
    (void) client;
    (void) event;

    auto * self = ClientFromContext(request_context);
    VerifyOrReturnError(self != nullptr, SL_STATUS_FAIL);

    sl_http_client_response_t * postResponse = static_cast<sl_http_client_response_t *>(data);
    self->mCallbackStatus                    = postResponse->status;

    ChipLogProgress(DeviceLayer, "HTTPS POST response: status=0x%lX http_code=%u data_len=%u",
                    static_cast<unsigned long>(postResponse->status), postResponse->http_response_code, postResponse->data_length);

    if ((postResponse->status != SL_STATUS_OK) && (postResponse->status != SL_STATUS_IN_PROGRESS))
    {
        self->mHttpRspReceived = kHttpFailureResponse;
        self->SignalResponse();
        return postResponse->status;
    }

    if ((postResponse->http_response_code >= kHttpClientErrorMin) && (postResponse->http_response_code <= kHttpServerErrorMax) &&
        (postResponse->http_response_code != 0))
    {
        self->mHttpRspReceived = kHttpFailureResponse;
        self->mCallbackStatus  = SL_STATUS_FAIL;
        self->SignalResponse();
        return postResponse->status;
    }

    if (self->mChunkedActive)
    {
        self->mHttpRspReceived = kHttpSuccessResponse;
        if (postResponse->end_of_data)
        {
            self->mEndOfFile = kHttpSuccessResponse;
        }
        self->SignalResponse();
        return SL_STATUS_OK;
    }

    if (postResponse->end_of_data)
    {
        self->mHttpRspReceived = kHttpSuccessResponse;
        self->SignalResponse();
    }

    return SL_STATUS_OK;
}

sl_status_t HttpClient::PutResponseCallback(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                            void * request_context)
{
    (void) client;
    (void) event;

    auto * self = ClientFromContext(request_context);
    VerifyOrReturnError(self != nullptr, SL_STATUS_FAIL);

    sl_http_client_response_t * putResponse = static_cast<sl_http_client_response_t *>(data);
    self->mCallbackStatus                   = putResponse->status;

    ChipLogProgress(DeviceLayer, "HTTPS PUT response: status=0x%lX end_of_data=%lu data_len=%u",
                    static_cast<unsigned long>(putResponse->status), static_cast<unsigned long>(putResponse->end_of_data),
                    putResponse->data_length);

    if ((putResponse->status != SL_STATUS_OK) && (putResponse->status != SL_STATUS_IN_PROGRESS))
    {
        self->mHttpRspReceived = kHttpFailureResponse;
        self->SignalResponse();
        return putResponse->status;
    }

    // PUT response body is not exposed by the public API; ignore payload bytes.
    self->mHttpRspReceived = kHttpSuccessResponse;

    if (putResponse->end_of_data == SL_HTTP_CLIENT_PUT_SERVER_RESPONSE_END_OF_DATA)
    {
        self->mEndOfFile = kHttpSuccessResponse;
    }

    self->SignalResponse();
    return SL_STATUS_OK;
}

CHIP_ERROR HttpClient::Get(const HttpHost & host, const char * resource, MutableByteSpan & responseBuffer,
                           HttpOperationCallback callback, void * context)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy && !mChunkedActive, CHIP_ERROR_BUSY);
    VerifyOrReturnError((host.hostName != nullptr) && (host.serverIp != nullptr) && (resource != nullptr),
                        CHIP_ERROR_INVALID_ARGUMENT);

    ResetRequestState();
    mActiveResponseBuffer = &responseBuffer;
    mPendingHost          = host;
    mPendingResource      = resource;
    return QueueOperation(Operation::Get, callback, context);
}

CHIP_ERROR HttpClient::ProcessGet()
{
    sl_http_client_request_t request = {};
    request.ip_address               = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingHost.serverIp));
    request.host_name                = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingHost.hostName));
    request.port                     = mPendingHost.port;
    request.resource                 = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingResource));
    request.extended_header          = nullptr;
    request.http_method_type         = SL_HTTP_GET;
    request.body                     = nullptr;
    request.body_length              = 0;

    sActiveClient      = this;
    sl_status_t status = sl_http_client_request_init(&request, GetResponseCallback, this);
    VerifyOrReturnError(status == SL_STATUS_OK, MapStatus(status));

    status = sl_http_client_send_request(&mClientHandle, &request);
    VerifyOrReturnError((status == SL_STATUS_OK) || (status == SL_STATUS_IN_PROGRESS), MapStatus(status));

    return CHIP_ERROR_IN_PROGRESS;
}

CHIP_ERROR HttpClient::Post(const HttpHost & host, const char * resource, ByteSpan body, HttpOperationCallback callback,
                            void * context)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy && !mChunkedActive, CHIP_ERROR_BUSY);
    VerifyOrReturnError((host.hostName != nullptr) && (host.serverIp != nullptr) && (resource != nullptr),
                        CHIP_ERROR_INVALID_ARGUMENT);

    ResetRequestState();
    mPendingHost     = host;
    mPendingResource = resource;
    mPendingBody     = body;
    return QueueOperation(Operation::Post, callback, context);
}

CHIP_ERROR HttpClient::ProcessPost()
{
    sl_http_client_request_t request = {};
    request.ip_address               = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingHost.serverIp));
    request.host_name                = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingHost.hostName));
    request.port                     = mPendingHost.port;
    request.resource                 = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingResource));
    request.extended_header          = nullptr;
    request.http_method_type         = SL_HTTP_POST;
    request.body_length              = static_cast<uint32_t>(mPendingBody.size());

    if (NeedsChunkedBody())
    {
        request.body   = nullptr;
        mChunkedActive = true;
    }
    else
    {
        request.body = const_cast<uint8_t *>(mPendingBody.data());
    }

    sActiveClient      = this;
    sl_status_t status = sl_http_client_request_init(&request, PostResponseCallback, this);
    if (status != SL_STATUS_OK)
    {
        mChunkedActive = false;
        mPendingBody   = ByteSpan();
        return MapStatus(status);
    }

    status = sl_http_client_send_request(&mClientHandle, &request);
    if ((status != SL_STATUS_OK) && (status != SL_STATUS_IN_PROGRESS))
    {
        mChunkedActive = false;
        mPendingBody   = ByteSpan();
        return MapStatus(status);
    }

    return CHIP_ERROR_IN_PROGRESS;
}

CHIP_ERROR HttpClient::Put(const HttpHost & host, const char * resource, ByteSpan body, HttpOperationCallback callback,
                           void * context)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy && !mChunkedActive, CHIP_ERROR_BUSY);
    VerifyOrReturnError((host.hostName != nullptr) && (host.serverIp != nullptr) && (resource != nullptr),
                        CHIP_ERROR_INVALID_ARGUMENT);

    ResetRequestState();
    mPendingBody     = body;
    mChunkedActive   = true;
    mPendingHost     = host;
    mPendingResource = resource;
    return QueueOperation(Operation::Put, callback, context);
}

CHIP_ERROR HttpClient::ProcessPut()
{
    sl_http_client_request_t request = {};
    request.ip_address               = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingHost.serverIp));
    request.host_name                = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingHost.hostName));
    request.port                     = mPendingHost.port;
    request.resource                 = reinterpret_cast<uint8_t *>(const_cast<char *>(mPendingResource));
    request.extended_header          = nullptr;
    request.http_method_type         = SL_HTTP_PUT;
    request.body                     = nullptr;
    request.body_length              = static_cast<uint32_t>(mPendingBody.size());

    sActiveClient      = this;
    sl_status_t status = sl_http_client_request_init(&request, PutResponseCallback, this);
    if (status != SL_STATUS_OK)
    {
        mChunkedActive = false;
        mPendingBody   = ByteSpan();
        return MapStatus(status);
    }

    status = sl_http_client_send_request(&mClientHandle, &request);
    if ((status != SL_STATUS_OK) && (status != SL_STATUS_IN_PROGRESS))
    {
        mChunkedActive = false;
        mPendingBody   = ByteSpan();
        return MapStatus(status);
    }

    return CHIP_ERROR_IN_PROGRESS;
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
