/**
 * @file
 * @brief Matter abstraction layer for HTTPS client configuration.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef __MATTER_HTTPS_CONFIG_H
#define __MATTER_HTTPS_CONFIG_H

#include "cmsis_os2.h"

#include <stddef.h>
#include <stdint.h>

#ifndef MATTER_HTTPS_INIT_DELAY_SEC
#define MATTER_HTTPS_INIT_DELAY_SEC (10)
#endif

/* Set to 1 to skip CA load/verify (test only; insecure). */
#ifndef MATTER_HTTPS_SKIP_CA_VERIFY
#define MATTER_HTTPS_SKIP_CA_VERIFY (1)
#endif

/*
 * 0 (default): server is a literal IP string; uses httpc_get_file() + ipaddr_aton().
 * 1: server is a hostname (or IP); uses httpc_get_file_dns(). Requires LWIP_DNS=1 in the build.
 */
#ifndef MATTER_HTTPS_USE_DNS
#define MATTER_HTTPS_USE_DNS (0)
#endif

/* Task configuration (mirrors Matter AWS pattern) */
#define MATTER_HTTPS_TASK_NAME "MATTER_HTTPS"
#define MATTER_HTTPS_TASK_STACK_SIZE (1024)
#define MATTER_HTTPS_TASK_PRIORITY (osPriorityAboveNormal)

/* IP when MATTER_HTTPS_USE_DNS=0, hostname when MATTER_HTTPS_USE_DNS=1 */
#define MATTER_HTTPS_DEFAULT_SERVER "192.168.68.104"
#define MATTER_HTTPS_DEFAULT_URI "/"
#define MATTER_HTTPS_DEFAULT_PORT (8443)

/* Backward-compatible alias */
#define MATTER_HTTPS_DEFAULT_HOST MATTER_HTTPS_DEFAULT_SERVER

extern const char MATTER_HTTPS_DEFAULT_ROOT_CA[];
extern const size_t MATTER_HTTPS_DEFAULT_ROOT_CA_LEN;

#endif // __MATTER_HTTPS_CONFIG_H
