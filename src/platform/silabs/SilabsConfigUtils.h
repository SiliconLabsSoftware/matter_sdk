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

#pragma once

#include <lib/core/CHIPError.h>
#include <platform/silabs/CHIPDevicePlatformConfig.h>

#include <sl_status.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

/**
 * Maps a Silicon Labs NVM3/Token Manager status code to a CHIP error.
 *
 * @param[in] nvm3Res The Silicon Labs status code to map.
 * @return CHIP_ERROR corresponding to the status code.
 */
CHIP_ERROR MapNvm3Error(sl_status_t nvm3Res);

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
