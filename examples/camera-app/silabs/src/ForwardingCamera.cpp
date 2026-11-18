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

#include "ForwardingCamera.h"

#include "mmic_matter_forward.h"

#include <app/CASESessionManager.h>
#include <app/data-model/Decode.h>
#include <app/server/Server.h>
#include <clusters/WebRTCTransportRequestor/ClusterId.h>
#include <clusters/WebRTCTransportRequestor/Commands.h>
#include <controller/InvokeInteraction.h>
#include <devices/Types.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ScopedMemoryBuffer.h>
#include <lib/support/TypeTraits.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <optional>

/*
 * Messages exchanged with the host over MMIC
 *
 * The SoC validates every command and keeps the attribute storage, then forwards anything driving
 * the camera to the host as a cluster_command. Parameters are encoded as anonymous Matter TLV
 * elements. "Blocking" commands wait for the host answer: its status is returned to the Matter
 * client (Failure when the host does not answer within kHostAnswerTimeout).
 *
 * CameraAvStreamManagement (SoC -> host)
 *   VideoStreamAllocate       Blocking. VideoStreamStruct with the allocated ID. The host validates
 *                             and starts the stream. Sent again without blocking when the stream is
 *                             modified, and for each persisted stream at boot.
 *   VideoStreamModify         Blocking. VideoStreamModify request
 *   VideoStreamDeallocate     Blocking. VideoStreamDeallocate request
 *   AudioStreamAllocate       Blocking. AudioStreamStruct with the allocated ID (also sent without
 *                             blocking for persisted streams at boot)
 *   AudioStreamDeallocate     Blocking. AudioStreamDeallocate request
 *   SnapshotStreamAllocate    Blocking. SnapshotStreamStruct with the allocated ID (also sent without
 *                             blocking for persisted streams at boot)
 *   SnapshotStreamModify      Blocking. SnapshotStreamModify request
 *   SnapshotStreamDeallocate  Blocking. SnapshotStreamDeallocate request
 *   SetStreamPriorities       SetStreamPriorities request
 *   CaptureSnapshot           Blocking. CaptureSnapshot request. On success the host answer carries a
 *                             CaptureSnapshotResponse TLV structure.
 *   Attribute changes         attribute_changed with the new value (no value for unhandled attributes)
 *
 * WebRTCTransportProvider (SoC -> host)
 *   SolicitOffer              Blocking. WebRTCSessionStruct (session created on the SoC), then the
 *                             SolicitOffer request
 *   ProvideOffer              Blocking. WebRTCSessionStruct, then the ProvideOffer request
 *                             (webRTCSessionID set)
 *   ProvideAnswer             Blocking. ProvideAnswer request
 *   ProvideICECandidates      Blocking. ProvideICECandidates request
 *   EndSession                Blocking. EndSession request
 *
 * CameraAvSettingsUserLevelManagement (SoC -> host)
 *   MPTZSetPosition           Blocking. MPTZStruct with the target position
 *   MPTZRelativeMove          Blocking. MPTZStruct with the resolved target position
 *   MPTZMoveToPreset          Blocking. MPTZMoveToPreset request, then MPTZStruct with the preset position
 *   MPTZSavePreset            Blocking. MPTZRemovePreset-like structure { presetID }
 *   MPTZRemovePreset          Blocking. MPTZRemovePreset request
 *   DPTZSetViewport           Blocking. DPTZSetViewport request
 *   DPTZRelativeMove          Blocking. DPTZStruct with the resolved viewport
 *
 * WebRTCTransportRequestor (host -> SoC)
 *   Offer, Answer,            Request structure of the WebRTCTransportRequestor command (only
 *   ICECandidates, End        webRTCSessionID and the mandatory fields are used). The SoC sends the
 *                             command to the requestor of the session. End also ends the session
 *                             on the SoC.
 */

using namespace chip::app::Clusters;
using namespace chip::app::Clusters::CameraAvStreamManagement;
using chip::Protocols::InteractionModel::Status;

namespace chip {
namespace app {

namespace {

namespace Avsm      = Clusters::CameraAvStreamManagement;
namespace Avsum     = Clusters::CameraAvSettingsUserLevelManagement;
namespace WebRtc    = Clusters::WebRTCTransportProvider;
namespace Requestor = Clusters::WebRTCTransportRequestor;

// Advertised capabilities of the camera driven by the host.
constexpr uint16_t kSensorWidth          = 1920;
constexpr uint16_t kSensorHeight         = 1080;
constexpr uint16_t kMaxFrameRate         = 30;
constexpr uint16_t kMinViewportWidth     = 640;
constexpr uint16_t kMinViewportHeight    = 360;
constexpr uint16_t kSnapshotWidth        = 640;
constexpr uint16_t kSnapshotHeight       = 480;
constexpr uint8_t kMaxConcurrentEncoders = 1;
constexpr uint32_t kMaxEncodedPixelRate  = static_cast<uint32_t>(kSensorWidth) * kSensorHeight * kMaxFrameRate;
constexpr uint32_t kMaxContentBufferSize = 1024 * 1024;
constexpr uint32_t kMaxNetworkBandwidth  = 10 * 1000 * 1000; // bps
constexpr uint32_t kMinH264BitRate       = 10000;
constexpr uint8_t kMicrophoneChannels    = 1;

// Mechanical PTZ capabilities.
constexpr uint8_t kMaxPresets = 5;
constexpr int16_t kPanMin     = -90;
constexpr int16_t kPanMax     = 90;
constexpr int16_t kTiltMin    = -90;
constexpr int16_t kTiltMax    = 90;
constexpr uint8_t kZoomMax    = 100;

// The host does not report the end of a physical PTZ movement yet: assume it completes after this delay.
constexpr System::Clock::Milliseconds32 kPhysicalMoveDuration(2000);

// Maximum time the Matter thread waits for the host to answer a blocking request.
constexpr System::Clock::Milliseconds32 kHostAnswerTimeout(2000);

// Largest CaptureSnapshotResponse accepted from the host. Must fit in the MMIC transport frame.
constexpr size_t kMaxSnapshotResponseSize = 2048;

const AudioCodecEnum kAudioCodecs[]            = { AudioCodecEnum::kOpus };
const uint32_t kAudioSampleRates[]             = { 48000 };
const uint8_t kAudioBitDepths[]                = { 16 };
const ClusterId kClientClusters[]              = { WebRTCTransportRequestor::Id };
const Globals::StreamUsageEnum kStreamUsages[] = { Globals::StreamUsageEnum::kLiveView, Globals::StreamUsageEnum::kRecording };

template <typename StreamList, typename IdGetter>
uint16_t NextStreamId(const StreamList & streams, IdGetter idGetter)
{
    uint16_t nextId = 0;
    for (const auto & stream : streams)
    {
        nextId = std::max<uint16_t>(nextId, static_cast<uint16_t>(idGetter(stream) + 1));
    }
    return nextId;
}

template <typename StreamList, typename IdGetter>
bool ContainsStream(const StreamList & streams, uint16_t streamId, IdGetter idGetter)
{
    return std::any_of(streams.begin(), streams.end(), [&](const auto & stream) { return idGetter(stream) == streamId; });
}

uint16_t VideoStreamId(const VideoStreamStruct & stream)
{
    return stream.videoStreamID;
}

uint16_t AudioStreamId(const AudioStreamStruct & stream)
{
    return stream.audioStreamID;
}

uint16_t SnapshotStreamId(const SnapshotStreamStruct & stream)
{
    return stream.snapshotStreamID;
}

/// Forwards a command to the host. Failures are logged only: the SoC remains the source of truth.
template <typename... Ts>
void ForwardCommand(EndpointId endpoint, ClusterId cluster, CommandId command, const Ts &... params)
{
    CHIP_ERROR err = ::chip::Silabs::Mmic::SendClusterCommand(endpoint, cluster, command, params...);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Camera, "ForwardingCamera: forwarding command " ChipLogFormatMEI " of cluster " ChipLogFormatMEI
                     " failed: %" CHIP_ERROR_FORMAT,
                     ChipLogValueMEI(command), ChipLogValueMEI(cluster), err.Format());
    }
}

/// Forwards a command to the host and waits for its answer. Returns the status reported by the host,
/// or no value when the host did not answer.
template <typename... Ts>
std::optional<Status> RequestHost(EndpointId endpoint, ClusterId cluster, CommandId command, const Ts &... params)
{
    Status hostStatus = Status::Failure;
    MutableByteSpan noResponse;
    CHIP_ERROR err = ::chip::Silabs::Mmic::RequestClusterCommand(endpoint, cluster, command, kHostAnswerTimeout, hostStatus,
                                                                 noResponse, params...);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Camera, "ForwardingCamera: command " ChipLogFormatMEI " of cluster " ChipLogFormatMEI
                     " not answered by host: %" CHIP_ERROR_FORMAT,
                     ChipLogValueMEI(command), ChipLogValueMEI(cluster), err.Format());
        return std::nullopt;
    }
    if (hostStatus != Status::Success)
    {
        ChipLogError(Camera, "ForwardingCamera: command " ChipLogFormatMEI " of cluster " ChipLogFormatMEI
                     " rejected by host, status 0x%02x",
                     ChipLogValueMEI(command), ChipLogValueMEI(cluster), to_underlying(hostStatus));
    }
    return hostStatus;
}

