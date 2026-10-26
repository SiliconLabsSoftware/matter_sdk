/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

#pragma once

/**
 * Lab-only connect-IP override for HTTPS OTA (skips DNS).
 *
 * This header is compiled from the SDK tree (not an SLC config_file copy), so
 * edits here are picked up on rebuild. Leave empty for production / real DNS.
 *
 * Prefer setting HTTPS_OTA_CONNECT_IP via compiler -D or sl_matter_ota_config.h
 * when that config is actually on the include path for this TU.
 */
#ifndef HTTPS_OTA_CONNECT_IP
#define HTTPS_OTA_CONNECT_IP "192.168.29.150"
#endif
