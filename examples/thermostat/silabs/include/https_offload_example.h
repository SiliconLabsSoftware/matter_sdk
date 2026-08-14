/***************************************************************************/
/**
 * @file https_offload_example.h
 * @brief NWP offload HTTPS client demo entry point
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#pragma once

#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Load config + start HTTPS offload demo on a dedicated thread (idempotent). */
sl_status_t http_client_demo_start(void);

#ifdef __cplusplus
}
#endif