/// Same as RequestHost(), for the delegates reporting a CHIP_ERROR.
template <typename... Ts>
CHIP_ERROR RequestHostError(EndpointId endpoint, ClusterId cluster, CommandId command, const Ts &... params)
{
    Status status = RequestHost(endpoint, cluster, command, params...).value_or(Status::Failure);
    return (status == Status::Success) ? CHIP_NO_ERROR : CHIP_ERROR_IM_GLOBAL_STATUS_VALUE(status);
}

/// Reads the webRTCSessionID (context tag 0) of a WebRTCTransportRequestor command request structure.
CHIP_ERROR DecodeRequestorSessionId(const ByteSpan & params, uint16_t & sessionId)
{
    TLV::TLVReader reader;
    reader.Init(params);
    ReturnErrorOnFailure(reader.Next(TLV::kTLVType_Structure, TLV::AnonymousTag()));
    TLV::TLVType outer;
    ReturnErrorOnFailure(reader.EnterContainer(outer));
    ReturnErrorOnFailure(reader.Next(TLV::ContextTag(0)));
    return reader.Get(sessionId);
}

/// A WebRTCTransportRequestor command from the host, sent once a CASE session with the requestor exists.
class PendingRelay
{
public:
    PendingRelay(CommandId commandId, EndpointId endpoint, std::vector<uint8_t> && params) :
        mCommandId(commandId), mEndpoint(endpoint), mParams(std::move(params)), mOnConnected(OnConnected, this),
        mOnFailure(OnFailure, this)
    {}

    void Start(const ScopedNodeId & peer)
    {
        Server::GetInstance().GetCASESessionManager()->FindOrEstablishSession(peer, &mOnConnected, &mOnFailure);
    }

private:
    static void OnConnected(void * context, Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle)
    {
        std::unique_ptr<PendingRelay> self(static_cast<PendingRelay *>(context));
        CHIP_ERROR err = self->Send(exchangeMgr, sessionHandle);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(Camera, "ForwardingCamera: relaying requestor command " ChipLogFormatMEI " failed: %" CHIP_ERROR_FORMAT,
                         ChipLogValueMEI(self->mCommandId), err.Format());
        }
    }

    static void OnFailure(void * context, const ScopedNodeId & peerId, CHIP_ERROR error)
    {
        std::unique_ptr<PendingRelay> self(static_cast<PendingRelay *>(context));
        ChipLogError(Camera, "ForwardingCamera: no session with requestor " ChipLogFormatScopedNodeId ": %" CHIP_ERROR_FORMAT,
                     ChipLogValueScopedNodeId(peerId), error.Format());
    }

    template <typename CommandType>
    CHIP_ERROR Invoke(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle, const CommandType & command)
    {
        const CommandId commandId = mCommandId;
        auto onSuccess            = [commandId](const ConcreteCommandPath &, const StatusIB &, const auto &) {
            ChipLogProgress(Camera, "ForwardingCamera: requestor command " ChipLogFormatMEI " delivered", ChipLogValueMEI(commandId));
        };
        auto onFailure = [commandId](CHIP_ERROR error) {
            ChipLogError(Camera, "ForwardingCamera: requestor command " ChipLogFormatMEI " failed: %" CHIP_ERROR_FORMAT,
                         ChipLogValueMEI(commandId), error.Format());
        };
        return Controller::InvokeCommandRequest(&exchangeMgr, sessionHandle, mEndpoint, command, onSuccess, onFailure);
    }

    CHIP_ERROR Send(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle)
    {
        TLV::TLVReader reader;
        reader.Init(mParams.data(), mParams.size());
        ReturnErrorOnFailure(reader.Next());

        switch (mCommandId)
        {
        case Requestor::Commands::Offer::Id: {
            Requestor::Commands::Offer::DecodableType request;
            ReturnErrorOnFailure(DataModel::Decode(reader, request));
            Requestor::Commands::Offer::Type command;
            command.webRTCSessionID    = request.webRTCSessionID;
            command.sdp                = request.sdp;
            command.ICETransportPolicy = request.ICETransportPolicy;
            return Invoke(exchangeMgr, sessionHandle, command);
        }
        case Requestor::Commands::Answer::Id: {
            Requestor::Commands::Answer::DecodableType request;
            ReturnErrorOnFailure(DataModel::Decode(reader, request));
            Requestor::Commands::Answer::Type command;
            command.webRTCSessionID = request.webRTCSessionID;
            command.sdp             = request.sdp;
            return Invoke(exchangeMgr, sessionHandle, command);
        }
        case Requestor::Commands::ICECandidates::Id: {
            Requestor::Commands::ICECandidates::DecodableType request;
            ReturnErrorOnFailure(DataModel::Decode(reader, request));
            std::vector<Globals::Structs::ICECandidateStruct::Type> candidates;
            auto it = request.ICECandidates.begin();
            while (it.Next())
            {
                candidates.push_back(it.GetValue());
            }
            ReturnErrorOnFailure(it.GetStatus());
            Requestor::Commands::ICECandidates::Type command;
            command.webRTCSessionID = request.webRTCSessionID;
            command.ICECandidates   = DataModel::List<const Globals::Structs::ICECandidateStruct::Type>(candidates.data(),
                                                                                                        candidates.size());
            return Invoke(exchangeMgr, sessionHandle, command);
        }
        case Requestor::Commands::End::Id: {
            Requestor::Commands::End::DecodableType request;
            ReturnErrorOnFailure(DataModel::Decode(reader, request));
            Requestor::Commands::End::Type command;
            command.webRTCSessionID = request.webRTCSessionID;
            command.reason          = request.reason;
            return Invoke(exchangeMgr, sessionHandle, command);
        }
        default:
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
    }

    const CommandId mCommandId;
    const EndpointId mEndpoint;
    const std::vector<uint8_t> mParams;
    Callback::Callback<OnDeviceConnected> mOnConnected;
    Callback::Callback<OnDeviceConnectionFailure> mOnFailure;
};

// A command received from the host on the MMIC task, run on the Matter thread.
struct HostCommand
{
    CommandId commandId;
    std::vector<uint8_t> params;
};

// Keeps the data referenced by a rebuilt offer request alive until it is encoded.
struct OfferRequestStorage
{
    std::vector<std::vector<CharSpan>> urls;
    std::vector<Globals::Structs::ICEServerStruct::Type> iceServers;
};

/// Rebuilds the SolicitOffer / ProvideOffer request fields from the delegate arguments.
template <typename RequestType>
CHIP_ERROR BuildOfferRequest(const WebRtc::Delegate::OfferRequestArgs & args, RequestType & request,
                             OfferRequestStorage & storage)
{
    request.originatingEndpointID = args.originatingEndpointId;
    if (args.videoStreams.HasValue())
    {
        request.videoStreams.SetValue(
            DataModel::List<const uint16_t>(args.videoStreams.Value().data(), args.videoStreams.Value().size()));
    }
    if (args.audioStreams.HasValue())
    {
        request.audioStreams.SetValue(
            DataModel::List<const uint16_t>(args.audioStreams.Value().data(), args.audioStreams.Value().size()));
    }
    if (args.iceServers.HasValue())
    {
        const auto & servers = args.iceServers.Value();
        storage.urls.resize(servers.size());
        storage.iceServers.resize(servers.size());
        for (size_t i = 0; i < servers.size(); i++)
        {
            auto it = servers[i].URLs.begin();
            while (it.Next())
            {
                storage.urls[i].push_back(it.GetValue());
            }
            ReturnErrorOnFailure(it.GetStatus());

            storage.iceServers[i].URLs       = DataModel::List<const CharSpan>(storage.urls[i].data(), storage.urls[i].size());
            storage.iceServers[i].username   = servers[i].username;
            storage.iceServers[i].credential = servers[i].credential;
            storage.iceServers[i].caid       = servers[i].caid;
        }
        request.ICEServers.SetValue(
            DataModel::List<const Globals::Structs::ICEServerStruct::Type>(storage.iceServers.data(), storage.iceServers.size()));
    }
    if (args.iceTransportPolicy.HasValue())
    {
        request.ICETransportPolicy.SetValue(
            CharSpan(args.iceTransportPolicy.Value().data(), args.iceTransportPolicy.Value().size()));
    }
    request.SFrameConfig = args.sFrameConfig;
    return CHIP_NO_ERROR;
}

