/**
 * @file
 * @brief Minimal HTTPS GET over forked altcp TLS (HTTPS Client Temp).
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#include "HTTPS_client.h"
#include "HTTPS_transport.h"
#include "HttpsClientTempConfig.h"

#include "https_client_temp_altcp.h"
#include "https_client_temp_altcp_tls.h"

#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"

#include "mbedtls/ssl.h"

#include "FreeRTOS.h"
#include "task.h"

#include "silabs_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    struct altcp_pcb * pcb;
    char request[512];
    https_client_temp_http_done_fn done_fn;
    void * done_arg;
    u32_t rx_bytes;
    int http_status;
    bool got_status;
    u8_t poll_count;
    bool closing;
} https_client_temp_http_t;

static https_client_temp_http_t * s_active_mbedtls_client = NULL;

extern err_t https_client_temp_altcp_mbedtls_lower_recv_process(struct altcp_pcb * conn);

void mbedtls_signal_app(void * arg)
{
    s_active_mbedtls_client = (https_client_temp_http_t *) arg;
    HTTPS_Transport_NotifyMbedtlsRx();
}

static void https_client_temp_mbedtls_rx_tcpip_cb(void * ctx)
{
    (void) ctx;

    if (s_active_mbedtls_client != NULL && s_active_mbedtls_client->pcb != NULL && !s_active_mbedtls_client->closing)
    {
        https_client_temp_altcp_mbedtls_lower_recv_process(s_active_mbedtls_client->pcb);
    }
}

void https_client_temp_transport_process_mbedtls_rx(void)
{
    err_t err;

    if (s_active_mbedtls_client == NULL || s_active_mbedtls_client->pcb == NULL || s_active_mbedtls_client->closing)
    {
        return;
    }

    err = tcpip_callback(https_client_temp_mbedtls_rx_tcpip_cb, NULL);
    if (err != ERR_OK)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] tcpip_callback(mbedtls_rx) failed: %d", (int) err);
    }
}

static void https_client_temp_http_clear_active(https_client_temp_http_t * client)
{
    if (s_active_mbedtls_client == client)
    {
        s_active_mbedtls_client = NULL;
    }
}

static void https_client_temp_http_close(https_client_temp_http_t * client)
{
    if (client == NULL || client->closing)
    {
        return;
    }

    client->closing = true;
    https_client_temp_http_clear_active(client);

    if (client->pcb != NULL)
    {
        https_client_temp_altcp_arg(client->pcb, NULL);
        https_client_temp_altcp_recv(client->pcb, NULL);
        https_client_temp_altcp_sent(client->pcb, NULL);
        https_client_temp_altcp_err(client->pcb, NULL);
        https_client_temp_altcp_poll(client->pcb, NULL, 0);
        https_client_temp_altcp_close(client->pcb);
        client->pcb = NULL;
    }

    vPortFree(client);
}

static void https_client_temp_http_notify_error(https_client_temp_http_t * client, err_t err)
{
    if (client == NULL || client->closing)
    {
        return;
    }

    client->closing = true;
    https_client_temp_http_clear_active(client);
    /* TLS/altcp owns pcb teardown after err; do not altcp_close here (avoids double-free). */
    client->pcb = NULL;

    if (client->done_fn != NULL)
    {
        client->done_fn(client->done_arg, err, client->http_status, client->rx_bytes);
    }

    vPortFree(client);
}

static err_t https_client_temp_http_connected(void * arg, struct altcp_pcb * pcb, err_t err)
{
    https_client_temp_http_t * client = (https_client_temp_http_t *) arg;

    if (client == NULL || client->closing)
    {
        return ERR_VAL;
    }

    if (err != ERR_OK)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] HTTP connect failed: %d heap_free=%u", (int) err, (unsigned) xPortGetFreeHeapSize());
        if (client->done_fn != NULL)
        {
            client->done_fn(client->done_arg, err, 0, client->rx_bytes);
        }
        https_client_temp_http_close(client);
        return err;
    }

    err = https_client_temp_altcp_write(pcb, client->request, (u16_t) strlen(client->request), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] HTTP write failed: %d", (int) err);
        if (client->done_fn != NULL)
        {
            client->done_fn(client->done_arg, err, 0, client->rx_bytes);
        }
        https_client_temp_http_close(client);
        return err;
    }

    return https_client_temp_altcp_output(pcb);
}

static void https_client_temp_http_try_parse_status(https_client_temp_http_t * client, const u8_t * data, u16_t len)
{
    if (client->got_status || len < 12)
    {
        return;
    }

    if (strncmp((const char *) data, "HTTP/", 5) == 0)
    {
        const char * sp = strchr((const char *) data, ' ');
        if (sp != NULL)
        {
            client->http_status = atoi(sp + 1);
            client->got_status  = true;
        }
    }
}

