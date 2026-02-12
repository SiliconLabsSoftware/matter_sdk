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

 #include "AppTaskImpl.h"
 #include <lib/core/CHIPError.h>
 #include <cstddef>
 #include <app/persistence/DeferredAttributePersistenceProvider.h>
 
 /**
  * @brief Custom AppTask implementation using CRTP pattern with Impl methods
  * 
  * This class demonstrates how to override AppTask methods using the CRTP system.
  * It implements private Impl methods that are called by the CRTP base class.
  * 
  * Note: We don't need to inherit from BaseApplication directly because
  * AppTaskImpl inherits from AppTask, which already inherits from BaseApplication.
  */
 class CustomAppTask : public AppTaskImpl<CustomAppTask>
 {
 public:
     /**
      * @brief Get singleton instance
      */
     static CustomAppTask & GetAppTask() { return sAppTask; }
 
 protected:
     // DeferredAttributePersistenceProvider pointer (needed for AppTaskMainImpl)
     // Note: This shadows the private member in AppTask, but we need our own
     // since the base class member is private and inaccessible
     chip::app::DeferredAttributePersistenceProvider * pDeferredAttributePersister = nullptr;
 
 private:
     /**
      * @brief Friend declaration to allow CRTP base to access private Impl methods
      */
     friend class AppTaskImpl<CustomAppTask>;
 
     /**
      * @brief Override AppInitImpl() with custom implementation
      * 
      * This method is called during application initialization.
      * It adds custom logging and implements the initialization logic.
      */
     CHIP_ERROR AppInitImpl();
 
     /**
      * @brief Override StartAppTaskImpl() to use CustomAppTask::AppTaskMain
      * 
      * This ensures that our custom AppTaskMain is called instead of the default.
      */
     CHIP_ERROR StartAppTaskImpl();
 
     /**
      * @brief Override AppTaskMainImpl() with custom implementation
      * 
      * This is the main task loop function.
      * It adds custom logging to make it easy to identify in logs.
      */
     static void AppTaskMainImpl(void * pvParameter);
 
     /**
      * @brief Custom callback for when a lighting action is initiated
      * 
      * This replaces the private AppTask::ActionInitiated callback.
      */
     static void ActionInitiated(LightingManager::Action_t aAction, int32_t aActor, uint8_t * aValue);
 
     /**
      * @brief Custom callback for when a lighting action is completed
      * 
      * This replaces the private AppTask::ActionCompleted callback.
      */
     static void ActionCompleted(LightingManager::Action_t aAction);
 
     /**
      * @brief Custom UpdateClusterState method
      * 
      * This replaces the private AppTask::UpdateClusterState method.
      * Updates the cluster state to match the current light state.
      */
     static void UpdateClusterState(intptr_t context);
 
     static CustomAppTask sAppTask;
 };
 