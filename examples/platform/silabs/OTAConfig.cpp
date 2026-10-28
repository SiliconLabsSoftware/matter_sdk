/*
 *
 *    Copyright (c) 2021-2026 Project CHIP Authors
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

#include "OTAConfig.h"
#include "silabs_utils.h"
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

#ifndef SLI_SI91X_MCU_INTERFACE

#include "api/application_properties.h"

#if defined(SL_COMPONENT_CATALOG_PRESENT)
#include "sl_component_catalog.h"
#endif

// Only include app properties if the Gecko SDK component that does it automatically isn't present
#if !defined(SL_CATALOG_GECKO_BOOTLOADER_INTERFACE_PRESENT)
// Header used for building the image GBL file
#define APP_PROPERTIES_VERSION 1
#define APP_PROPERTIES_ID                                                                                                          \
    {                                                                                                                              \
        0                                                                                                                          \
    }

__attribute__((used)) ApplicationProperties_t sl_app_properties = {
    /// @brief Magic value indicating that this is an ApplicationProperties_t
    /// Must equal @ref APPLICATION_PROPERTIES_MAGIC
    .magic = APPLICATION_PROPERTIES_MAGIC,

    /// Version number of this struct
    .structVersion = APPLICATION_PROPERTIES_VERSION,

    /// Type of signature this application is signed with
    .signatureType = APPLICATION_SIGNATURE_NONE,

    /// Location of the signature. Typically a pointer to the end of application
    .signatureLocation = 0,

    /// Information about the application
    .app = {

      /// Bitfield representing type of application
      /// e.g. @ref APPLICATION_TYPE_BLUETOOTH_APP
      .type = APPLICATION_TYPE_THREAD,

      /// Version number for this application
      .version = APP_PROPERTIES_VERSION,

      /// Capabilities of this application
      .capabilities = 0,

      /// Unique ID (e.g. UUID/GUID) for the product this application is built for
      .productId = APP_PROPERTIES_ID,
    },

    /// Pointer to information about the certificate
    .cert = NULL,

    /// Pointer to Long Token Data Section
    .longTokenSectionAddress = NULL,
};
#endif // SL_CATALOG_GECKO_BOOTLOADER_INTERFACE_PRESENT
#endif // SLI_SI91X_MCU_INTERFACE

// Global OTA objects
chip::DefaultOTARequestor gRequestorCore;
chip::DefaultOTARequestorStorage gRequestorStorage;
chip::BDXDownloader gDownloader;

#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
chip::SilabsOTARequestorDriver gRequestorUser;
chip::HttpsDownloader gHttpsDownloader;
chip::VendorOTARequestorDriver gVendorDriver;
chip::VendorOtaMatterGate gVendorGate;
#else
chip::DeviceLayer::DefaultOTARequestorDriver gRequestorUser;
#endif

void OTAConfig::Init()
{
    // BaseApplication schedules this twice at boot (connectivity established and
    // kDnssdInitialized, each after kInitOTARequestorDelaySec) and again on every
    // later Wi-Fi/Thread reconnect. DNS-SD itself posts kDnssdInitialized only once
    // per boot (DiscoveryImplPlatform::InitImpl returns early once initialized).
    // Re-entry is not safe: SilabsOTARequestorDriver::Init calls Reset() whenever
    // the requestor is not idle, which would abort an in-progress BDX or HTTPS OTA.
    // AddEventHandler and StartTimer are idempotent; this guard exists for the Reset.
    static bool sInitialized = false;
    if (sInitialized)
    {
        return;
    }

    // Initialize and interconnect the Requestor and Image Processor objects -- START
    SetRequestorInstance(&gRequestorCore);

    gRequestorStorage.Init(chip::Server::GetInstance().GetPersistentStorage());
    TEMPORARY_RETURN_IGNORED gRequestorCore.Init(chip::Server::GetInstance(), gRequestorStorage, gRequestorUser, gDownloader,
                                                 chip::GetOTARequestorAttributes(), chip::GetDefaultOTARequestorEventGenerator());

    // Periodic query timeout must be set prior to requestor being initialized
    gRequestorUser.SetPeriodicQueryTimeout(OTA_PERIODIC_TIMEOUT);

#if defined(SL_MATTER_ENABLE_MULTI_OTA_REQUESTOR) && SL_MATTER_ENABLE_MULTI_OTA_REQUESTOR
    auto & imageProcessor = chip::DeviceLayer::Silabs::MultiOTA::OTAMultiImageProcessorImpl::GetDefaultInstance();
#else
    auto & imageProcessor = chip::OTAImageProcessorImpl::GetDefaultInstance();
#endif

    gRequestorUser.Init(&gRequestorCore, &imageProcessor);

    CHIP_ERROR err = imageProcessor.Init(&gDownloader);
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("Image processor init failed");
        assert(err == CHIP_NO_ERROR);
    }

    // Connect the Downloader and Image Processor objects
    gDownloader.SetImageProcessorDelegate(&imageProcessor);

#if defined(SL_MATTER_ENABLE_HTTPS_OTA) && SL_MATTER_ENABLE_HTTPS_OTA
#if !(defined(SL_MATTER_ENABLE_MULTI_OTA_REQUESTOR) && SL_MATTER_ENABLE_MULTI_OTA_REQUESTOR)
#error "SL_MATTER_ENABLE_HTTPS_OTA requires SL_MATTER_ENABLE_MULTI_OTA_REQUESTOR"
#endif
    gVendorGate.Init(gRequestorUser);
    gVendorDriver.SetPeriodicCheckTimeout(SL_MATTER_HTTPS_OTA_PERIODIC_TIMEOUT);

    chip::VendorOTARequestor::GetInstance().Init(gHttpsDownloader, imageProcessor, gRequestorCore, gDownloader, gRequestorStorage,
                                                 gVendorGate, gVendorDriver);
    // Starts periodic timer + registers connectivity event handler.
    gVendorDriver.Init(&chip::VendorOTARequestor::GetInstance());
#endif
    sInitialized = true;
    // Initialize and interconnect the Requestor and Image Processor objects -- END
}
