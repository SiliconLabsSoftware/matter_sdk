/**
 * @file
 * @brief HTTPS Client Temp application bridge.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#include <lib/support/logging/CHIPLogging.h>

#include "HttpsClientTemp.h"
#include "HttpsClientTempControl.h"

namespace httpsClientTemp {
namespace control {

void readyCb(void)
{
    httpsClientTemp_request_t request = {};
    request.host                      = HTTPS_CLIENT_TEMP_DEFAULT_SERVER;
    request.uri                       = HTTPS_CLIENT_TEMP_DEFAULT_URI;
    request.port                      = HTTPS_CLIENT_TEMP_DEFAULT_PORT;

    httpsClientTemp_err_t err = HttpsClientTempRequestGet(&request);
    if (err != HTTPS_CLIENT_TEMP_OK)
    {
        ChipLogError(AppServer, "[HTTPS_CLIENT_TEMP] default GET failed: %d", static_cast<int>(err));
    }
}

} // namespace control
} // namespace httpsClientTemp
