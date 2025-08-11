/*
 *    Copyright (c) 2025 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Utility functions for Silicon Labs platform error handling
 *          and common operations.
 */

#include <platform/silabs/SilabsConfigUtils.h>

#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceError.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

CHIP_ERROR MapNvm3Error(sl_status_t nvm3Res)
{
    CHIP_ERROR err;

    switch (nvm3Res)
    {
    case SL_STATUS_OK:
        err = CHIP_NO_ERROR;
        break;
    case SL_STATUS_NOT_FOUND:
        err = CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
        break;
    default:
        err = CHIP_ERROR(ChipError::Range::kPlatform, (nvm3Res & 0xFF) + CHIP_DEVICE_CONFIG_SILABS_NVM3_ERROR_MIN);
        break;
    }

    return err;
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
