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

 #include "CustomAppTask.h"
 #include "AppConfig.h"
 #include "AppEvent.h"
 #include "silabs_utils.h"
 
 #include "LEDWidget.h"
 #if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
 #include "RGBLEDWidget.h"
 #endif //(defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
 
 #include <app/persistence/AttributePersistenceProviderInstance.h>
 #include <app/persistence/DefaultAttributePersistenceProvider.h>
 #include <app/persistence/DeferredAttributePersistenceProvider.h>
 
 #include <app-common/zap-generated/attributes/Accessors.h>
 #include <app/clusters/on-off-server/on-off-server.h>
 #include <app/server/Server.h>
 #include <app/util/attribute-storage.h>
 #include <setup_payload/OnboardingCodesUtil.h>
 
 #include <assert.h>
 
 #include <platform/silabs/platformAbstraction/SilabsPlatform.h>
 
 #include <setup_payload/QRCodeSetupPayloadGenerator.h>
 #include <setup_payload/SetupPayload.h>
 
 #include <lib/support/CodeUtils.h>
 
 #include <lib/support/Span.h>
 #include <platform/CHIPDeviceLayer.h>
 #include <platform/silabs/tracing/SilabsTracingMacros.h>
 
 #ifdef SL_CATALOG_SIMPLE_LED_LED1_PRESENT
 #define LIGHT_LED 1
 #else
 #define LIGHT_LED 0
 #endif
 
 #define APP_FUNCTION_BUTTON 0
 #define APP_LIGHT_SWITCH 1
 
 using namespace chip;
 using namespace chip::app;
 using namespace chip::app::Clusters;
 using namespace ::chip::DeviceLayer;
 using namespace ::chip::DeviceLayer::Silabs;
 
 namespace {
 #if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
 RGBLEDWidget sLightLED; // Use RGBLEDWidget if RGB LED functionality is enabled
 #else
 LEDWidget sLightLED; // Use LEDWidget for basic LED functionality
 #endif
 
 // Array of attributes that will have their non-volatile storage deferred/delayed.
 // This is useful for attributes that change frequently over short periods of time, such as during transitions.
 // In this example, we defer the storage of the Level Control's CurrentLevel attribute and the Color Control's
 // CurrentHue and CurrentSaturation attributes for the LIGHT_ENDPOINT.
 DeferredAttribute gDeferredAttributeTable[] = {
     DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id)),
     DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id)),
     DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id))
 };
 } // namespace
 
 using namespace chip::TLV;
 using namespace ::chip::DeviceLayer;
 
 CustomAppTask CustomAppTask::sAppTask;
 
 CHIP_ERROR CustomAppTask::AppInitImpl()
 {
     // ============================================
     // CUSTOM IMPLEMENTATIONS
     // ============================================
     SILABS_LOG("╔══════════════════════════════════════════════════════════╗");
     SILABS_LOG("║  CUSTOM: CustomAppTask::AppInitImpl() - Using CRTP      ║");
     SILABS_LOG("╚══════════════════════════════════════════════════════════╝");
     SILABS_LOG("CUSTOM: This is a custom implementation of AppInitImpl()");
 
     CHIP_ERROR err = CHIP_NO_ERROR;
     chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(AppTask::ButtonEventHandler);
 
     err = LightMgr().Init();
     if (err != CHIP_NO_ERROR)
     {
         SILABS_LOG("LightMgr::Init() failed");
         appError(err);
     }
 
     // Set up lighting callbacks with our custom implementations
     // Note: ActionInitiated and ActionCompleted are private in AppTask, so we provide our own
     SILABS_LOG("CUSTOM: Setting up lighting callbacks (using custom implementations)");
     LightMgr().SetCallbacks(ActionInitiated, ActionCompleted);
 
     sLightLED.Init(LIGHT_LED);
     sLightLED.Set(LightMgr().IsLightOn());
     SILABS_TRACE_NAMED_INSTANT("LightOn", "Reboot");
 
     // Update the LCD with the Stored value. Show QR Code if not provisioned
 #ifdef DISPLAY_ENABLED
     GetLCD().WriteDemoUI(LightMgr().IsLightOn());
 #ifdef QR_CODE_ENABLED
 #ifdef SL_WIFI
     if (!ConnectivityMgr().IsWiFiStationProvisioned())
 #else
     if (!ConnectivityMgr().IsThreadProvisioned())
 #endif /* !SL_WIFI */
     {
         GetLCD().ShowQRCode(true);
     }
 #endif // QR_CODE_ENABLED
 #endif
 
     SILABS_LOG("CUSTOM: CustomAppTask::AppInitImpl() completed successfully");
     
     // InitCompleteCallback is inherited from BaseApplication through AppTask
     // We can call it directly since we're in a member function
     InitCompleteCallback(err);
     return err;
 }
 
 CHIP_ERROR CustomAppTask::StartAppTaskImpl()
 {
     SILABS_LOG("CUSTOM: CustomAppTask::StartAppTaskImpl() - Using custom AppTaskMain");
     // Call BaseApplication::StartAppTask with our custom AppTaskMainImpl function pointer
     // Note: We pass AppTaskMainImpl directly since it's a static method
     return BaseApplication::StartAppTask(AppTaskMainImpl);
 }
 
 void CustomAppTask::AppTaskMainImpl(void * pvParameter)
 {
     // ============================================
     // CUSTOM IMPLEMENTATION - AppTaskMain
     // ============================================
     SILABS_LOG("");
     SILABS_LOG("╔══════════════════════════════════════════════════════════╗");
     SILABS_LOG("║  CUSTOM: CustomAppTask::AppTaskMain() - Starting         ║");
     SILABS_LOG("╚══════════════════════════════════════════════════════════╝");
     SILABS_LOG("");
 
     AppEvent event;
     osMessageQueueId_t sAppEventQueue = *(static_cast<osMessageQueueId_t *>(pvParameter));
 
     // Initialization that needs to happen before the BaseInit is called here as the BaseApplication::Init() will call
     // the AppInit() after BaseInit.
 
     SILABS_LOG("CUSTOM: [Step 1/4] Setting up AttributePersistenceProvider");
 
     // Retrieve the existing AttributePersistenceProvider, which should already be created and initialized.
     // This provider is typically set up by the CodegenDataModelProviderInstance constructor,
     // which is called in InitMatter within MatterConfig.cpp.
     // We use this as the base provider for deferred attribute persistence.
     AttributePersistenceProvider * attributePersistence = GetAttributePersistenceProvider();
     VerifyOrDie(attributePersistence != nullptr);
 
     SILABS_LOG("CUSTOM: [Step 2/4] Creating DeferredAttributePersistenceProvider");
 
     //  The DeferredAttributePersistenceProvider will persist the attribute value in non-volatile memory
     //  once it remains constant for SL_MATTER_DEFERRED_ATTRIBUTE_STORE_DELAY_MS milliseconds.
     //  For all other attributes not listed in gDeferredAttributeTable, the default PersistenceProvider is used.
     sAppTask.pDeferredAttributePersister = new DeferredAttributePersistenceProvider(
         *attributePersistence, Span<DeferredAttribute>(gDeferredAttributeTable, MATTER_ARRAY_SIZE(gDeferredAttributeTable)),
         System::Clock::Milliseconds32(SL_MATTER_DEFERRED_ATTRIBUTE_STORE_DELAY_MS));
     VerifyOrDie(sAppTask.pDeferredAttributePersister != nullptr);
 
     app::SetAttributePersistenceProvider(sAppTask.pDeferredAttributePersister);
     SILABS_LOG("CUSTOM: [Step 2/4] DeferredAttributePersistenceProvider created successfully");
 
     SILABS_LOG("CUSTOM: [Step 3/4] Initializing CustomAppTask");
     CHIP_ERROR err = sAppTask.Init();
     if (err != CHIP_NO_ERROR)
     {
         SILABS_LOG("╔══════════════════════════════════════════════════════════╗");
         SILABS_LOG("║  CUSTOM ERROR: CustomAppTask.Init() FAILED               ║");
         SILABS_LOG("╚══════════════════════════════════════════════════════════╝");
         appError(err);
     }
 
 #if !(defined(CHIP_CONFIG_ENABLE_ICD_SERVER) && CHIP_CONFIG_ENABLE_ICD_SERVER)
     SILABS_LOG("CUSTOM: [Step 4/4] Starting status LED timer");
     sAppTask.StartStatusLEDTimer();
 #endif
 
     SILABS_LOG("");
     SILABS_LOG("╔══════════════════════════════════════════════════════════╗");
     SILABS_LOG("║  CUSTOM: CustomAppTask initialized successfully          ║");
     SILABS_LOG("║  CUSTOM: Entering main event loop                        ║");
     SILABS_LOG("╚══════════════════════════════════════════════════════════╝");
     SILABS_LOG("");
 
     while (true)
     {
         osStatus_t eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, osWaitForever);
         while (eventReceived == osOK)
         {
             sAppTask.DispatchEvent(&event);
             eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, 0);
         }
     }
 }
 
 void CustomAppTask::ActionInitiated(LightingManager::Action_t aAction, int32_t aActor, uint8_t * aValue)
 {
     SILABS_LOG("CUSTOM: ActionInitiated callback - Action: %d, Actor: %d", aAction, aActor);
 
     if (aAction == LightingManager::LEVEL_ACTION)
     {
         VerifyOrReturn(aValue != nullptr);
         sLightLED.SetLevel(*aValue);
         SILABS_LOG("CUSTOM: Set LED level to %d", *aValue);
     }
     else
     {
         // Action initiated, update the light led
         bool lightOn = aAction == LightingManager::ON_ACTION;
         SILABS_LOG("CUSTOM: Turning light %s", (lightOn) ? "On" : "Off");
 
         sLightLED.Set(lightOn);
 
 #ifdef DISPLAY_ENABLED
         sAppTask.GetLCD().WriteDemoUI(lightOn);
 #endif
 
         if (aActor == AppEvent::kEventType_Button)
         {
             // mSyncClusterToButtonAction is in BaseApplication (inherited through AppTask)
             sAppTask.mSyncClusterToButtonAction = true;
             SILABS_LOG("CUSTOM: Button action detected - cluster sync enabled");
         }
     }
 }
 
 void CustomAppTask::ActionCompleted(LightingManager::Action_t aAction)
 {
     SILABS_LOG("CUSTOM: ActionCompleted callback - Action: %d", aAction);
 
     // action has been completed on the light
     if (aAction == LightingManager::ON_ACTION)
     {
         SILABS_LOG("CUSTOM: Light ON - Action completed successfully");
     }
     else if (aAction == LightingManager::OFF_ACTION)
     {
         SILABS_LOG("CUSTOM: Light OFF - Action completed successfully");
     }
     else if (aAction == LightingManager::LEVEL_ACTION)
     {
         SILABS_LOG("CUSTOM: Light LEVEL change - Action completed successfully");
     }
 
     // Handle cluster sync if button action was used
     if (sAppTask.mSyncClusterToButtonAction)
     {
         SILABS_LOG("CUSTOM: Syncing cluster state after button action");
         chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(nullptr));
         sAppTask.mSyncClusterToButtonAction = false;
     }
 }
 
 void CustomAppTask::UpdateClusterState(intptr_t context)
 {
     SILABS_LOG("CUSTOM: UpdateClusterState - Syncing cluster state with current light state");
     
     uint8_t newValue = LightMgr().IsLightOn();
 
     // write the new on/off value
     Protocols::InteractionModel::Status status = OnOffServer::Instance().setOnOffValue(LIGHT_ENDPOINT, newValue, false);
 
     if (status != Protocols::InteractionModel::Status::Success)
     {
         SILABS_LOG("CUSTOM ERROR: updating on/off %x", to_underlying(status));
     }
     else
     {
         SILABS_LOG("CUSTOM: Cluster state updated successfully - Light is %s", (newValue ? "ON" : "OFF"));
     }
 }
 