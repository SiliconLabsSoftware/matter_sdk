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

#include <app/clusters/camera-av-settings-user-level-management-server/CameraAvSettingsUserLevelManagementCluster.h>
#include <app/clusters/camera-av-stream-management-server/CameraAVStreamManagementCluster.h>
#include <app/clusters/webrtc-transport-provider-server/WebRTCTransportProviderCluster.h>
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <data-model-providers/codedriven/CodeDrivenDataModelProvider.h>
#include <device/api/SingleEndpoint.h>
#include <system/SystemLayer.h>

#include <map>
#include <vector>

namespace chip {
namespace app {

/**
 * Camera device type (0x0142) for an SoC acting as the Matter front-end of a Linux camera host.
 *
 * The SoC owns the data model: attribute storage, stream/session/preset bookkeeping and command
 * validation run locally. Everything driving the camera (stream start/stop, snapshots, PTZ,
 * audio/video controls) is forwarded to the host over MMIC, either as a cluster command or as an
 * attribute change notification. Commands the camera may reject wait for the host answer, whose
 * status is returned to the Matter client. The WebRTC signaling produced by the host (Offer,
 * Answer, ICECandidates, End) is relayed to the session requestor. The TLV payload of each
 * message is documented in ForwardingCamera.cpp.
 */
class ForwardingCamera : public SingleEndpoint,
                         public Clusters::CameraAvStreamManagement::CameraAVStreamManagementDelegate,
                         public Clusters::WebRTCTransportProvider::Delegate
{
    using VideoStreamStruct     = Clusters::CameraAvStreamManagement::VideoStreamStruct;
    using AudioStreamStruct     = Clusters::CameraAvStreamManagement::AudioStreamStruct;
    using VideoResolutionStruct = Clusters::CameraAvStreamManagement::VideoResolutionStruct;
    using ViewportStruct        = Clusters::Globals::Structs::ViewportStruct::Type;
    using WebRTCSessionStruct   = Clusters::WebRTCTransportProvider::WebRTCSessionStruct;
    using ICECandidateStruct    = Clusters::WebRTCTransportProvider::ICECandidateStruct;
    using WebRTCEndReasonEnum   = Clusters::WebRTCTransportProvider::WebRTCEndReasonEnum;
    using StreamUsageEnum       = Clusters::Globals::StreamUsageEnum;
    using PhysicalPTZCallback   = Clusters::CameraAvSettingsUserLevelManagement::PhysicalPTZCallback;

public:
    ForwardingCamera();
    ~ForwardingCamera() override = default;

    CHIP_ERROR Register(EndpointId endpoint, CodeDrivenDataModelProvider & provider, EndpointComposition composition = {}) override;
    void Unregister(CodeDrivenDataModelProvider & provider) override;
    CHIP_ERROR ClientClusters(ReadOnlyBufferBuilder<ClusterId> & out) const override;

    /// Applies attribute defaults and loads persisted stream allocations, which are re-sent to the host.
    /// Must be called once the data model provider has started the clusters (i.e. after Server::Init())
    /// and after the MMIC link has been initialized.
    CHIP_ERROR InitClusters();

    // CameraAVStreamManagementDelegate
    Protocols::InteractionModel::Status VideoStreamAllocate(const VideoStreamStruct & allocateArgs,
                                                            uint16_t & outStreamID) override;
    void OnVideoStreamAllocated(const VideoStreamStruct & allocatedStream,
                                Clusters::CameraAvStreamManagement::StreamAllocationAction action) override;
    Protocols::InteractionModel::Status VideoStreamModify(const uint16_t streamID, const Optional<bool> waterMarkEnabled,
                                                          const Optional<bool> osdEnabled) override;
    Protocols::InteractionModel::Status VideoStreamDeallocate(const uint16_t streamID) override;
    Protocols::InteractionModel::Status AudioStreamAllocate(const AudioStreamStruct & allocateArgs,
                                                            uint16_t & outStreamID) override;
    Protocols::InteractionModel::Status AudioStreamDeallocate(const uint16_t streamID) override;
    Protocols::InteractionModel::Status SnapshotStreamAllocate(const SnapshotStreamAllocateArgs & allocateArgs,
                                                               uint16_t & outStreamID) override;
    Protocols::InteractionModel::Status SnapshotStreamModify(const uint16_t streamID, const Optional<bool> waterMarkEnabled,
                                                             const Optional<bool> osdEnabled) override;
    Protocols::InteractionModel::Status SnapshotStreamDeallocate(const uint16_t streamID) override;
    void OnStreamUsagePrioritiesChanged() override;
    void OnAttributeChanged(AttributeId attributeId) override;
    Protocols::InteractionModel::Status
    CaptureSnapshot(const DataModel::Nullable<uint16_t> streamID, const VideoResolutionStruct & resolution,
                    Clusters::CameraAvStreamManagement::ImageSnapshot & outImageSnapshot) override;
    CHIP_ERROR PersistentAttributesLoadedCallback() override;
    const std::vector<VideoStreamStruct> & GetAllocatedVideoStreams() const override;
    const std::vector<AudioStreamStruct> & GetAllocatedAudioStreams() const override;

    // WebRTCTransportProvider::Delegate
    CHIP_ERROR HandleSolicitOffer(const OfferRequestArgs & args, WebRTCSessionStruct & outSession,
                                  bool & outDeferredOffer) override;
    CHIP_ERROR HandleProvideOffer(const ProvideOfferRequestArgs & args, WebRTCSessionStruct & outSession) override;
    CHIP_ERROR HandleProvideAnswer(uint16_t sessionId, const std::string & sdpAnswer) override;
    CHIP_ERROR HandleProvideICECandidates(uint16_t sessionId, const std::vector<ICECandidateStruct> & candidates) override;
    CHIP_ERROR HandleEndSession(uint16_t sessionId, WebRTCEndReasonEnum reasonCode) override;
    CHIP_ERROR ValidateStreamUsage(StreamUsageEnum streamUsage, Optional<std::vector<uint16_t>> & videoStreams,
                                   Optional<std::vector<uint16_t>> & audioStreams) override;
    CHIP_ERROR ValidateVideoStreamID(uint16_t videoStreamId) override;
    CHIP_ERROR ValidateAudioStreamID(uint16_t audioStreamId) override;
    CHIP_ERROR ValidateVideoStreams(const std::vector<uint16_t> & videoStreams) override;
    CHIP_ERROR ValidateAudioStreams(const std::vector<uint16_t> & audioStreams) override;
    CHIP_ERROR IsStreamUsageSupported(StreamUsageEnum streamUsage) override;
    CHIP_ERROR IsHardPrivacyModeActive(bool & isActive) override;
    CHIP_ERROR IsSoftRecordingPrivacyModeActive(bool & isActive) override;
    CHIP_ERROR IsSoftLivestreamPrivacyModeActive(bool & isActive) override;
    bool HasAllocatedVideoStreams() override;
    bool HasAllocatedAudioStreams() override;
    CHIP_ERROR ValidateSFrameConfig(uint16_t cipherSuite, size_t baseKeyLength) override;
    CHIP_ERROR IsUTCTimeNull(bool & isNull) override;

private:
    /// CameraAvSettingsUserLevelManagement delegate (mechanical and digital PTZ).
    class AvSettingsDelegate : public Clusters::CameraAvSettingsUserLevelManagementDelegate
    {
    public:
        explicit AvSettingsDelegate(ForwardingCamera & camera) : mCamera(camera) {}

        void ShutdownApp() override;
        bool CanChangeMPTZ() override;
        void VideoStreamAllocated(uint16_t aStreamID) override;
        void VideoStreamDeallocated(uint16_t aStreamID) override;
        void DefaultViewportUpdated(ViewportStruct aViewport) override;
        Protocols::InteractionModel::Status MPTZSetPosition(Optional<int16_t> aPan, Optional<int16_t> aTilt,
                                                            Optional<uint8_t> aZoom, PhysicalPTZCallback * callback) override;
        Protocols::InteractionModel::Status MPTZRelativeMove(Optional<int16_t> aPan, Optional<int16_t> aTilt,
                                                             Optional<uint8_t> aZoom, PhysicalPTZCallback * callback) override;
        Protocols::InteractionModel::Status MPTZMoveToPreset(uint8_t aPreset, Optional<int16_t> aPan, Optional<int16_t> aTilt,
                                                             Optional<uint8_t> aZoom, PhysicalPTZCallback * callback) override;
        Protocols::InteractionModel::Status MPTZSavePreset(uint8_t aPreset) override;
        Protocols::InteractionModel::Status MPTZRemovePreset(uint8_t aPreset) override;
        Protocols::InteractionModel::Status DPTZSetViewport(uint16_t aVideoStreamID, ViewportStruct aViewport) override;
        Protocols::InteractionModel::Status DPTZRelativeMove(uint16_t aVideoStreamID, Optional<int16_t> aDeltaX,
                                                             Optional<int16_t> aDeltaY, Optional<int8_t> aZoomDelta,
                                                             ViewportStruct & aViewport) override;
        CHIP_ERROR PersistentAttributesLoadedCallback() override;

    private:
        Protocols::InteractionModel::Status ForwardPhysicalMove(CommandId commandId, Optional<int16_t> aPan,
                                                                Optional<int16_t> aTilt, Optional<uint8_t> aZoom,
                                                                PhysicalPTZCallback * callback);
        static void OnPhysicalMoveTimer(System::Layer * layer, void * context);

        ForwardingCamera & mCamera;
        PhysicalPTZCallback * mPtzCallback = nullptr;
    };

    // A WebRTC session: storage backing the non-owning stream lists of its WebRTCSessionStruct,
    // and the requestor the host signaling (Offer, Answer, ICECandidates, End) is relayed to.
    struct SessionInfo
    {
        std::vector<uint16_t> videoStreams;
        std::vector<uint16_t> audioStreams;
        NodeId peerNodeId         = kUndefinedNodeId;
        FabricIndex fabricIndex   = kUndefinedFabricIndex;
        EndpointId peerEndpointId = kInvalidEndpointId;
    };

    void FillSession(const OfferRequestArgs & args, WebRTCSessionStruct & outSession);
    void ReleaseSessionStreams(uint16_t sessionId);
    void ForwardAttributeChange(AttributeId attributeId);

    // Commands sent by the host, relayed to the WebRTC requestor of a session.
    static uint8_t HandleHostCommand(uint16_t endpointId, uint32_t clusterId, uint32_t commandId, const uint8_t * params,
                                     uint16_t paramsLen);
    static void RunHostCommand(intptr_t context);
    void RelayToRequestor(CommandId commandId, std::vector<uint8_t> && params);

    static ForwardingCamera * sInstance;

    LazyRegisteredServerCluster<Clusters::CameraAvStreamManagement::CameraAVStreamManagementCluster> mAvStreamManagementCluster;
    LazyRegisteredServerCluster<Clusters::WebRTCTransportProvider::WebRTCTransportProviderCluster> mWebRTCTransportProviderCluster;
    LazyRegisteredServerCluster<Clusters::CameraAvSettingsUserLevelManagementCluster> mAvSettingsCluster;
    AvSettingsDelegate mAvSettingsDelegate{ *this };
    std::map<uint16_t, SessionInfo> mSessionStreams;
};

} // namespace app
} // namespace chip
