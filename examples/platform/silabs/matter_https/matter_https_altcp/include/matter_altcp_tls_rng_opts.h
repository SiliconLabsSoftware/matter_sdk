/**
 * @file
 * @brief SHORT TERM shared dummy TLS RNG for Matter altcp (AWS + HTTPS).
 *
 * REMOVE THIS FILE when altcp TLS uses a real entropy source on SI917.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef MATTER_ALTCP_TLS_RNG_OPTS_H
#define MATTER_ALTCP_TLS_RNG_OPTS_H

#if ((defined(SL_MATTER_ENABLE_AWS) && (SL_MATTER_ENABLE_AWS == 1)) \
     || (defined(SL_MATTER_ENABLE_HTTPS_CLIENT) && (SL_MATTER_ENABLE_HTTPS_CLIENT == 1)))

#include "matter_https_altcp_tls_rng.h"

#ifndef ALTCP_MBEDTLS_RNG_FN
#define ALTCP_MBEDTLS_RNG_FN matter_https_altcp_dummy_rng
#endif

#endif

#endif // MATTER_ALTCP_TLS_RNG_OPTS_H