Avsum::Structs::MPTZStruct::Type MakeMptz(Optional<int16_t> pan, Optional<int16_t> tilt, Optional<uint8_t> zoom)
{
    Avsum::Structs::MPTZStruct::Type position;
    position.pan  = pan;
    position.tilt = tilt;
    position.zoom = zoom;
    return position;
}

} // namespace

ForwardingCamera * ForwardingCamera::sInstance = nullptr;

ForwardingCamera::ForwardingCamera() : SingleEndpoint(Span<const DataModel::DeviceTypeEntry>(&Device::Type::kCamera, 1)) {}

CHIP_ERROR ForwardingCamera::Register(EndpointId endpoint, CodeDrivenDataModelProvider & provider, EndpointComposition composition)
{
    ReturnErrorOnFailure(RegisterDescriptor(endpoint, provider, composition));

    BitFlags<Feature> features(Feature::kVideo, Feature::kAudio, Feature::kSnapshot);

    AudioCapabilitiesStruct microphoneCapabilities;
    microphoneCapabilities.maxNumberOfChannels  = kMicrophoneChannels;
    microphoneCapabilities.supportedCodecs      = Span<const AudioCodecEnum>(kAudioCodecs);
    microphoneCapabilities.supportedSampleRates = Span<const uint32_t>(kAudioSampleRates);
    microphoneCapabilities.supportedBitDepths   = Span<const uint8_t>(kAudioBitDepths);

    SnapshotCapabilitiesStruct snapshotCapabilities;
    snapshotCapabilities.resolution              = { kSnapshotWidth, kSnapshotHeight };
    snapshotCapabilities.maxFrameRate            = kMaxFrameRate;
    snapshotCapabilities.imageCodec              = ImageCodecEnum::kJpeg;
    snapshotCapabilities.requiresEncodedPixels   = false;
    snapshotCapabilities.requiresHardwareEncoder = MakeOptional(false);

    RateDistortionTradeOffStruct rateDistortionTradeOffPoint;
    rateDistortionTradeOffPoint.codec      = VideoCodecEnum::kH264;
    rateDistortionTradeOffPoint.resolution = { kMinViewportWidth, kMinViewportHeight };
    rateDistortionTradeOffPoint.minBitRate = kMinH264BitRate;

    VideoSensorParamsStruct sensorParams;
    sensorParams.sensorWidth  = kSensorWidth;
    sensorParams.sensorHeight = kSensorHeight;
    sensorParams.maxFPS       = kMaxFrameRate;

    CameraAVStreamManagementCluster::InitArguments args{
        .delegate                     = *this,
        .endpointId                   = endpoint,
        .features                     = features,
        .optionalAttrs                = BitFlags<OptionalAttribute>(),
        .maxConcurrentEncoders        = kMaxConcurrentEncoders,
        .maxEncodedPixelRate          = kMaxEncodedPixelRate,
        .videoSensorParams            = sensorParams,
        .nightVisionUsesInfrared      = false,
        .minViewPort                  = { kMinViewportWidth, kMinViewportHeight },
        .rateDistortionTradeOffPoints = { rateDistortionTradeOffPoint },
        .maxContentBufferSize         = kMaxContentBufferSize,
        .microphoneCapabilities       = microphoneCapabilities,
        .spkrCapabilities             = AudioCapabilitiesStruct(),
        .twoWayTalkSupport            = TwoWayTalkSupportTypeEnum::kNotSupported,
        .snapshotCapabilities         = { snapshotCapabilities },
        .maxNetworkBandwidth          = kMaxNetworkBandwidth,
        .supportedStreamUsages        = std::vector<Globals::StreamUsageEnum>(std::begin(kStreamUsages), std::end(kStreamUsages)),
        .streamUsagePriorities        = std::vector<Globals::StreamUsageEnum>(std::begin(kStreamUsages), std::end(kStreamUsages)),
    };
    mAvStreamManagementCluster.Create(std::move(args));
    ReturnErrorOnFailure(provider.AddCluster(mAvStreamManagementCluster.Registration()));

    mWebRTCTransportProviderCluster.Create(endpoint, *this);
    ReturnErrorOnFailure(provider.AddCluster(mWebRTCTransportProviderCluster.Registration()));

    BitFlags<Avsum::Feature> avSettingsFeatures(Avsum::Feature::kDigitalPTZ, Avsum::Feature::kMechanicalPan,
                                                Avsum::Feature::kMechanicalTilt, Avsum::Feature::kMechanicalZoom,
                                                Avsum::Feature::kMechanicalPresets);
    mAvSettingsCluster.Create(endpoint, avSettingsFeatures, kMaxPresets);
    auto & avSettings = mAvSettingsCluster.Cluster();
    avSettings.SetDelegate(&mAvSettingsDelegate);
    ReturnErrorOnFailure(avSettings.SetPanMin(kPanMin));
    ReturnErrorOnFailure(avSettings.SetPanMax(kPanMax));
    ReturnErrorOnFailure(avSettings.SetTiltMin(kTiltMin));
    ReturnErrorOnFailure(avSettings.SetTiltMax(kTiltMax));
    ReturnErrorOnFailure(avSettings.SetZoomMax(kZoomMax));
    ReturnErrorOnFailure(provider.AddCluster(mAvSettingsCluster.Registration()));

    return provider.AddEndpoint(mEndpointRegistration);
}

void ForwardingCamera::Unregister(CodeDrivenDataModelProvider & provider)
{
    if (sInstance == this)
    {
        mmic_set_cluster_command_handler(nullptr);
        sInstance = nullptr;
    }
    UnregisterDescriptor(provider);
    if (mAvSettingsCluster.IsConstructed())
    {
        mAvSettingsDelegate.ShutdownApp();
        LogErrorOnFailure(provider.RemoveCluster(&mAvSettingsCluster.Cluster()));
        mAvSettingsCluster.Destroy();
    }
    if (mWebRTCTransportProviderCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mWebRTCTransportProviderCluster.Cluster()));
        mWebRTCTransportProviderCluster.Destroy();
    }
    if (mAvStreamManagementCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mAvStreamManagementCluster.Cluster()));
        mAvStreamManagementCluster.Destroy();
    }
    mSessionStreams.clear();
}

CHIP_ERROR ForwardingCamera::ClientClusters(ReadOnlyBufferBuilder<ClusterId> & out) const
{
    return out.ReferenceExisting(Span<const ClusterId>(kClientClusters));
}

CHIP_ERROR ForwardingCamera::InitClusters()
{
    VerifyOrReturnError(mAvStreamManagementCluster.IsConstructed() && mAvSettingsCluster.IsConstructed(),
                        CHIP_ERROR_INCORRECT_STATE);

    auto & cluster = mAvStreamManagementCluster.Cluster();
    ReturnErrorOnFailure(cluster.SetViewport({ 0, 0, kSensorWidth, kSensorHeight }));
    ReturnErrorOnFailure(cluster.Init());
    ReturnErrorOnFailure(mAvSettingsCluster.Cluster().Init());

    // WebRTC signaling sent back by the host.
    sInstance = this;
    mmic_set_cluster_command_handler(HandleHostCommand);

    ChipLogProgress(Camera, "ForwardingCamera[ep=%u]: clusters initialized, camera control forwarded to host", mEndpointId);
    return CHIP_NO_ERROR;
}

// ---------------------------------------------------------------------------
// CameraAVStreamManagementDelegate
// ---------------------------------------------------------------------------

Status ForwardingCamera::VideoStreamAllocate(const VideoStreamStruct & allocateArgs, uint16_t & outStreamID)
{
    VerifyOrReturnValue(mAvStreamManagementCluster.IsConstructed(), Status::Failure);
    auto & cluster = mAvStreamManagementCluster.Cluster();

    std::optional<uint16_t> reusableId = cluster.GetReusableVideoStreamId(allocateArgs);
    if (reusableId.has_value())
    {
        outStreamID = reusableId.value();
    }
    else
    {
        VerifyOrReturnValue(cluster.GetAllocatedVideoStreams().size() < CHIP_CONFIG_MAX_NUM_CAMERA_VIDEO_STREAMS,
                            Status::ResourceExhausted);
        outStreamID = NextStreamId(cluster.GetAllocatedVideoStreams(), VideoStreamId);
    }

    ChipLogProgress(Camera, "ForwardingCamera: VideoStreamAllocate usage=%u codec=%u %ux%u-%ux%u @%u-%u fps -> id=%u",
                    to_underlying(allocateArgs.streamUsage), to_underlying(allocateArgs.videoCodec),
                    allocateArgs.minResolution.width, allocateArgs.minResolution.height, allocateArgs.maxResolution.width,
                    allocateArgs.maxResolution.height, allocateArgs.minFrameRate, allocateArgs.maxFrameRate, outStreamID);

    // The host validates the stream against the camera capabilities and starts it.
    VideoStreamStruct stream = allocateArgs;
    stream.videoStreamID     = outStreamID;
    return RequestHost(mEndpointId, Avsm::Id, Avsm::Commands::VideoStreamAllocate::Id, stream).value_or(Status::Failure);
}

