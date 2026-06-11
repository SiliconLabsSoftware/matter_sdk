/**
 * @file
 * @brief TLS config holder for HTTPS Client Temp forked altcp stack.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#include "HTTPS_transport.h"

#include "https_client_temp_altcp_tls.h"

#include "FreeRTOS.h"
#include "task.h"

#include "silabs_utils.h"

#include <string.h>

struct HTTPS_Transport_t
{
    struct altcp_tls_config * tls_config;
    EventGroupHandle_t events;
    bool initialized;
};

HTTPS_Transport_t * HTTPS_Transport_Init(EventGroupHandle_t events)
{
    if (events == NULL)
    {
        return NULL;
    }

    HTTPS_Transport_t * client = (HTTPS_Transport_t *) pvPortMalloc(sizeof(HTTPS_Transport_t));
    if (client == NULL)
    {
        return NULL;
    }

    memset(client, 0, sizeof(HTTPS_Transport_t));
    client->events = events;
    return client;
}

err_t HTTPS_Transport_SSLConfigure(HTTPS_Transport_t * trans, const u8_t * ca, size_t ca_len)
{
    if (trans == NULL)
    {
        return ERR_ARG;
    }

    if (trans->tls_config != NULL)
    {
        https_client_temp_altcp_tls_free_config(trans->tls_config);
        trans->tls_config = NULL;
    }

    trans->tls_config = https_client_temp_altcp_tls_create_config_client(ca, ca_len);
    if (trans->tls_config == NULL)
    {
        SILABS_LOG("[HTTPS_CLIENT_TEMP] altcp_tls_create_config_client failed (ca_len=%u heap_free=%u heap_min=%u)",
                   (unsigned) ca_len, (unsigned) xPortGetFreeHeapSize(),
                   (unsigned) xPortGetMinimumEverFreeHeapSize());
        return ERR_MEM;
    }

    trans->initialized = true;
    return ERR_OK;
}

altcp_tls_config * HTTPS_Transport_GetTlsConfig(HTTPS_Transport_t * trans)
{
    if (trans == NULL || !trans->initialized)
    {
        return NULL;
    }
    return trans->tls_config;
}

bool HTTPS_Transport_IsInitialized(HTTPS_Transport_t * trans)
{
    return (trans != NULL && trans->initialized && trans->tls_config != NULL);
}

void HTTPS_Transport_Deinit(HTTPS_Transport_t * trans)
{
    if (trans == NULL)
    {
        return;
    }

    if (trans->tls_config != NULL)
    {
        https_client_temp_altcp_tls_free_config(trans->tls_config);
        trans->tls_config = NULL;
    }

    trans->initialized = false;
    vPortFree(trans);
}