static err_t https_client_temp_http_recv(void * arg, struct altcp_pcb * pcb, struct pbuf * p, err_t err)
{
    https_client_temp_http_t * client = (https_client_temp_http_t *) arg;

    if ((err != ERR_OK) || (client == NULL) || client->closing)
    {
        if (p != NULL)
        {
            pbuf_free(p);
        }
        return err;
    }

    if (p == NULL)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] HTTP response complete status=%d rx=%u", client->http_status,
                   (unsigned) client->rx_bytes);
        if (client->done_fn != NULL)
        {
            client->done_fn(client->done_arg, ERR_OK, client->http_status, client->rx_bytes);
        }
        https_client_temp_http_close(client);
        return ERR_OK;
    }

    https_client_temp_altcp_recved(pcb, p->tot_len);
    client->rx_bytes += p->tot_len;

    if (!client->got_status && p->len > 0)
    {
        https_client_temp_http_try_parse_status(client, (const u8_t *) p->payload, p->len);
    }

    SILABS_LOG("[HTTPS_CLIENT_TEMP] HTTP RX %u bytes (total=%u)", (unsigned) p->tot_len, (unsigned) client->rx_bytes);
    pbuf_free(p);
    return ERR_OK;
}

static void https_client_temp_http_error(void * arg, err_t err)
{
    https_client_temp_http_t * client = (https_client_temp_http_t *) arg;

    SILABS_LOG("[HTTPS_CLIENT_TEMP] HTTP altcp error: %d", (int) err);

    https_client_temp_http_notify_error(client, err);
}

static err_t https_client_temp_http_sent(void * arg, struct altcp_pcb * pcb, u16_t len)
{
    (void) arg;
    (void) pcb;
    (void) len;
    return ERR_OK;
}

static err_t https_client_temp_http_poll(void * arg, struct altcp_pcb * pcb)
{
    https_client_temp_http_t * client = (https_client_temp_http_t *) arg;

    (void) pcb;

    if (client == NULL || client->closing)
    {
        return ERR_OK;
    }

    client->poll_count++;
    if (client->poll_count >= HTTPS_CLIENT_TEMP_HTTP_POLL_MAX)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] HTTP request timed out (poll=%u heap_free=%u)", (unsigned) client->poll_count,
                   (unsigned) xPortGetFreeHeapSize());
        if (client->done_fn != NULL)
        {
            client->done_fn(client->done_arg, ERR_TIMEOUT, client->http_status, client->rx_bytes);
        }
        https_client_temp_http_close(client);
    }

    return ERR_OK;
}

err_t HTTPS_Client_Get(HTTPS_Transport_t * transport, const ip_addr_t * server_addr, u16_t port, const char * host_header,
                       const char * uri, https_client_temp_http_done_fn done_fn, void * done_arg)
{
    https_client_temp_http_t * client;
    struct altcp_tls_config * tls_config;
    err_t err;
    int req_len;

    if ((transport == NULL) || (server_addr == NULL) || (host_header == NULL) || (uri == NULL))
    {
        return ERR_ARG;
    }

    tls_config = HTTPS_Transport_GetTlsConfig(transport);
    if (tls_config == NULL)
    {
        return ERR_VAL;
    }

    client = (https_client_temp_http_t *) pvPortMalloc(sizeof(https_client_temp_http_t));
    if (client == NULL)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] HTTP client alloc failed (sz=%u heap_free=%u)", (unsigned) sizeof(https_client_temp_http_t),
                   (unsigned) xPortGetFreeHeapSize());
        return ERR_MEM;
    }

    memset(client, 0, sizeof(https_client_temp_http_t));
    client->done_fn  = done_fn;
    client->done_arg = done_arg;
    client->http_status = 0;
    client->poll_count  = 0;

    req_len = snprintf(client->request, sizeof(client->request),
                       "GET %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       uri, host_header);
    if ((req_len < 0) || ((size_t) req_len >= sizeof(client->request)))
    {
        vPortFree(client);
        return ERR_VAL;
    }

    client->pcb = https_client_temp_altcp_tls_new(tls_config, IPADDR_TYPE_V4);
    if (client->pcb == NULL)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] https_client_temp_altcp_tls_new failed heap_free=%u",
                   (unsigned) xPortGetFreeHeapSize());
        vPortFree(client);
        return ERR_MEM;
    }

    https_client_temp_altcp_arg(client->pcb, client);
    https_client_temp_altcp_recv(client->pcb, https_client_temp_http_recv);
    https_client_temp_altcp_sent(client->pcb, https_client_temp_http_sent);
    https_client_temp_altcp_err(client->pcb, https_client_temp_http_error);
    https_client_temp_altcp_poll(client->pcb, https_client_temp_http_poll, 2);

    {
        mbedtls_ssl_context * ssl = (mbedtls_ssl_context *) https_client_temp_altcp_tls_context(client->pcb);
        if (ssl != NULL)
        {
            int hostname_ret = mbedtls_ssl_set_hostname(ssl, host_header);
            if (hostname_ret != 0)
            {
                SILABS_LOG("[HTTPS_CLIENT_TEMP] mbedtls_ssl_set_hostname failed: %d", hostname_ret);
                https_client_temp_http_close(client);
                return ERR_VAL;
            }
        }
    }

    s_active_mbedtls_client = client;

    err = https_client_temp_altcp_connect(client->pcb, server_addr, port, https_client_temp_http_connected);
    if (err != ERR_OK)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] https_client_temp_altcp_connect failed: %d heap_free=%u", (int) err,
                   (unsigned) xPortGetFreeHeapSize());
        https_client_temp_http_close(client);
        return err;
    }

    return ERR_OK;
}