void ForwardingCamera::OnVideoStreamAllocated(const VideoStreamStruct & allocatedStream, StreamAllocationAction action)
{
    switch (action)
    {
    case StreamAllocationAction::kNewAllocation:
        // Already started by the host in VideoStreamAllocate().
        mAvSettingsDelegate.VideoStreamAllocated(allocatedStream.videoStreamID);
        break;
    case StreamAllocationAction::kModification:
        // The host restarts the stream with the updated parameters.
        ForwardCommand(mEndpointId, Avsm::Id, Avsm::Commands::VideoStreamAllocate::Id, allocatedStream);
        break;
    case StreamAllocationAction::kReuse:
        ChipLogProgress(Camera, "ForwardingCamera: video stream %u reused, nothing to forward", allocatedStream.videoStreamID);
        break;
    }
}

Status ForwardingCamera::VideoStreamModify(const uint16_t streamID, const Optional<bool> waterMarkEnabled,
                                           const Optional<bool> osdEnabled)
{
    Avsm::Commands::VideoStreamModify::Type request;
    request.videoStreamID    = streamID;
    request.watermarkEnabled = waterMarkEnabled;
    request.OSDEnabled       = osdEnabled;
    return RequestHost(mEndpointId, Avsm::Id, Avsm::Commands::VideoStreamModify::Id, request).value_or(Status::Failure);
}

Status ForwardingCamera::VideoStreamDeallocate(const uint16_t streamID)
{
    mAvSettingsDelegate.VideoStreamDeallocated(streamID);

    Avsm::Commands::VideoStreamDeallocate::Type request;
    request.videoStreamID = streamID;
    // Deallocation goes through when the host is not reachable, to keep the data model consistent.
    return RequestHost(mEndpointId, Avsm::Id, Avsm::Commands::VideoStreamDeallocate::Id, request).value_or(Status::Success);
}

Status ForwardingCamera::AudioStreamAllocate(const AudioStreamStruct & allocateArgs, uint16_t & outStreamID)
{
    VerifyOrReturnValue(mAvStreamManagementCluster.IsConstructed(), Status::Failure);
    const auto & streams = mAvStreamManagementCluster.Cluster().GetAllocatedAudioStreams();
    VerifyOrReturnValue(streams.size() < CHIP_CONFIG_MAX_NUM_CAMERA_AUDIO_STREAMS, Status::ResourceExhausted);

    outStreamID = NextStreamId(streams, AudioStreamId);
    ChipLogProgress(Camera, "ForwardingCamera: AudioStreamAllocate usage=%u codec=%u channels=%u rate=%lu -> id=%u",
                    to_underlying(allocateArgs.streamUsage), to_underlying(allocateArgs.audioCodec), allocateArgs.channelCount,
                    static_cast<unsigned long>(allocateArgs.sampleRate), outStreamID);

    AudioStreamStruct allocatedStream = allocateArgs;
    allocatedStream.audioStreamID     = outStreamID;
    return RequestHost(mEndpointId, Avsm::Id, Avsm::Commands::AudioStreamAllocate::Id, allocatedStream).value_or(Status::Failure);
}

Status ForwardingCamera::AudioStreamDeallocate(const uint16_t streamID)
{
    Avsm::Commands::AudioStreamDeallocate::Type request;
    request.audioStreamID = streamID;
    return RequestHost(mEndpointId, Avsm::Id, Avsm::Commands::AudioStreamDeallocate::Id, request).value_or(Status::Success);
}

Status ForwardingCamera::SnapshotStreamAllocate(const SnapshotStreamAllocateArgs & allocateArgs, uint16_t & outStreamID)
{
    VerifyOrReturnValue(mAvStreamManagementCluster.IsConstructed(), Status::Failure);
    auto & cluster = mAvStreamManagementCluster.Cluster();

    std::optional<uint16_t> reusableId = cluster.GetReusableSnapshotStreamId(allocateArgs);
    if (reusableId.has_value())
    {
        outStreamID = reusableId.value();
        ChipLogProgress(Camera, "ForwardingCamera: snapshot stream %u reused, nothing to forward", outStreamID);
        return Status::Success;
    }

    VerifyOrReturnValue(cluster.GetAllocatedSnapshotStreams().size() < CHIP_CONFIG_MAX_NUM_CAMERA_SNAPSHOT_STREAMS,
                        Status::ResourceExhausted);
    outStreamID = NextStreamId(cluster.GetAllocatedSnapshotStreams(), SnapshotStreamId);

    ChipLogProgress(Camera, "ForwardingCamera: SnapshotStreamAllocate codec=%u %ux%u-%ux%u quality=%u -> id=%u",
                    to_underlying(allocateArgs.imageCodec), allocateArgs.minResolution.width, allocateArgs.minResolution.height,
                    allocateArgs.maxResolution.width, allocateArgs.maxResolution.height, allocateArgs.quality, outStreamID);

    SnapshotStreamStruct allocatedStream;
    allocatedStream.snapshotStreamID = outStreamID;
    allocatedStream.imageCodec       = allocateArgs.imageCodec;
    allocatedStream.frameRate        = allocateArgs.maxFrameRate;
    allocatedStream.minResolution    = allocateArgs.minResolution;
    allocatedStream.maxResolution    = allocateArgs.maxResolution;
    allocatedStream.quality          = allocateArgs.quality;
    allocatedStream.referenceCount   = 0;
    allocatedStream.encodedPixels    = allocateArgs.encodedPixels;
    allocatedStream.hardwareEncoder  = allocateArgs.hardwareEncoder;
    allocatedStream.watermarkEnabled = allocateArgs.watermarkEnabled;
    allocatedStream.OSDEnabled       = allocateArgs.OSDEnabled;
    return RequestHost(mEndpointId, Avsm::Id, Avsm::Commands::SnapshotStreamAllocate::Id, allocatedStream)
        .value_or(Status::Failure);
}

Status ForwardingCamera::SnapshotStreamModify(const uint16_t streamID, const Optional<bool> waterMarkEnabled,
                                              const Optional<bool> osdEnabled)
{
    Avsm::Commands::SnapshotStreamModify::Type request;
    request.snapshotStreamID = streamID;
    request.watermarkEnabled = waterMarkEnabled;
    request.OSDEnabled       = osdEnabled;
    return RequestHost(mEndpointId, Avsm::Id, Avsm::Commands::SnapshotStreamModify::Id, request).value_or(Status::Failure);
}

Status ForwardingCamera::SnapshotStreamDeallocate(const uint16_t streamID)
{
    Avsm::Commands::SnapshotStreamDeallocate::Type request;
    request.snapshotStreamID = streamID;
    return RequestHost(mEndpointId, Avsm::Id, Avsm::Commands::SnapshotStreamDeallocate::Id, request).value_or(Status::Success);
}

void ForwardingCamera::OnStreamUsagePrioritiesChanged()
{
    VerifyOrReturn(mAvStreamManagementCluster.IsConstructed());
    const auto & priorities = mAvStreamManagementCluster.Cluster().GetStreamUsagePriorities();

    Avsm::Commands::SetStreamPriorities::Type request;
    request.streamPriorities = DataModel::List<const Globals::StreamUsageEnum>(priorities.data(), priorities.size());
    ForwardCommand(mEndpointId, Avsm::Id, Avsm::Commands::SetStreamPriorities::Id, request);
}

void ForwardingCamera::OnAttributeChanged(AttributeId attributeId)
{
    VerifyOrReturn(mAvStreamManagementCluster.IsConstructed());

    switch (attributeId)
    {
    case Avsm::Attributes::StreamUsagePriorities::Id:
        // Forwarded as a SetStreamPriorities command by OnStreamUsagePrioritiesChanged().
        return;
    case Avsm::Attributes::Viewport::Id:
        mAvSettingsDelegate.DefaultViewportUpdated(mAvStreamManagementCluster.Cluster().GetViewport());
        break;
    default:
        break;
    }

    ForwardAttributeChange(attributeId);
}

