/**
 * @file
 * @brief SHORT TERM dummy TLS RNG for lwIP altcp TLS on SI917.
 *
 * SI917 builds use MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES and altcp TLS creates a
 * fresh mbedtls_entropy_context with no registered sources, so
 * mbedtls_ctr_drbg_seed() fails with MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED (-52).
 *
 * This mirrors the matter_aws_dummy_rng workaround used by matter_aws_transport_lwip.
 *
 * REMOVE THIS FILE and the ALTCP_MBEDTLS_RNG_FN override in
 * matter_https_client_altcp.slcc once altcp TLS uses a real entropy source.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#include "matter_https_altcp_tls_rng.h"

int matter_https_altcp_dummy_rng(void * ctx, unsigned char * buffer, size_t len)
{
    static size_t ctr;
    size_t i;

    (void) ctx;

    /* SHORT TERM: deterministic bytes only -- NOT cryptographically secure. */
    for (i = 0; i < len; i++)
    {
        buffer[i] = (unsigned char) ++ctr;
    }

    return 0;
}
