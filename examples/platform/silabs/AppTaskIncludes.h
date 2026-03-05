/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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
  * @file AppTaskInclude.h
  * @brief Configuration header to select between default AppTask and CustomAppTask
  * 
  * This header provides a unified interface to use either the default AppTask
  * or CustomAppTask based on configuration, without hardcoding in MatterConfig.cpp
  */
 
 #include "AppConfig.h"
 
 // SL_MATTER_USE_CUSTOM_APPTASK is defined in sl_matter_config.h
 // This header just uses that configuration to select the appropriate AppTask implementation
 
 #if defined (SL_MATTER_USE_CUSTOM_APPTASK) && SL_MATTER_USE_CUSTOM_APPTASK == 1
     // Use CustomAppTask implementation
     #include "CustomAppTask.h"
     using AppTaskType = CustomAppTask;
 #else
     // Use default AppTask implementation
     #include "AppTask.h"
     using AppTaskType = AppTask;
 #endif
 
 // Helper function to get the AppTask instance
 inline AppTaskType & GetAppTaskInstance()
 {
     return AppTaskType::GetAppTask();
 }
 