/***************************************************************************/
/**
 * @file http_client.c
 * @brief NWP offload HTTPS client demo (invoked from Matter)
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/

#include "http_client.h"

#include "cacert.pem.h"
#include "cmsis_os2.h"
#include "index.html.h"
#include "silabs_utils.h"
#include "sl_additional_status.h"
#include "sl_http_client.h"
#include "sl_net.h"
#include "sl_status.h"
#include <stdlib.h>
#include <string.h>

#define TLS_CERTIFICATE_INDEX 1

#define HTTP_CLIENT_USERNAME "admin"
#define HTTP_CLIENT_PASSWORD "admin"

#define HTTP_IP_VERSION SL_IPV4
#define HTTP_VERSION SL_HTTP_V_1_1
#define HTTP_TLS_VERSION SL_TLS_V_1_2
#define HTTP_CERT_INDEX SL_HTTPS_CLIENT_CERTIFICATE_INDEX_1

#define HTTP_SERVER_IP "192.168.0.191"
#define HTTP_HOSTNAME "192.168.0.191"
#define HTTP_PORT 8443

#define HTTP_URL "/index.html"
#define HTTP_DATA                                                                                                                  \
    "employee_name=MR.REDDY&employee_id=RSXYZ123&designation=Engineer&company="                                                    \
    "SILABS&location=Hyderabad"

#define APP_BUFFER_LENGTH 2000

#define HTTP_SUCCESS_RESPONSE 1
#define HTTP_FAILURE_RESPONSE 2

#define HTTP_STATUS_CLIENT_ERROR_MIN 400U
#define HTTP_STATUS_SERVER_ERROR_MAX 599U
#define HTTP_STATUS_CODE_NONE 0U

#define HTTP_SYNC_RESPONSE 0
#define HTTP_ASYNC_RESPONSE 1

#define CLEAN_HTTP_CLIENT_IF_FAILED(status, client_handle, is_sync, cb_status)                                                     \
    do                                                                                                                             \
    {                                                                                                                              \
        if ((status) != SL_STATUS_OK && (status) != SL_STATUS_IN_PROGRESS)                                                         \
        {                                                                                                                          \
            sl_http_client_deinit(client_handle);                                                                                  \
            return (((is_sync) == HTTP_SYNC_RESPONSE) ? (status) : (cb_status));                                                   \
        }                                                                                                                          \
    } while (0)

static uint8_t app_buffer[APP_BUFFER_LENGTH] = { 0 };
static uint32_t app_buff_index               = 0;

static volatile uint8_t http_rsp_received = 0;
static volatile uint8_t end_of_file       = 0;
static sl_status_t callback_status        = SL_STATUS_OK;
static int32_t http_offset                = 0;
static int32_t http_chunk_length          = 0;

static osThreadId_t s_https_demo_thread_id = NULL;
static bool s_nwp_ca_loaded                = false;

static const osThreadAttr_t https_offload_thread_attributes = {
    .name       = "https_offload",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = (2 * 1024),
    .priority   = osPriorityBelowNormal,
    .tz_module  = 0,
};

static sl_status_t https_offload_example(void);
static void https_offload_thread(void * argument);
static sl_status_t http_response_status(volatile uint8_t * response);
static void reset_http_handles(void);
static sl_status_t load_nwp_tls_ca(void);

static sl_status_t load_nwp_tls_ca(void)
{
    sl_status_t status;

    if (s_nwp_ca_loaded)
    {
        return SL_STATUS_OK;
    }

    status = sl_net_set_credential(SL_NET_TLS_SERVER_CREDENTIAL_ID(TLS_CERTIFICATE_INDEX), SL_NET_SIGNING_CERTIFICATE, cacert,
                                   sizeof(cacert) - 1);
    if (status != SL_STATUS_OK)
    {
        SILABS_LOG("HTTPS error: loading TLS CA into flash failed: 0x%lx", status);
        return status;
    }

    s_nwp_ca_loaded = true;
    SILABS_LOG("HTTPS loaded TLS CA at index %d", TLS_CERTIFICATE_INDEX);
    return SL_STATUS_OK;
}

static sl_status_t http_response_status(volatile uint8_t * response)
{
    while (*response != HTTP_SUCCESS_RESPONSE && *response != HTTP_FAILURE_RESPONSE)
    {
        osDelay(1);
    }

    if (*response != HTTP_SUCCESS_RESPONSE)
    {
        return SL_STATUS_FAIL;
    }

    *response = 0;
    return SL_STATUS_OK;
}

static void reset_http_handles(void)
{
    app_buff_index    = 0;
    end_of_file       = 0;
    http_rsp_received = 0;
    callback_status   = SL_STATUS_OK;
    http_offset       = 0;
    http_chunk_length = 0;
}

static sl_status_t http_put_response_callback_handler(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                                      void * request_context)
{
    (void) client;
    (void) event;
    (void) request_context;

    sl_http_client_response_t * put_response = (sl_http_client_response_t *) data;
    callback_status                          = put_response->status;

    SILABS_LOG("HTTPS PUT response: status=0x%X end_of_data=%lu data_len=%u", put_response->status,
               (unsigned long) put_response->end_of_data, put_response->data_length);

    if (put_response->status != SL_STATUS_OK && put_response->status != SL_STATUS_IN_PROGRESS)
    {
        http_rsp_received = HTTP_FAILURE_RESPONSE;
        return put_response->status;
    }

    if (put_response->data_length)
    {
        if (APP_BUFFER_LENGTH > (app_buff_index + put_response->data_length))
        {
            memcpy(app_buffer + app_buff_index, put_response->data_buffer, put_response->data_length);
        }
        app_buff_index += put_response->data_length;
    }
    http_rsp_received = HTTP_SUCCESS_RESPONSE;

    if (put_response->end_of_data == SL_HTTP_CLIENT_PUT_SERVER_RESPONSE_END_OF_DATA)
    {
        end_of_file = HTTP_SUCCESS_RESPONSE;
    }

    return SL_STATUS_OK;
}

static sl_status_t http_get_response_callback_handler(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                                      void * request_context)
{
    (void) client;
    (void) event;
    (void) request_context;

    sl_http_client_response_t * get_response = (sl_http_client_response_t *) data;
    callback_status                          = get_response->status;

    SILABS_LOG("HTTPS GET response: status=0x%X http_code=%u data_len=%u", get_response->status, get_response->http_response_code,
               get_response->data_length);

    if (get_response->status != SL_STATUS_OK && get_response->status != SL_STATUS_IN_PROGRESS)
    {
        http_rsp_received = HTTP_FAILURE_RESPONSE;
        callback_status   = SL_STATUS_FAIL;
        return get_response->status;
    }

    if (get_response->http_response_code >= HTTP_STATUS_CLIENT_ERROR_MIN &&
        get_response->http_response_code <= HTTP_STATUS_SERVER_ERROR_MAX &&
        get_response->http_response_code != HTTP_STATUS_CODE_NONE)
    {
        http_rsp_received = HTTP_FAILURE_RESPONSE;
        callback_status   = get_response->status;
        return get_response->status;
    }

    if (get_response->data_length)
    {
        if (APP_BUFFER_LENGTH > (app_buff_index + get_response->data_length))
        {
            memcpy(app_buffer + app_buff_index, get_response->data_buffer, get_response->data_length);
        }
        app_buff_index += get_response->data_length;
    }

    if (get_response->end_of_data)
    {
        http_rsp_received = HTTP_SUCCESS_RESPONSE;
        app_buff_index    = 0;
    }

    return SL_STATUS_OK;
}

static sl_status_t http_post_response_callback_handler(const sl_http_client_t * client, sl_http_client_event_t event, void * data,
                                                       void * request_context)
{
    (void) client;
    (void) event;
    (void) request_context;

    sl_http_client_response_t * post_response = (sl_http_client_response_t *) data;
    callback_status                           = post_response->status;

    SILABS_LOG("HTTPS POST response: status=0x%X http_code=%u data_len=%u", post_response->status,
               post_response->http_response_code, post_response->data_length);

    if (post_response->status != SL_STATUS_OK && post_response->status != SL_STATUS_IN_PROGRESS)
    {
        http_rsp_received = HTTP_FAILURE_RESPONSE;
        return post_response->status;
    }

    if (post_response->http_response_code >= HTTP_STATUS_CLIENT_ERROR_MIN &&
        post_response->http_response_code <= HTTP_STATUS_SERVER_ERROR_MAX &&
        post_response->http_response_code != HTTP_STATUS_CODE_NONE)
    {
        http_rsp_received = HTTP_FAILURE_RESPONSE;
        return post_response->status;
    }

    if (post_response->end_of_data)
    {
        http_rsp_received = HTTP_SUCCESS_RESPONSE;
    }

    return SL_STATUS_OK;
}

static sl_status_t https_offload_example(void)
{
    sl_status_t status                                  = SL_STATUS_OK;
    sl_http_client_t client_handle                      = 0;
    sl_http_client_configuration_t client_configuration = { 0 };
    sl_http_client_request_t client_request             = { 0 };
    int32_t total_put_data_len                          = sizeof(sl_index) - 1;

    reset_http_handles();

    uint16_t username_length = strlen(HTTP_CLIENT_USERNAME);
    uint16_t password_length = strlen(HTTP_CLIENT_PASSWORD);
    uint32_t credential_size = sizeof(sl_http_client_credentials_t) + username_length + password_length;

    sl_http_client_credentials_t * client_credentials = (sl_http_client_credentials_t *) malloc(credential_size);
    SL_VERIFY_POINTER_OR_RETURN(client_credentials, SL_STATUS_ALLOCATION_FAILED);
    memset(client_credentials, 0, credential_size);
    client_credentials->username_length = username_length;
    client_credentials->password_length = password_length;

    memcpy(&client_credentials->data[0], HTTP_CLIENT_USERNAME, username_length);
    memcpy(&client_credentials->data[username_length], HTTP_CLIENT_PASSWORD, password_length);

    status = sl_net_set_credential(SL_NET_HTTP_CLIENT_CREDENTIAL_ID(0), SL_NET_HTTP_CLIENT_CREDENTIAL, client_credentials,
                                   credential_size);
    if (status != SL_STATUS_OK)
    {
        free(client_credentials);
        return status;
    }

    client_configuration.network_interface = SL_NET_WIFI_CLIENT_INTERFACE;
    client_configuration.ip_version        = HTTP_IP_VERSION;
    client_configuration.http_version      = HTTP_VERSION;
    client_configuration.https_enable      = true;
    client_configuration.tls_version       = HTTP_TLS_VERSION;
    client_configuration.certificate_index = HTTP_CERT_INDEX;

    client_request.ip_address      = (uint8_t *) HTTP_SERVER_IP;
    client_request.host_name       = (uint8_t *) HTTP_HOSTNAME;
    client_request.port            = HTTP_PORT;
    client_request.resource        = (uint8_t *) HTTP_URL;
    client_request.extended_header = NULL;

    status = sl_http_client_init(&client_configuration, &client_handle);
    if (status != SL_STATUS_OK)
    {
        free(client_credentials);
        return status;
    }
    SILABS_LOG("HTTPS client init success");

    sl_http_client_tcp_tls_advanced_options_t tcp_tls_opts = {
        .tcp_keepalive_initial_time_sec   = 120,
        .tcp_max_retry_count              = 5,
        .max_retransmission_timeout_value = 2,
        .ssl_ciphers_bitmap               = 0,
        .ssl_ext_ciphers_bitmap           = 0,
    };
    status = sl_http_client_set_tcp_tls_advanced_configuration(&client_handle, &tcp_tls_opts);
    CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_SYNC_RESPONSE, callback_status);
    SILABS_LOG("HTTPS TCP/TLS advanced configuration set");

    client_request.http_method_type = SL_HTTP_PUT;
    client_request.body             = NULL;
    client_request.body_length      = total_put_data_len;

    status = sl_http_client_request_init(&client_request, http_put_response_callback_handler, "This is HTTP client");
    CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_SYNC_RESPONSE, callback_status);
    SILABS_LOG("HTTPS PUT request init success");

    status = sl_http_client_send_request(&client_handle, &client_request);
    CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_SYNC_RESPONSE, callback_status);
    if (http_rsp_received != HTTP_SUCCESS_RESPONSE)
    {
        status = http_response_status(&http_rsp_received);
        CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_ASYNC_RESPONSE, callback_status);
    }

    while (!end_of_file)
    {
        http_chunk_length = ((total_put_data_len - http_offset) > SL_HTTP_CLIENT_MAX_WRITE_BUFFER_LENGTH)
            ? SL_HTTP_CLIENT_MAX_WRITE_BUFFER_LENGTH
            : (total_put_data_len - http_offset);

        if (http_chunk_length > 0)
        {
            status = sl_http_client_write_chunked_data(&client_handle, (uint8_t *) (sl_index + http_offset), http_chunk_length, 0);
            if (status == SL_STATUS_IN_PROGRESS)
            {
                status = http_response_status(&http_rsp_received);
                CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_ASYNC_RESPONSE, callback_status);
                http_offset += http_chunk_length;
            }
            else
            {
                CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_SYNC_RESPONSE, callback_status);
            }
        }
        else
        {
            http_rsp_received = 0;
            status            = http_response_status(&http_rsp_received);
            CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_ASYNC_RESPONSE, callback_status);
        }
    }

    SILABS_LOG("HTTPS PUT request success");
    reset_http_handles();

    client_request.http_method_type = SL_HTTP_GET;

    status = sl_http_client_request_init(&client_request, http_get_response_callback_handler, "This is HTTP client");
    CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_SYNC_RESPONSE, callback_status);
    SILABS_LOG("HTTPS GET request init success");

    status = sl_http_client_send_request(&client_handle, &client_request);
    CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_SYNC_RESPONSE, callback_status);
    if (http_rsp_received != HTTP_SUCCESS_RESPONSE)
    {
        status = http_response_status(&http_rsp_received);
        CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_ASYNC_RESPONSE, callback_status);
    }

    SILABS_LOG("HTTPS GET request success");
    reset_http_handles();

    client_request.http_method_type = SL_HTTP_POST;
    client_request.body             = (uint8_t *) HTTP_DATA;
    client_request.body_length      = strlen(HTTP_DATA);

    status = sl_http_client_request_init(&client_request, http_post_response_callback_handler, "This is HTTP client");
    CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_SYNC_RESPONSE, callback_status);
    SILABS_LOG("HTTPS POST request init success");

    status = sl_http_client_send_request(&client_handle, &client_request);
    CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_SYNC_RESPONSE, callback_status);
    if (http_rsp_received != HTTP_SUCCESS_RESPONSE)
    {
        status = http_response_status(&http_rsp_received);
        CLEAN_HTTP_CLIENT_IF_FAILED(status, &client_handle, HTTP_ASYNC_RESPONSE, callback_status);
    }

    SILABS_LOG("HTTPS POST request success");
    reset_http_handles();

    status = sl_http_client_deinit(&client_handle);
    if (status != SL_STATUS_OK)
    {
        free(client_credentials);
        return status;
    }
    SILABS_LOG("HTTPS client deinit success");
    free(client_credentials);

    return status;
}

static void https_offload_thread(void * argument)
{
    (void) argument;
    sl_status_t status;

    SILABS_LOG("HTTPS starting on offload stack");

    status = https_offload_example();
    if (status != SL_STATUS_OK)
    {
        SILABS_LOG("HTTPS error: demo failed: 0x%lx", status);
    }
    else
    {
        SILABS_LOG("HTTPS demo completed");
    }

    s_https_demo_thread_id = NULL;
    osThreadTerminate(osThreadGetId());
}

sl_status_t http_client_demo_start(void)
{
    sl_status_t status;

    if (s_https_demo_thread_id != NULL)
    {
        return SL_STATUS_ALREADY_INITIALIZED;
    }

    status = load_nwp_tls_ca();
    if (status != SL_STATUS_OK)
    {
        return status;
    }

    s_https_demo_thread_id = osThreadNew((osThreadFunc_t) https_offload_thread, NULL, &https_offload_thread_attributes);
    if (s_https_demo_thread_id == NULL)
    {
        return SL_STATUS_FAIL;
    }

    return SL_STATUS_OK;
}
