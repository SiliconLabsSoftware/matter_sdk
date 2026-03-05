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

 #include "AppTask.h"
 #include "LightingManager.h"
 #include "CRTPHelpers.h"
 #include <lib/core/CHIPError.h>
 #include <type_traits>
 #include <cstddef>
 
 #if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
 #include "RGBLEDWidget.h"
 #endif //(defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
 
 /**
  * @brief CRTP Base class for AppTask that allows API overrides with signature validation
  * 
  * This class provides a CRTP (Curiously Recurring Template Pattern) interface that allows
  * users to override specific APIs while maintaining compile-time signature validation.
  * 
  * Usage:
  *   class MyAppTask : public AppTaskImpl<MyAppTask> {
  *   private:
  *       friend class AppTaskImpl<MyAppTask>;
  *       
  *       // Override only the Impl methods you need
  *       CHIP_ERROR AppInitImpl() { // custom implementation
  *           return CHIP_NO_ERROR;
  *       }
  *       void PostLightActionRequestImpl(int32_t aActor, LightingManager::Action_t aAction) { // custom
  *       }
  *   };
  * 
  * Note: Since AppTask inherits from BaseApplication, your derived class will also
  * inherit from BaseApplication through AppTask. The CRTP class provides the override mechanism.
  * 
  * @tparam Derived The derived class type (CRTP pattern)
  */
 template <typename Derived>
 class AppTaskImpl : public AppTask
 {
 public:
     // Forward all types from base class
     using Action_t = LightingManager::Action_t;
 
     /**
      * @brief Public AppInit() method - calls AppInitImpl()
      * 
      * Note: AppInit is private in AppTask, so there's no base AppInitImpl().
      * Derived MUST implement AppInitImpl(). The compiler will validate the signature.
      */
     CHIP_ERROR AppInit()
     {
         // AppInit is private in AppTask, so Derived must always implement AppInitImpl
         // Use CRTP_RETURN_DERIVED_ONLY since there's no base Impl to fall back to
         CRTP_RETURN_DERIVED_ONLY(AppTaskImpl, Derived, AppInit);
     }
 
     /**
      * @brief Public StartAppTask() method - calls StartAppTaskImpl()
      */
     CHIP_ERROR StartAppTask()
     {
         CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, StartAppTask);
     }
 
     /**
      * @brief Public PostLightActionRequest() method - calls PostLightActionRequestImpl()
      */
     void PostLightActionRequest(int32_t aActor, Action_t aAction)
     {
         CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, PostLightActionRequest, aActor, aAction);
     }
 
 #if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
     /**
      * @brief Public PostLightControlActionRequest() method - calls PostLightControlActionRequestImpl()
      */
     void PostLightControlActionRequest(int32_t aActor, Action_t aAction, RGBLEDWidget::ColorData_t * aValue)
     {
         CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, PostLightControlActionRequest, aActor, aAction, aValue);
     }
 #endif // (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
 
     /**
      * @brief Public static ButtonEventHandler() method - calls ButtonEventHandlerImpl()
      */
     static void ButtonEventHandler(uint8_t button, uint8_t btnAction)
     {
         CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, ButtonEventHandler, button, btnAction);
     }
 
     /**
      * @brief Public static AppTaskMain() method - calls AppTaskMainImpl()
      */
     static void AppTaskMain(void * pvParameter)
     {
         CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, AppTaskMain, pvParameter);
     }
 
 private:
     /**
      * @brief Default Impl methods - fallback implementations that call base AppTask
      * 
      * These are called if Derived doesn't override them.
      * Derived classes can override these private Impl methods.
      * 
      * Note: AppInitImpl is not provided because AppTask::AppInit() is private.
      * Derived MUST implement AppInitImpl().
      */
     friend Derived;
 
     CHIP_ERROR StartAppTaskImpl()
     {
         return AppTask::StartAppTask();
     }
 
     void PostLightActionRequestImpl(int32_t aActor, Action_t aAction)
     {
         AppTask::PostLightActionRequest(aActor, aAction);
     }
 
 #if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
     void PostLightControlActionRequestImpl(int32_t aActor, Action_t aAction, RGBLEDWidget::ColorData_t * aValue)
     {
         AppTask::PostLightControlActionRequest(aActor, aAction, aValue);
     }
 #endif // (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
 
     static void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction)
     {
         AppTask::ButtonEventHandler(button, btnAction);
     }
 
     static void AppTaskMainImpl(void * pvParameter)
     {
         AppTask::AppTaskMain(pvParameter);
     }
 };
 