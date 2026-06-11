/**
 * @file
 * @brief SHORT TERM dummy TLS RNG for HTTPS Client Temp altcp TLS on SI917.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#include "https_client_temp_altcp_tls_rng.h"

int https_client_temp_dummy_rng(void * ctx, unsigned char * buffer, size_t len)
{
    static size_t ctr;
    size_t i;

    (void) ctx;

    for (i = 0; i < len; i++)
    {
        buffer[i] = (unsigned char) ++ctr;
    }

    return 0;
}
