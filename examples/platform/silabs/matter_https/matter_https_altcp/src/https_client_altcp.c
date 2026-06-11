#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "https_client_altcp.h"
#include "MatterHttpsConfig.h"

#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/def.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/mem.h"

#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/debug.h"

#include "silabs_utils.h"

#ifndef LWIP_UNUSED_ARG
#define LWIP_UNUSED_ARG(x) (void) (x)
#endif

#if defined(MBEDTLS_SSL_TLS_C)
const char * mbedtls_ssl_states_str(mbedtls_ssl_states in);
#endif

typedef struct
{
    struct altcp_pcb * pcb;
    char request[512];
    u32_t rx_bytes;
    int http_status;
} https_client_t;

#if !MATTER_HTTPS_SKIP_CA_VERIFY
static const char ca_cert_pem[] = "";

static const size_t ca_cert_pem_len = sizeof(ca_cert_pem);
#endif // !MATTER_HTTPS_SKIP_CA_VERIFY

static err_t https_client_connected(void * arg, struct altcp_pcb * pcb, err_t err);
static err_t https_client_recv(void * arg, struct altcp_pcb * pcb, struct pbuf * p, err_t err);
static err_t https_client_sent(void * arg, struct altcp_pcb * pcb, u16_t len);
static void https_client_error(void * arg, err_t err);
static err_t https_client_poll(void * arg, struct altcp_pcb * pcb);
static void https_client_close(https_client_t * client);
static void https_client_log_tls_diagnostics(struct altcp_pcb * pcb);

static void https_client_try_parse_status(https_client_t * client, const u8_t * data, u16_t len)
{
    int status;

    if ((client == NULL) || (client->http_status != 0) || (len < 12))
    {
        return;
    }

    if (sscanf((const char *) data, "HTTP/%*d.%*d %d", &status) == 1)
    {
        client->http_status = status;
        SILABS_LOG("[MATTER_HTTPS] HTTP status %d", status);
    }
}

static void https_client_log_tls_diagnostics(struct altcp_pcb * pcb)
{
    mbedtls_ssl_context * ssl = (mbedtls_ssl_context *) altcp_tls_context(pcb);

    if (ssl == NULL)
    {
        SILABS_LOG("[MATTER_HTTPS] TLS diagnostics: no ssl context");
        return;
    }

    const char * ciphersuite = mbedtls_ssl_get_ciphersuite(ssl);
    const char * version     = mbedtls_ssl_get_version(ssl);
    uint32_t verify_result   = mbedtls_ssl_get_verify_result(ssl);
    int cs_id                = mbedtls_ssl_get_ciphersuite_id_from_ssl(ssl);
#if defined(MBEDTLS_SSL_TLS_C)
    int ssl_state            = ssl->MBEDTLS_PRIVATE(state);
    const char * ssl_state_s = mbedtls_ssl_states_str((mbedtls_ssl_states) ssl_state);
#else
    int ssl_state            = -1;
    const char * ssl_state_s = "n/a";
#endif

    SILABS_LOG("[MATTER_HTTPS] TLS diagnostics: state=%d (%s) ciphersuite_id=0x%04x ciphersuite=%s version=%s verify=0x%lx",
               ssl_state, (ssl_state_s != NULL) ? ssl_state_s : "?", cs_id,
               (ciphersuite != NULL) ? ciphersuite : "none", (version != NULL) ? version : "none",
               (unsigned long) verify_result);
}

static void https_client_log_mbedtls_build_flags(void)
{
#if defined(MBEDTLS_DEBUG_C)
    SILABS_LOG("[MATTER_HTTPS] MBEDTLS_DEBUG_C=1");
#else
    SILABS_LOG("[MATTER_HTTPS] MBEDTLS_DEBUG_C=0 (no ssl_tls12_client debug strings)");
#endif
#if defined(MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED)
    SILABS_LOG("[MATTER_HTTPS] MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED=1");
#else
    SILABS_LOG("[MATTER_HTTPS] MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED=0");
#endif
#if defined(MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED)
    SILABS_LOG("[MATTER_HTTPS] MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED=1");
#else
    SILABS_LOG("[MATTER_HTTPS] MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED=0");
#endif
}

static void https_client_close(https_client_t * client)
{
    if (client == NULL)
    {
        return;
    }

    if (client->pcb != NULL)
    {
        altcp_arg(client->pcb, NULL);
        altcp_recv(client->pcb, NULL);
        altcp_sent(client->pcb, NULL);
        altcp_err(client->pcb, NULL);
        altcp_poll(client->pcb, NULL, 0);

        altcp_close(client->pcb);

        client->pcb = NULL;
    }

    mem_free(client);
}

static err_t https_client_connected(void * arg, struct altcp_pcb * pcb, err_t err)
{
    https_client_t * client = (https_client_t *) arg;

    if (client == NULL)
    {
        SILABS_LOG("[MATTER_HTTPS] connect callback: client is NULL");
        return ERR_VAL;
    }

    if (err != ERR_OK)
    {
        SILABS_LOG("[MATTER_HTTPS] TLS connect failed: lwip_err=%d", (int) err);
        return ERR_VAL;
    }

    SILABS_LOG("[MATTER_HTTPS] TLS connected, sending GET");

    err = altcp_write(pcb, client->request, strlen(client->request), TCP_WRITE_FLAG_COPY);

    if (err != ERR_OK)
    {
        SILABS_LOG("[MATTER_HTTPS] HTTP write failed: lwip_err=%d", (int) err);
        return err;
    }

    return altcp_output(pcb);
}