void ForwardingCamera::ForwardAttributeChange(AttributeId attributeId)
{
    namespace Attrs = Avsm::Attributes;
    const auto & cluster = mAvStreamManagementCluster.Cluster();
    auto send            = [&](const auto & value) {
        return ::chip::Silabs::Mmic::SendAttributeChanged(mEndpointId, Avsm::Id, attributeId, value);
    };

    CHIP_ERROR err = CHIP_NO_ERROR;
    switch (attributeId)
    {
    case Attrs::HDRModeEnabled::Id:
        err = send(cluster.GetHDRModeEnabled());
        break;
    case Attrs::SoftRecordingPrivacyModeEnabled::Id:
        err = send(cluster.GetSoftRecordingPrivacyModeEnabled());
        break;
    case Attrs::SoftLivestreamPrivacyModeEnabled::Id:
        err = send(cluster.GetSoftLivestreamPrivacyModeEnabled());
        break;
    case Attrs::HardPrivacyModeOn::Id:
        err = send(cluster.GetHardPrivacyModeOn());
        break;
    case Attrs::NightVision::Id:
        err = send(cluster.GetNightVision());
        break;
    case Attrs::NightVisionIllum::Id:
        err = send(cluster.GetNightVisionIllum());
        break;
    case Attrs::Viewport::Id:
        err = send(cluster.GetViewport());
        break;
    case Attrs::SpeakerMuted::Id:
        err = send(cluster.GetSpeakerMuted());
        break;
    case Attrs::SpeakerVolumeLevel::Id:
        err = send(cluster.GetSpeakerVolumeLevel());
        break;
    case Attrs::MicrophoneMuted::Id:
        err = send(cluster.GetMicrophoneMuted());
        break;
    case Attrs::MicrophoneVolumeLevel::Id:
        err = send(cluster.GetMicrophoneVolumeLevel());
        break;
    case Attrs::ImageRotation::Id:
        err = send(cluster.GetImageRotation());
        break;
    case Attrs::ImageFlipHorizontal::Id:
        err = send(cluster.GetImageFlipHorizontal());
        break;
    case Attrs::ImageFlipVertical::Id:
        err = send(cluster.GetImageFlipVertical());
        break;
    case Attrs::LocalVideoRecordingEnabled::Id:
        err = send(cluster.GetLocalVideoRecordingEnabled());
        break;
    case Attrs::LocalSnapshotRecordingEnabled::Id:
        err = send(cluster.GetLocalSnapshotRecordingEnabled());
        break;
    case Attrs::StatusLightEnabled::Id:
        err = send(cluster.GetStatusLightEnabled());
        break;
    case Attrs::StatusLightBrightness::Id:
        err = send(cluster.GetStatusLightBrightness());
        break;
    default:
        // The host is notified without the value.
        err = ::chip::Silabs::Mmic::SendAttributeChanged(mEndpointId, Avsm::Id, attributeId);
        break;
    }

    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Camera, "ForwardingCamera: forwarding attribute " ChipLogFormatMEI " change failed: %" CHIP_ERROR_FORMAT,
                     ChipLogValueMEI(attributeId), err.Format());
    }
}

Status ForwardingCamera::CaptureSnapshot(const DataModel::Nullable<uint16_t> streamID, const VideoResolutionStruct & resolution,
                                         ImageSnapshot & outImageSnapshot)
{
    Avsm::Commands::CaptureSnapshot::Type request;
    request.snapshotStreamID    = streamID;
    request.requestedResolution = resolution;

    Platform::ScopedMemoryBuffer<uint8_t> responseBuffer;
    VerifyOrReturnValue(responseBuffer.Alloc(kMaxSnapshotResponseSize), Status::ResourceExhausted);
    MutableByteSpan response(responseBuffer.Get(), kMaxSnapshotResponseSize);

    // The image is produced by the host: wait for its answer.
    Status hostStatus = Status::Failure;
    CHIP_ERROR err    = ::chip::Silabs::Mmic::RequestClusterCommand(mEndpointId, Avsm::Id, Avsm::Commands::CaptureSnapshot::Id,
                                                            kHostAnswerTimeout, hostStatus, response, request);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Status::Failure,
                        ChipLogError(Camera, "ForwardingCamera: CaptureSnapshot not answered by host: %" CHIP_ERROR_FORMAT,
                                     err.Format()));
    VerifyOrReturnValue(hostStatus == Status::Success, hostStatus,
                        ChipLogError(Camera, "ForwardingCamera: CaptureSnapshot rejected by host, status 0x%02x",
                                     to_underlying(hostStatus)));

    TLV::TLVReader reader;
    reader.Init(response);
    Avsm::Commands::CaptureSnapshotResponse::DecodableType snapshot;
    err = reader.Next();
    if (err == CHIP_NO_ERROR)
    {
        err = DataModel::Decode(reader, snapshot);
    }
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Status::Failure,
                        ChipLogError(Camera, "ForwardingCamera: invalid CaptureSnapshot answer from host: %" CHIP_ERROR_FORMAT,
                                     err.Format()));

    outImageSnapshot.data.assign(snapshot.data.begin(), snapshot.data.end());
    outImageSnapshot.imageRes.width  = snapshot.resolution.width;
    outImageSnapshot.imageRes.height = snapshot.resolution.height;
    outImageSnapshot.imageCodec      = snapshot.imageCodec;
    return Status::Success;
}

CHIP_ERROR ForwardingCamera::PersistentAttributesLoadedCallback()
{
    const auto & cluster = mAvStreamManagementCluster.Cluster();
    ChipLogProgress(Camera, "ForwardingCamera: persisted streams loaded (video=%u audio=%u snapshot=%u)",
                    static_cast<unsigned>(cluster.GetAllocatedVideoStreams().size()),
                    static_cast<unsigned>(cluster.GetAllocatedAudioStreams().size()),
                    static_cast<unsigned>(cluster.GetAllocatedSnapshotStreams().size()));

    // Re-send the persisted allocations so that the host restarts its pipelines.
    for (const auto & stream : cluster.GetAllocatedVideoStreams())
    {
        mAvSettingsDelegate.VideoStreamAllocated(stream.videoStreamID);
        ForwardCommand(mEndpointId, Avsm::Id, Avsm::Commands::VideoStreamAllocate::Id, stream);
    }
    for (const auto & stream : cluster.GetAllocatedAudioStreams())
    {
        ForwardCommand(mEndpointId, Avsm::Id, Avsm::Commands::AudioStreamAllocate::Id, stream);
    }
    for (const auto & stream : cluster.GetAllocatedSnapshotStreams())
    {
        ForwardCommand(mEndpointId, Avsm::Id, Avsm::Commands::SnapshotStreamAllocate::Id, stream);
    }
    return CHIP_NO_ERROR;
}

const std::vector<VideoStreamStruct> & ForwardingCamera::GetAllocatedVideoStreams() const
{
    static const std::vector<VideoStreamStruct> kEmpty;
    return mAvStreamManagementCluster.IsConstructed() ? mAvStreamManagementCluster.Cluster().GetAllocatedVideoStreams() : kEmpty;
}

const std::vector<AudioStreamStruct> & ForwardingCamera::GetAllocatedAudioStreams() const
{
    static const std::vector<AudioStreamStruct> kEmpty;
    return mAvStreamManagementCluster.IsConstructed() ? mAvStreamManagementCluster.Cluster().GetAllocatedAudioStreams() : kEmpty;
}

// ---------------------------------------------------------------------------
// WebRTCTransportProvider::Delegate
// ---------------------------------------------------------------------------

void ForwardingCamera::FillSession(const OfferRequestArgs & args, WebRTCSessionStruct & outSession)
{
    outSession.id             = args.sessionId;
    outSession.peerNodeID     = args.peerNodeId;
    outSession.peerEndpointID = args.originatingEndpointId;
    outSession.streamUsage    = args.streamUsage;
    outSession.fabricIndex    = args.fabricIndex;

    // Release references held by a previous negotiation on the same session before re-acquiring.
    ReleaseSessionStreams(args.sessionId);

    SessionInfo & streams   = mSessionStreams[args.sessionId];
    streams.peerNodeId     = args.peerNodeId;
    streams.fabricIndex    = args.fabricIndex;
    streams.peerEndpointId = args.originatingEndpointId;
    if (args.videoStreams.HasValue())
    {
        streams.videoStreams = args.videoStreams.Value();
    }
    if (args.audioStreams.HasValue())
    {
        streams.audioStreams = args.audioStreams.Value();
    }

    auto & cluster = mAvStreamManagementCluster.Cluster();
    for (uint16_t id : streams.videoStreams)
    {
        LogErrorOnFailure(cluster.UpdateVideoStreamRefCount(id, true));
    }
    for (uint16_t id : streams.audioStreams)
    {
        LogErrorOnFailure(cluster.UpdateAudioStreamRefCount(id, true));
    }

    if (streams.videoStreams.empty())
    {
        outSession.videoStreamID.SetNull();
    }
    else
    {
        outSession.videoStreamID.SetNonNull(streams.videoStreams.front());
        outSession.videoStreams.SetValue(DataModel::List<const uint16_t>(streams.videoStreams.data(), streams.videoStreams.size()));
    }

    if (streams.audioStreams.empty())
    {
        outSession.audioStreamID.SetNull();
    }
    else
    {
        outSession.audioStreamID.SetNonNull(streams.audioStreams.front());
        outSession.audioStreams.SetValue(DataModel::List<const uint16_t>(streams.audioStreams.data(), streams.audioStreams.size()));
    }
}

