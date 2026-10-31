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

#include "https_downloader.h"
#include "vendor_ota_matter_gate.h"
#include "vendor_ota_requestor_driver.h"

#include <app/clusters/ota-requestor/DefaultOTARequestorStorage.h>
#include <app/clusters/ota-requestor/OTARequestorInterface.h>
#include <platform/silabs/multi-ota/OTAMultiImageProcessorImpl.h>

namespace chip {

/**
 * Vendor HTTPS OTA controller. Installed as the global OTARequestorInterface while a
 * download/apply session is active so ImageProcessor CancelImageUpdate/Reset hit this object.
 * Requires multi-image OTA (OTAMultiImageProcessorImpl).
 */
class VendorOTARequestor : public OTARequestorInterface, public HttpsDownloader::StateDelegate
{
public:
    enum class LastResultReason : uint8_t
    {
        kOk = 0,
        kAborted,
        kVersionMismatch,
        kStall,
        kBusy,
        kHttp,
        kNoUrl,
        kClientStart,
    };

    /**
     * App/example owns URI composition and the periodic URL store.
     * Register via SetAppDelegate; pointer is not owned.
     */
    class AppDelegate : public HttpsDownloader::UriComposer
    {
    public:
        // CHIP_ERROR_NOT_FOUND when no periodic URL is configured.
        virtual CHIP_ERROR GetPeriodicUrl(MutableCharSpan & outUrl) = 0;
    };

    static VendorOTARequestor & GetInstance();

    void Init(HttpsDownloader & httpsDownloader, DeviceLayer::Silabs::MultiOTA::OTAMultiImageProcessorImpl & imageProcessor,
              OTARequestorInterface & matterRequestor, OTADownloader & bdxDownloader, DefaultOTARequestorStorage & matterStorage,
              VendorOtaMatterGate & gate, VendorOTARequestorDriver & vendorDriver);

    /** Cache app delegate and forward UriComposer to the downloader when ready. */
    void SetAppDelegate(AppDelegate * delegate);
    /** Cache HttpClient and forward to the downloader when ready. */
    void SetHttpClient(DeviceLayer::Silabs::HttpClient * client);

    /** Forwards to AppDelegate; CHIP_ERROR_NOT_FOUND if none / no URL. */
    CHIP_ERROR GetPeriodicUrl(MutableCharSpan & outUrl);

    /** Start a one-shot HTTPS OTA session. Does not write NVM. */
    CHIP_ERROR StartUpdate(CharSpan httpsUrl);

    void OnWiFiConnectivityChanged(bool established);

    void OnDownloadStateChanged(OTADownloader::State state,
                                app::Clusters::OtaSoftwareUpdateRequestor::OTAChangeReasonEnum reason) override;
    void OnUpdateProgressChanged(app::DataModel::Nullable<uint8_t> percent) override;

    void Reset() override;
    void CancelImageUpdate() override;
    OTAUpdateStateEnum GetCurrentUpdateState() override { return mUpdateState; }
    uint32_t GetTargetVersion() override { return mTargetVersion; }
    CHIP_ERROR GetUpdateStateAttribute(EndpointId endpointId, OTAUpdateStateEnum & state) override;
    CHIP_ERROR GetUpdateStateProgressAttribute(EndpointId endpointId, app::DataModel::Nullable<uint8_t> & progress) override;

    CHIP_ERROR GetLastResult(CHIP_ERROR & err, LastResultReason & reason) const;
    static const char * LastResultReasonToString(LastResultReason reason);

    HttpsDownloader * GetHttpsDownloader() const { return mHttpsDownloader; }
    DeviceLayer::Silabs::MultiOTA::OTAMultiImageProcessorImpl * GetImageProcessor() const { return mImageProcessor; }

    void HandleAnnounceOTAProvider(
        app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
        const app::Clusters::OtaSoftwareUpdateRequestor::Commands::AnnounceOTAProvider::DecodableType & commandData) override;
    void OnFabricRemoved(FabricIndex fabricIndex) override;

    CHIP_ERROR TriggerImmediateQuery(FabricIndex fabricIndex = kUndefinedFabricIndex) override;
    void TriggerImmediateQueryInternal() override;
    void DownloadUpdate() override;
    void DownloadUpdateDelayedOnUserConsent() override;
    void ApplyUpdate() override;
    void NotifyUpdateApplied() override;
    CHIP_ERROR ClearDefaultOtaProviderList(FabricIndex fabricIndex) override;
    void SetCurrentProviderLocation(ProviderLocationType providerLocation) override;
    void SetMetadataForProvider(ByteSpan metadataForProvider) override;
    void GetProviderLocation(Optional<ProviderLocationType> & providerLocation) override;
    CHIP_ERROR AddDefaultOtaProvider(const ProviderLocationType & providerLocation) override;
    ProviderLocationList::Iterator GetDefaultOTAProviderListIterator() override;

private:
    void ApplyBindings();
    CHIP_ERROR TakeOwnership();
    void RestoreMatterOwnership();
    bool IsMatterIdle() const;
    void RecordState(OTAUpdateStateEnum state, app::Clusters::OtaSoftwareUpdateRequestor::OTAChangeReasonEnum reason);
    CHIP_ERROR PersistApplyingForConfirm(uint32_t targetVersion);
    void SetLastResult(CHIP_ERROR err, LastResultReason reason);

    HttpsDownloader * mHttpsDownloader                                          = nullptr;
    DeviceLayer::Silabs::MultiOTA::OTAMultiImageProcessorImpl * mImageProcessor = nullptr;
    OTARequestorInterface * mMatterRequestor                                    = nullptr;
    OTADownloader * mBdxDownloader                                              = nullptr;
    DefaultOTARequestorStorage * mMatterStorage                                 = nullptr;
    VendorOtaMatterGate * mGate                                                 = nullptr;
    VendorOTARequestorDriver * mVendorDriver                                    = nullptr;
    AppDelegate * mAppDelegate                                                  = nullptr;
    DeviceLayer::Silabs::HttpClient * mHttpClient                               = nullptr;

    bool mActive                    = false;
    OTAUpdateStateEnum mUpdateState = OTAUpdateStateEnum::kIdle;
    uint32_t mTargetVersion         = 0;
    app::DataModel::Nullable<uint8_t> mProgress;
    ProviderLocationList mEmptyProviderList;

    CHIP_ERROR mLastResultError        = CHIP_NO_ERROR;
    LastResultReason mLastResultReason = LastResultReason::kOk;
};

} // namespace chip
