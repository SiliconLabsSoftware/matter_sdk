
/**
 * @file
 * @brief HTTPS Client Temp credential accessors.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef _HTTPS_CLIENT_TEMP_NVM_CERT_H
#define _HTTPS_CLIENT_TEMP_NVM_CERT_H

#include <lib/core/CHIPError.h>

CHIP_ERROR HttpsClientTempGetRootCA(char * buf, size_t buf_len, size_t * bufSize);

CHIP_ERROR HttpsClientTempGetHostname(char * buf, size_t buf_len, size_t * bufSize);

#endif // _HTTPS_CLIENT_TEMP_NVM_CERT_H
