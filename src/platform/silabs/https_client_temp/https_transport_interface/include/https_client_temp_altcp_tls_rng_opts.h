/**
 * @file
 * @brief SHORT TERM dummy TLS RNG opts for HTTPS Client Temp altcp TLS.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef HTTPS_CLIENT_TEMP_ALTCP_TLS_RNG_OPTS_H
#define HTTPS_CLIENT_TEMP_ALTCP_TLS_RNG_OPTS_H

#if (defined(HTTPS_CLIENT_TEMP) && (HTTPS_CLIENT_TEMP == 1))

#include "https_client_temp_altcp_tls_rng.h"

#ifndef ALTCP_MBEDTLS_RNG_FN
#define ALTCP_MBEDTLS_RNG_FN https_client_temp_dummy_rng
#endif

#endif

#endif // HTTPS_CLIENT_TEMP_ALTCP_TLS_RNG_OPTS_H
