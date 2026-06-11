/**
 * @file
 * @brief HTTPS Client Temp credential storage.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#include <cstring>
#include <stddef.h>
#include <stdint.h>

#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/silabs/SilabsConfig.h>

#include "HttpsClientTempConfig.h"
#include "HttpsClientTempNvmCert.h"

char https_client_temp_ca_certificate[] = "";

using namespace chip::DeviceLayer::Internal;

CHIP_ERROR HttpsClientTempGetRootCA(char * buf, size_t buf_len, size_t * bufSize)
{
    CHIP_ERROR status = CHIP_NO_ERROR;
#if SL_HTTPS_CLIENT_TEMP_NVM_EMBED_CERT
    status =
        SilabsConfig::ReadConfigValueBin(SilabsConfig::kConfigKey_CACerts, reinterpret_cast<uint8_t *>(buf), buf_len, *bufSize);
#else
    if (https_client_temp_ca_certificate[0] != '\0')
    {
        *bufSize = strlen(https_client_temp_ca_certificate) + 1;
        if (buf == NULL || buf_len < *bufSize)
        {
            return CHIP_ERROR_INTERNAL;
        }
        strncpy(buf, https_client_temp_ca_certificate, buf_len);
    }
    else
    {
        *bufSize = HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA_LEN;
        if (buf == NULL || buf_len < *bufSize)
        {
            return CHIP_ERROR_INTERNAL;
        }
        strncpy(buf, HTTPS_CLIENT_TEMP_DEFAULT_ROOT_CA, buf_len);
    }
#endif
    return status;
}

CHIP_ERROR HttpsClientTempGetHostname(char * buf, size_t buf_len, size_t * bufSize)
{
    CHIP_ERROR status = CHIP_NO_ERROR;
#if SL_HTTPS_CLIENT_TEMP_NVM_EMBED_CERT
    status =
        SilabsConfig::ReadConfigValueBin(SilabsConfig::kConfigKey_hostname, reinterpret_cast<uint8_t *>(buf), buf_len, *bufSize);
#else
    *bufSize = strlen(HTTPS_CLIENT_TEMP_DEFAULT_SERVER) + 1;
    if (buf == NULL || buf_len < *bufSize)
    {
        return CHIP_ERROR_INTERNAL;
    }
    strncpy(buf, HTTPS_CLIENT_TEMP_DEFAULT_SERVER, buf_len);
#endif
    return status;
}