void ForwardingCamera::ReleaseSessionStreams(uint16_t sessionId)
{
    auto it = mSessionStreams.find(sessionId);
    VerifyOrReturn(it != mSessionStreams.end());

    if (mAvStreamManagementCluster.IsConstructed())
    {
        auto & cluster = mAvStreamManagementCluster.Cluster();
        for (uint16_t id : it->second.videoStreams)
        {
            LogErrorOnFailure(cluster.UpdateVideoStreamRefCount(id, false));
        }
        for (uint16_t id : it->second.audioStreams)
        {
            LogErrorOnFailure(cluster.UpdateAudioStreamRefCount(id, false));
        }
    }
    mSessionStreams.erase(it);
}

uint8_t ForwardingCamera::HandleHostCommand(uint16_t endpointId, uint32_t clusterId, uint32_t commandId, const uint8_t * params,
                                            uint16_t paramsLen)
{
    // Runs on the MMIC task: only validate and hand the command over to the Matter thread.
    ForwardingCamera * camera = sInstance;
    VerifyOrReturnValue(camera != nullptr && endpointId == camera->mEndpointId, to_underlying(Status::UnsupportedEndpoint));
    VerifyOrReturnValue(clusterId == Requestor::Id, to_underlying(Status::UnsupportedCluster));
    VerifyOrReturnValue(commandId == Requestor::Commands::Offer::Id || commandId == Requestor::Commands::Answer::Id ||
                            commandId == Requestor::Commands::ICECandidates::Id || commandId == Requestor::Commands::End::Id,
                        to_underlying(Status::UnsupportedCommand));
    VerifyOrReturnValue(params != nullptr && paramsLen > 0, to_underlying(Status::InvalidCommand));

    auto * command = new (std::nothrow) HostCommand{ commandId, std::vector<uint8_t>(params, params + paramsLen) };
    VerifyOrReturnValue(command != nullptr, to_underlying(Status::ResourceExhausted));
    if (DeviceLayer::PlatformMgr().ScheduleWork(RunHostCommand, reinterpret_cast<intptr_t>(command)) != CHIP_NO_ERROR)
    {
        delete command;
        return to_underlying(Status::Busy);
    }
    return to_underlying(Status::Success);
}

void ForwardingCamera::RunHostCommand(intptr_t context)
{
    std::unique_ptr<HostCommand> command(reinterpret_cast<HostCommand *>(context));
    if (sInstance != nullptr)
    {
        sInstance->RelayToRequestor(command->commandId, std::move(command->params));
    }
}

void ForwardingCamera::RelayToRequestor(CommandId commandId, std::vector<uint8_t> && params)
{
    uint16_t sessionId = 0;
    CHIP_ERROR err     = DecodeRequestorSessionId(ByteSpan(params.data(), params.size()), sessionId);
    VerifyOrReturn(err == CHIP_NO_ERROR,
                   ChipLogError(Camera, "ForwardingCamera: invalid requestor command from host: %" CHIP_ERROR_FORMAT, err.Format()));

    auto it = mSessionStreams.find(sessionId);
    VerifyOrReturn(it != mSessionStreams.end(), ChipLogError(Camera, "ForwardingCamera: unknown WebRTC session %u", sessionId));

    const ScopedNodeId peer(it->second.peerNodeId, it->second.fabricIndex);
    auto * relay = new (std::nothrow) PendingRelay(commandId, it->second.peerEndpointId, std::move(params));
    VerifyOrReturn(relay != nullptr, ChipLogError(Camera, "ForwardingCamera: no memory to relay WebRTC session %u", sessionId));

    if (commandId == Requestor::Commands::End::Id)
    {
        // The host ended the session (e.g. connection lost): end it on the SoC as well.
        ChipLogProgress(Camera, "ForwardingCamera: WebRTC session %u ended by host", sessionId);
        ReleaseSessionStreams(sessionId);
        if (mWebRTCTransportProviderCluster.IsConstructed())
        {
            mWebRTCTransportProviderCluster.Cluster().RemoveSession(sessionId);
        }
    }

    // The relay object deletes itself once the CASE session callback ran.
    relay->Start(peer);
}

CHIP_ERROR ForwardingCamera::HandleSolicitOffer(const OfferRequestArgs & args, WebRTCSessionStruct & outSession,
                                                bool & outDeferredOffer)
{
    VerifyOrReturnError(mAvStreamManagementCluster.IsConstructed(), CHIP_ERROR_INCORRECT_STATE);

    FillSession(args, outSession);
    outDeferredOffer = false;

    ChipLogProgress(Camera, "ForwardingCamera: SolicitOffer session=%u peer=" ChipLogFormatX64 " usage=%u", args.sessionId,
                    ChipLogValueX64(args.peerNodeId), to_underlying(args.streamUsage));

    // The host creates the peer connection, then sends the Offer, relayed by RelayToRequestor().
    WebRtc::Commands::SolicitOffer::Type request;
    OfferRequestStorage storage;
    CHIP_ERROR err = BuildOfferRequest(args, request, storage);
    if (err == CHIP_NO_ERROR)
    {
        err = RequestHostError(mEndpointId, WebRtc::Id, WebRtc::Commands::SolicitOffer::Id, outSession, request);
    }
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Camera, "ForwardingCamera: SolicitOffer failed: %" CHIP_ERROR_FORMAT, err.Format());
        ReleaseSessionStreams(args.sessionId);
    }
    return err;
}

CHIP_ERROR ForwardingCamera::HandleProvideOffer(const ProvideOfferRequestArgs & args, WebRTCSessionStruct & outSession)
{
    VerifyOrReturnError(mAvStreamManagementCluster.IsConstructed(), CHIP_ERROR_INCORRECT_STATE);

    FillSession(args, outSession);

    ChipLogProgress(Camera, "ForwardingCamera: ProvideOffer session=%u peer=" ChipLogFormatX64 " usage=%u sdpLen=%u",
                    args.sessionId, ChipLogValueX64(args.peerNodeId), to_underlying(args.streamUsage),
                    static_cast<unsigned>(args.sdp.size()));

    // The host applies the offer, then sends the Answer, relayed by RelayToRequestor().
    WebRtc::Commands::ProvideOffer::Type request;
    OfferRequestStorage storage;
    CHIP_ERROR err = BuildOfferRequest(args, request, storage);
    if (err == CHIP_NO_ERROR)
    {
        request.webRTCSessionID.SetNonNull(args.sessionId);
        request.sdp = CharSpan(args.sdp.data(), args.sdp.size());
        err         = RequestHostError(mEndpointId, WebRtc::Id, WebRtc::Commands::ProvideOffer::Id, outSession, request);
    }
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Camera, "ForwardingCamera: ProvideOffer failed: %" CHIP_ERROR_FORMAT, err.Format());
        ReleaseSessionStreams(args.sessionId);
    }
    return err;
}

CHIP_ERROR ForwardingCamera::HandleProvideAnswer(uint16_t sessionId, const std::string & sdpAnswer)
{
    WebRtc::Commands::ProvideAnswer::Type request;
    request.webRTCSessionID = sessionId;
    request.sdp             = CharSpan(sdpAnswer.data(), sdpAnswer.size());
    return RequestHostError(mEndpointId, WebRtc::Id, WebRtc::Commands::ProvideAnswer::Id, request);
}

CHIP_ERROR ForwardingCamera::HandleProvideICECandidates(uint16_t sessionId, const std::vector<ICECandidateStruct> & candidates)
{
    WebRtc::Commands::ProvideICECandidates::Type request;
    request.webRTCSessionID = sessionId;
    request.ICECandidates   = DataModel::List<const ICECandidateStruct>(candidates.data(), candidates.size());
    return RequestHostError(mEndpointId, WebRtc::Id, WebRtc::Commands::ProvideICECandidates::Id, request);
}

CHIP_ERROR ForwardingCamera::HandleEndSession(uint16_t sessionId, WebRTCEndReasonEnum reasonCode)
{
    ReleaseSessionStreams(sessionId);

    WebRtc::Commands::EndSession::Type request;
    request.webRTCSessionID = sessionId;
    request.reason          = reasonCode;
    // The session ends on the SoC even when the host is not reachable.
    Status status = RequestHost(mEndpointId, WebRtc::Id, WebRtc::Commands::EndSession::Id, request).value_or(Status::Success);
    return (status == Status::Success) ? CHIP_NO_ERROR : CHIP_ERROR_IM_GLOBAL_STATUS_VALUE(status);
}

