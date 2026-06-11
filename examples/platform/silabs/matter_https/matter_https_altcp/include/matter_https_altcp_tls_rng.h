/**
 * @file
 * @brief SHORT TERM dummy TLS RNG for lwIP altcp TLS on SI917.
 *
 * REMOVE THIS FILE when altcp TLS is wired to a real entropy source
 * (SI91x TRNG / PSA random + mbedtls_entropy_add_source).
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef MATTER_HTTPS_ALTCP_TLS_RNG_H
#define MATTER_HTTPS_ALTCP_TLS_RNG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SHORT TERM: Insecure deterministic RNG for mbedtls_ctr_drbg_seed().
 * Do not use in production. Remove once real entropy is available.
 */
int matter_https_altcp_dummy_rng(void * ctx, unsigned char * buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif // MATTER_HTTPS_ALTCP_TLS_RNG_H
