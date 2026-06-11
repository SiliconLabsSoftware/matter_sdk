/**
 * @file
 * @brief HTTPS Client Temp configuration.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef __HTTPS_CLIENT_TEMP_CONFIG_H
#define __HTTPS_CLIENT_TEMP_CONFIG_H

#include "cmsis_os2.h"

#include <stddef.h>
#include <stdint.h>

#ifndef SL_HTTPS_CLIENT_TEMP_NVM_EMBED_CERT
#define SL_HTTPS_CLIENT_TEMP_NVM_EMBED_CERT (0)
#endif

#ifndef HTTPS_CLIENT_TEMP_INIT_DELAY_SEC
#define HTTPS_CLIENT_TEMP_INIT_DELAY_SEC (10)
#endif

#ifndef HTTPS_CLIENT_TEMP_SKIP_CA_VERIFY
#define HTTPS_CLIENT_TEMP_SKIP_CA_VERIFY (1)
#endif

#ifndef HTTPS_CLIENT_TEMP_USE_DNS
#define HTTPS_CLIENT_TEMP_USE_DNS (0)
#endif

#define HTTPS_CLIENT_TEMP_TASK_NAME "HTTPS_CLIENT_TEMP"
#define HTTPS_CLIENT_TEMP_TASK_STACK_SIZE (2 * 1024)
#define HTTPS_CLIENT_TEMP_TASK_PRIORITY (osPriorityAboveNormal)

#define HTTPS_CLIENT_TEMP_MIN_HEAP_FOR_TLS (12 * 1024)
#define HTTPS_CLIENT_TEMP_MIN_HEAP_FOR_GET (8 * 1024)
#define HTTPS_CLIENT_TEMP_REQUEST_TIMEOUT_MS (30000)
#define HTTPS_CLIENT_TEMP_HTTP_POLL_MAX (30)

#define HTTPS_CLIENT_TEMP_DEFAULT_SERVER "192.168.50.129"
#define HTTPS_CLIENT_TEMP_DEFAULT_URI "/"
#define HTTPS_CLIENT_TEMP_DEFAULT_PORT (8443)
#define HTTPS_CLIENT_TEMP_DEFAULT_HOST HTTPS_CLIENT_TEMP_DEFAULT_SERVER

#define HTTPS_CLIENT_TEMP_CA_CERT_LENGTH (1400)
#define HTTPS_CLIENT_TEMP_HOSTNAME_LENGTH (55)

extern const char HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA[];
extern const size_t HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA_LEN;

#endif // __HTTPS_CLIENT_TEMP_CONFIG_H