CHIP_ERROR ForwardingCamera::ValidateStreamUsage(StreamUsageEnum streamUsage, Optional<std::vector<uint16_t>> & videoStreams,
                                                 Optional<std::vector<uint16_t>> & audioStreams)
{
    VerifyOrReturnError(mAvStreamManagementCluster.IsConstructed(), CHIP_ERROR_INCORRECT_STATE);
    ReturnErrorOnFailure(IsStreamUsageSupported(streamUsage));

    auto & cluster = mAvStreamManagementCluster.Cluster();

    // Empty lists request automatic selection: prefer a stream with the same usage, else any allocated stream.
    if (videoStreams.HasValue() && videoStreams.Value().empty() && !cluster.GetAllocatedVideoStreams().empty())
    {
        const auto & allocated = cluster.GetAllocatedVideoStreams();
        auto match = std::find_if(allocated.begin(), allocated.end(), [&](const auto & s) { return s.streamUsage == streamUsage; });
        videoStreams.Value().push_back(match != allocated.end() ? match->videoStreamID : allocated.front().videoStreamID);
    }

    if (audioStreams.HasValue() && audioStreams.Value().empty() && !cluster.GetAllocatedAudioStreams().empty())
    {
        const auto & allocated = cluster.GetAllocatedAudioStreams();
        auto match = std::find_if(allocated.begin(), allocated.end(), [&](const auto & s) { return s.streamUsage == streamUsage; });
        audioStreams.Value().push_back(match != allocated.end() ? match->audioStreamID : allocated.front().audioStreamID);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR ForwardingCamera::ValidateVideoStreamID(uint16_t videoStreamId)
{
    return ContainsStream(GetAllocatedVideoStreams(), videoStreamId, VideoStreamId) ? CHIP_NO_ERROR : CHIP_ERROR_NOT_FOUND;
}

CHIP_ERROR ForwardingCamera::ValidateAudioStreamID(uint16_t audioStreamId)
{
    return ContainsStream(GetAllocatedAudioStreams(), audioStreamId, AudioStreamId) ? CHIP_NO_ERROR : CHIP_ERROR_NOT_FOUND;
}

CHIP_ERROR ForwardingCamera::ValidateVideoStreams(const std::vector<uint16_t> & videoStreams)
{
    for (uint16_t id : videoStreams)
    {
        ReturnErrorOnFailure(ValidateVideoStreamID(id));
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR ForwardingCamera::ValidateAudioStreams(const std::vector<uint16_t> & audioStreams)
{
    for (uint16_t id : audioStreams)
    {
        ReturnErrorOnFailure(ValidateAudioStreamID(id));
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR ForwardingCamera::IsStreamUsageSupported(StreamUsageEnum streamUsage)
{
    VerifyOrReturnError(mAvStreamManagementCluster.IsConstructed(), CHIP_ERROR_INCORRECT_STATE);
    const auto & priorities = mAvStreamManagementCluster.Cluster().GetStreamUsagePriorities();
    return (std::find(priorities.begin(), priorities.end(), streamUsage) != priorities.end()) ? CHIP_NO_ERROR
                                                                                              : CHIP_ERROR_NOT_FOUND;
}

CHIP_ERROR ForwardingCamera::IsHardPrivacyModeActive(bool & isActive)
{
    VerifyOrReturnError(mAvStreamManagementCluster.IsConstructed(), CHIP_ERROR_INCORRECT_STATE);
    isActive = mAvStreamManagementCluster.Cluster().GetHardPrivacyModeOn();
    return CHIP_NO_ERROR;
}

CHIP_ERROR ForwardingCamera::IsSoftRecordingPrivacyModeActive(bool & isActive)
{
    VerifyOrReturnError(mAvStreamManagementCluster.IsConstructed(), CHIP_ERROR_INCORRECT_STATE);
    isActive = mAvStreamManagementCluster.Cluster().GetSoftRecordingPrivacyModeEnabled();
    return CHIP_NO_ERROR;
}

CHIP_ERROR ForwardingCamera::IsSoftLivestreamPrivacyModeActive(bool & isActive)
{
    VerifyOrReturnError(mAvStreamManagementCluster.IsConstructed(), CHIP_ERROR_INCORRECT_STATE);
    isActive = mAvStreamManagementCluster.Cluster().GetSoftLivestreamPrivacyModeEnabled();
    return CHIP_NO_ERROR;
}

bool ForwardingCamera::HasAllocatedVideoStreams()
{
    return !GetAllocatedVideoStreams().empty();
}

bool ForwardingCamera::HasAllocatedAudioStreams()
{
    return !GetAllocatedAudioStreams().empty();
}

CHIP_ERROR ForwardingCamera::ValidateSFrameConfig(uint16_t cipherSuite, size_t baseKeyLength)
{
    // SFrame cipher suites: 0x0001 AES-128-GCM-SHA256 (16 byte key), 0x0002 AES-256-GCM-SHA512 (32 byte key).
    switch (cipherSuite)
    {
    case 0x0001:
        return (baseKeyLength == 16) ? CHIP_NO_ERROR : CHIP_ERROR_INVALID_ARGUMENT;
    case 0x0002:
        return (baseKeyLength == 32) ? CHIP_NO_ERROR : CHIP_ERROR_INVALID_ARGUMENT;
    default:
        ChipLogError(Camera, "ForwardingCamera: unsupported SFrame cipher suite 0x%04x", cipherSuite);
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
}

CHIP_ERROR ForwardingCamera::IsUTCTimeNull(bool & isNull)
{
    // No Time Synchronization cluster on this device; do not block TURNS/STUNS ICE servers.
    isNull = false;
    return CHIP_NO_ERROR;
}

// ---------------------------------------------------------------------------
// CameraAvSettingsUserLevelManagementDelegate
// ---------------------------------------------------------------------------

void ForwardingCamera::AvSettingsDelegate::ShutdownApp()
{
    DeviceLayer::SystemLayer().CancelTimer(OnPhysicalMoveTimer, this);
    mPtzCallback = nullptr;
}

bool ForwardingCamera::AvSettingsDelegate::CanChangeMPTZ()
{
    return true;
}

void ForwardingCamera::AvSettingsDelegate::VideoStreamAllocated(uint16_t aStreamID)
{
    VerifyOrReturn(mCamera.mAvSettingsCluster.IsConstructed() && mCamera.mAvStreamManagementCluster.IsConstructed());
    auto & avSettings = mCamera.mAvSettingsCluster.Cluster();

    // DPTZ streams are persisted by the cluster: do not add a stream twice.
    const auto dptzStreams = avSettings.GetLogic().GetDptzRelativeMove();
    VerifyOrReturn(std::none_of(dptzStreams.begin(), dptzStreams.end(),
                                [&](const auto & entry) { return entry.videoStreamID == aStreamID; }));

    avSettings.AddMoveCapableVideoStream(aStreamID, mCamera.mAvStreamManagementCluster.Cluster().GetViewport());
}

void ForwardingCamera::AvSettingsDelegate::VideoStreamDeallocated(uint16_t aStreamID)
{
    VerifyOrReturn(mCamera.mAvSettingsCluster.IsConstructed());
    mCamera.mAvSettingsCluster.Cluster().RemoveMoveCapableVideoStream(aStreamID);
}

void ForwardingCamera::AvSettingsDelegate::DefaultViewportUpdated(ViewportStruct aViewport)
{
    VerifyOrReturn(mCamera.mAvSettingsCluster.IsConstructed());
    mCamera.mAvSettingsCluster.Cluster().UpdateMoveCapableVideoStreams(aViewport);
}

Status ForwardingCamera::AvSettingsDelegate::ForwardPhysicalMove(CommandId commandId, Optional<int16_t> aPan,
                                                                 Optional<int16_t> aTilt, Optional<uint8_t> aZoom,
                                                                 PhysicalPTZCallback * callback)
{
    Status status = RequestHost(mCamera.mEndpointId, Avsum::Id, commandId, MakeMptz(aPan, aTilt, aZoom)).value_or(Status::Failure);
    VerifyOrReturnValue(status == Status::Success, status);

    // The cluster updates MPTZPosition once the movement completion is reported.
    mPtzCallback = callback;
    DeviceLayer::SystemLayer().CancelTimer(OnPhysicalMoveTimer, this);
    ReturnValueOnFailure(DeviceLayer::SystemLayer().StartTimer(kPhysicalMoveDuration, OnPhysicalMoveTimer, this),
                         Status::Failure);
    return Status::Success;
}

void ForwardingCamera::AvSettingsDelegate::OnPhysicalMoveTimer(System::Layer *, void * context)
{
    auto * self = static_cast<AvSettingsDelegate *>(context);
    if (self->mPtzCallback != nullptr && self->mCamera.mAvSettingsCluster.IsConstructed())
    {
        PhysicalPTZCallback * callback = self->mPtzCallback;
        self->mPtzCallback             = nullptr;
        callback->OnPhysicalMovementComplete(Status::Success);
    }
}

Status ForwardingCamera::AvSettingsDelegate::MPTZSetPosition(Optional<int16_t> aPan, Optional<int16_t> aTilt,
                                                             Optional<uint8_t> aZoom, PhysicalPTZCallback * callback)
{
    return ForwardPhysicalMove(Avsum::Commands::MPTZSetPosition::Id, aPan, aTilt, aZoom, callback);
}

Status ForwardingCamera::AvSettingsDelegate::MPTZRelativeMove(Optional<int16_t> aPan, Optional<int16_t> aTilt,
                                                              Optional<uint8_t> aZoom, PhysicalPTZCallback * callback)
{
    // The cluster already resolved the relative move into the absolute target position.
    return ForwardPhysicalMove(Avsum::Commands::MPTZRelativeMove::Id, aPan, aTilt, aZoom, callback);
}

Status ForwardingCamera::AvSettingsDelegate::MPTZMoveToPreset(uint8_t aPreset, Optional<int16_t> aPan, Optional<int16_t> aTilt,
                                                              Optional<uint8_t> aZoom, PhysicalPTZCallback * callback)
{
    Avsum::Commands::MPTZMoveToPreset::Type request;
    request.presetID = aPreset;
    Status status = RequestHost(mCamera.mEndpointId, Avsum::Id, Avsum::Commands::MPTZMoveToPreset::Id, request,
                                MakeMptz(aPan, aTilt, aZoom))
                        .value_or(Status::Failure);
    VerifyOrReturnValue(status == Status::Success, status);

    mPtzCallback = callback;
    DeviceLayer::SystemLayer().CancelTimer(OnPhysicalMoveTimer, this);
    ReturnValueOnFailure(DeviceLayer::SystemLayer().StartTimer(kPhysicalMoveDuration, OnPhysicalMoveTimer, this),
                         Status::Failure);
    return Status::Success;
}

Status ForwardingCamera::AvSettingsDelegate::MPTZSavePreset(uint8_t aPreset)
{
    // Presets are stored on the SoC; the host is informed for its own bookkeeping.
    Avsum::Commands::MPTZRemovePreset::Type preset;
    preset.presetID = aPreset;
    return RequestHost(mCamera.mEndpointId, Avsum::Id, Avsum::Commands::MPTZSavePreset::Id, preset).value_or(Status::Failure);
}

Status ForwardingCamera::AvSettingsDelegate::MPTZRemovePreset(uint8_t aPreset)
{
    Avsum::Commands::MPTZRemovePreset::Type request;
    request.presetID = aPreset;
    return RequestHost(mCamera.mEndpointId, Avsum::Id, Avsum::Commands::MPTZRemovePreset::Id, request).value_or(Status::Failure);
}

Status ForwardingCamera::AvSettingsDelegate::DPTZSetViewport(uint16_t aVideoStreamID, ViewportStruct aViewport)
{
    const auto & streams = mCamera.GetAllocatedVideoStreams();
    auto stream          = std::find_if(streams.begin(), streams.end(),
                                        [&](const VideoStreamStruct & s) { return s.videoStreamID == aVideoStreamID; });
    VerifyOrReturnValue(stream != streams.end(), Status::NotFound);

    VerifyOrReturnValue(aViewport.x2 > aViewport.x1 && aViewport.y2 > aViewport.y1, Status::ConstraintError);
    const uint16_t width  = static_cast<uint16_t>(aViewport.x2 - aViewport.x1);
    const uint16_t height = static_cast<uint16_t>(aViewport.y2 - aViewport.y1);
    VerifyOrReturnValue(width >= kMinViewportWidth && height >= kMinViewportHeight && width <= kSensorWidth &&
                            height <= kSensorHeight,
                        Status::ConstraintError);

    // The viewport aspect ratio must match the stream one (compared at 2 decimal places).
    VerifyOrReturnValue(stream->maxResolution.height != 0, Status::ConstraintError);
    const float requestedRatio = std::floor((static_cast<float>(width) / height) * 100) / 100;
    const float streamRatio =
        std::floor((static_cast<float>(stream->maxResolution.width) / stream->maxResolution.height) * 100) / 100;
    VerifyOrReturnValue(requestedRatio == streamRatio, Status::ConstraintError);

    Avsum::Commands::DPTZSetViewport::Type request;
    request.videoStreamID = aVideoStreamID;
    request.viewport      = aViewport;
    return RequestHost(mCamera.mEndpointId, Avsum::Id, Avsum::Commands::DPTZSetViewport::Id, request).value_or(Status::Failure);
}

Status ForwardingCamera::AvSettingsDelegate::DPTZRelativeMove(uint16_t aVideoStreamID, Optional<int16_t> aDeltaX,
                                                              Optional<int16_t> aDeltaY, Optional<int8_t> aZoomDelta,
                                                              ViewportStruct & aViewport)
{
    VerifyOrReturnValue(mCamera.mAvSettingsCluster.IsConstructed(), Status::Failure);
    const auto dptzStreams = mCamera.mAvSettingsCluster.Cluster().GetLogic().GetDptzRelativeMove();
    auto entry             = std::find_if(dptzStreams.begin(), dptzStreams.end(),
                                          [&](const auto & e) { return e.videoStreamID == aVideoStreamID; });
    VerifyOrReturnValue(entry != dptzStreams.end(), Status::NotFound);

    // Resolve the new viewport on the SoC, which holds the current one.
    int32_t x1 = entry->viewport.x1;
    int32_t y1 = entry->viewport.y1;
    int32_t x2 = entry->viewport.x2;
    int32_t y2 = entry->viewport.y2;

    if (aDeltaX.HasValue())
    {
        const int32_t dx = std::max<int32_t>(aDeltaX.Value(), -x1);
        x1 += dx;
        x2 += dx;
    }
    if (aDeltaY.HasValue())
    {
        const int32_t dy = std::max<int32_t>(aDeltaY.Value(), -y1);
        y1 += dy;
        y2 += dy;
    }
    if (aZoomDelta.HasValue() && aZoomDelta.Value() != 0 && x2 > x1 && y2 > y1)
    {
        // Scale the viewport area by zoomDelta percent, keeping its aspect ratio and center.
        const double width  = x2 - x1;
        const double height = y2 - y1;
        const double scale  = std::sqrt(std::max(0.0, 1.0 + aZoomDelta.Value() / 100.0));
        const double cx     = x1 + width / 2;
        const double cy     = y1 + height / 2;
        x1                  = static_cast<int32_t>(std::lround(cx - width * scale / 2));
        x2                  = static_cast<int32_t>(std::lround(cx + width * scale / 2));
        y1                  = static_cast<int32_t>(std::lround(cy - height * scale / 2));
        y2                  = static_cast<int32_t>(std::lround(cy + height * scale / 2));
    }

    // Keep the viewport between the minimum viewport and the sensor size.
    if ((x2 - x1) < kMinViewportWidth || (y2 - y1) < kMinViewportHeight)
    {
        x2 = x1 + kMinViewportWidth;
        y2 = y1 + kMinViewportHeight;
    }
    if (x1 < 0 || y1 < 0 || x2 > kSensorWidth || y2 > kSensorHeight || (x2 - x1) > kSensorWidth || (y2 - y1) > kSensorHeight)
    {
        x1 = 0;
        y1 = 0;
        x2 = kSensorWidth;
        y2 = kSensorHeight;
    }

    aViewport.x1 = static_cast<uint16_t>(x1);
    aViewport.y1 = static_cast<uint16_t>(y1);
    aViewport.x2 = static_cast<uint16_t>(x2);
    aViewport.y2 = static_cast<uint16_t>(y2);

    Avsum::Structs::DPTZStruct::Type dptz;
    dptz.videoStreamID = aVideoStreamID;
    dptz.viewport      = aViewport;
    return RequestHost(mCamera.mEndpointId, Avsum::Id, Avsum::Commands::DPTZRelativeMove::Id, dptz).value_or(Status::Failure);
}

CHIP_ERROR ForwardingCamera::AvSettingsDelegate::PersistentAttributesLoadedCallback()
{
    ChipLogProgress(Camera, "ForwardingCamera: MPTZ presets and DPTZ streams loaded");
    return CHIP_NO_ERROR;
}

} // namespace app
} // namespace chip
