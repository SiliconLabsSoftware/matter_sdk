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

#include <cstdio>

#define CAMERA_HOST_LOG(stream, level, fmt, ...) std::fprintf(stream, "[camera_host][" level "] " fmt "\n", ##__VA_ARGS__)

#define LogInfo(fmt, ...) CAMERA_HOST_LOG(stdout, "info", fmt, ##__VA_ARGS__)
#define LogError(fmt, ...) CAMERA_HOST_LOG(stderr, "error", fmt, ##__VA_ARGS__)