static err_t https_client_recv(void * arg, struct altcp_pcb * pcb, struct pbuf * p, err_t err)
{
    https_client_t * client = (https_client_t *) arg;

    if ((err != ERR_OK) || (client == NULL))
    {
        if (p != NULL)
        {
            pbuf_free(p);
        }

        if (client != NULL)
        {
            SILABS_LOG("[MATTER_HTTPS] recv error: lwip_err=%d rx=%u", (int) err, (unsigned) client->rx_bytes);
        }

        return err;
    }

    if (p == NULL)
    {
        SILABS_LOG("[MATTER_HTTPS] response complete: http_status=%d rx=%u",
                   client->http_status, (unsigned) client->rx_bytes);

        https_client_close(client);

        return ERR_OK;
    }

    altcp_recved(pcb, p->tot_len);

    client->rx_bytes += p->tot_len;

    if (client->http_status == 0)
    {
        https_client_try_parse_status(client, (const u8_t *) p->payload, p->len);
    }

    SILABS_LOG("[MATTER_HTTPS] HTTP RX %u bytes (total=%u)", (unsigned) p->tot_len, (unsigned) client->rx_bytes);

    pbuf_free(p);

    return ERR_OK;
}

static err_t https_client_sent(void * arg, struct altcp_pcb * pcb, u16_t len)
{
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(pcb);

    SILABS_LOG("[MATTER_HTTPS] HTTP sent %u bytes", (unsigned) len);

    return ERR_OK;
}

static void https_client_error(void * arg, err_t err)
{
    https_client_t * client = (https_client_t *) arg;

    if (client != NULL)
    {
        SILABS_LOG("[MATTER_HTTPS] altcp error: lwip_err=%d rx=%u http_status=%d", (int) err,
                   (unsigned) client->rx_bytes, client->http_status);

        if (client->pcb != NULL)
        {
            https_client_log_tls_diagnostics(client->pcb);
        }

        client->pcb = NULL;
        mem_free(client);
    }
    else
    {
        SILABS_LOG("[MATTER_HTTPS] altcp error: lwip_err=%d (no client)", (int) err);
    }
}

static err_t https_client_poll(void * arg, struct altcp_pcb * pcb)
{
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(pcb);

    return ERR_OK;
}

err_t https_client_get(const char * server_ip, uint16_t port, const char * host, const char * uri)
{
    ip_addr_t server_addr;
    https_client_t * client;
    err_t err;
    struct altcp_tls_config * tls_config;

    if ((server_ip == NULL) || (host == NULL) || (uri == NULL))
    {
        SILABS_LOG("[MATTER_HTTPS] https_client_get: invalid args");
        return ERR_ARG;
    }

    SILABS_LOG("[MATTER_HTTPS] starting GET https://%s:%u%s (host=%s)", server_ip, port, uri, host);
    https_client_log_mbedtls_build_flags();

    client = (https_client_t *) mem_malloc(sizeof(https_client_t));

    if (client == NULL)
    {
        SILABS_LOG("[MATTER_HTTPS] client alloc failed");
        return ERR_MEM;
    }

    memset(client, 0, sizeof(https_client_t));

#if MATTER_HTTPS_SKIP_CA_VERIFY
    tls_config = altcp_tls_create_config_client(NULL, 0);
    SILABS_LOG("[MATTER_HTTPS] TLS config: CA verify disabled");
#else
    tls_config = altcp_tls_create_config_client((const u8_t *) ca_cert_pem, sizeof(ca_cert_pem));
#endif

    if (tls_config == NULL)
    {
        SILABS_LOG("[MATTER_HTTPS] altcp_tls_create_config_client failed");

        mem_free(client);

        return ERR_MEM;
    }

    client->pcb = altcp_tls_new(tls_config, IPADDR_TYPE_V4);

    if (client->pcb == NULL)
    {
        SILABS_LOG("[MATTER_HTTPS] altcp_tls_new failed");

        altcp_tls_free_config(tls_config);

        mem_free(client);

        return ERR_MEM;
    }

    {
        mbedtls_ssl_context * ssl = (mbedtls_ssl_context *) altcp_tls_context(client->pcb);
        if (ssl != NULL)
        {
            const int hn_ret = mbedtls_ssl_set_hostname(ssl, host);
            if (hn_ret != 0)
            {
                char errbuf[96];
                mbedtls_strerror(hn_ret, errbuf, sizeof(errbuf));
                SILABS_LOG("[MATTER_HTTPS] mbedtls_ssl_set_hostname(%s) failed: %d %s", host, hn_ret, errbuf);
            }
            else
            {
                SILABS_LOG("[MATTER_HTTPS] TLS hostname set: %s", host);
            }
        }
    }

    if (!ipaddr_aton(server_ip, &server_addr))
    {
        SILABS_LOG("[MATTER_HTTPS] invalid server IP: %s", server_ip);

        https_client_close(client);

        altcp_tls_free_config(tls_config);

        return ERR_ARG;
    }

    snprintf(client->request, sizeof(client->request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             uri, host);

    altcp_arg(client->pcb, client);
    altcp_recv(client->pcb, https_client_recv);
    altcp_sent(client->pcb, https_client_sent);
    altcp_err(client->pcb, https_client_error);
    altcp_poll(client->pcb, https_client_poll, 2);

    err = altcp_connect(client->pcb, &server_addr, port, https_client_connected);

    if (err != ERR_OK)
    {
        SILABS_LOG("[MATTER_HTTPS] altcp_connect failed: lwip_err=%d", (int) err);

        https_client_close(client);

        altcp_tls_free_config(tls_config);

        return err;
    }

    SILABS_LOG("[MATTER_HTTPS] altcp_connect dispatched OK");

    return ERR_OK;
}
