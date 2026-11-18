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

#include "CameraHost.h"

#include "CameraDevice.h"
#include "Log.h"
#include "WebRtcManager.h"

#include <algorithm>

using namespace camera;

namespace {

namespace Avsm = camera::AvStreamManagement;

// Sample rates supported by the Opus encoder.
constexpr uint32_t kOpusSampleRates[] = { 8000, 12000, 16000, 24000, 48000 };
constexpr uint32_t kMinOpusBitRate    = 6000;
constexpr uint32_t kMaxOpusBitRate    = 510000;
constexpr uint32_t kDefaultVideoKbps  = 2000;

const tlv::Element * StructureAt(const std::vector<tlv::Element> & params, size_t index)
{
    return (index < params.size() && params[index].type == tlv::Type::kStructure) ? &params[index] : nullptr;
}

bool ReadResolution(const tlv::Element * structure, uint8_t tag, uint16_t & width, uint16_t & height)
{
    const tlv::Element * resolution = structure ? structure->Field(tag) : nullptr;
    if (resolution == nullptr)
    {
        return false;
    }
    auto w = resolution->UInt(Avsm::VideoResolutionStruct::kWidth);
    auto h = resolution->UInt(Avsm::VideoResolutionStruct::kHeight);
    if (!w || !h)
    {
        return false;
    }
    width  = static_cast<uint16_t>(*w);
    height = static_cast<uint16_t>(*h);
    return true;
}

std::optional<Viewport> ReadViewport(const tlv::Element * element)
{
    if (element == nullptr)
    {
        return std::nullopt;
    }
    auto x1 = element->UInt(ViewportStruct::kX1);
    auto y1 = element->UInt(ViewportStruct::kY1);
    auto x2 = element->UInt(ViewportStruct::kX2);
    auto y2 = element->UInt(ViewportStruct::kY2);
    if (!x1 || !y1 || !x2 || !y2)
    {
        return std::nullopt;
    }
    return Viewport{ static_cast<uint16_t>(*x1), static_cast<uint16_t>(*y1), static_cast<uint16_t>(*x2),
                     static_cast<uint16_t>(*y2) };
}

std::vector<uint16_t> ReadStreamIds(const tlv::Element & session, uint8_t listTag, uint8_t idTag)
{
    std::vector<uint16_t> ids;
    const tlv::Element * list = session.Field(listTag);
    if (list != nullptr && list->IsContainer())
    {
        for (const auto & item : list->children)
        {
            if (auto id = item.AsUInt())
            {
                ids.push_back(static_cast<uint16_t>(*id));
            }
        }
    }
    else if (auto id = session.UInt(idTag))
    {
        // Deprecated single stream field, null when absent.
        ids.push_back(static_cast<uint16_t>(*id));
    }
    return ids;
}

std::vector<IceServer> ReadIceServers(const tlv::Element * request, uint8_t tag)
{
    std::vector<IceServer> servers;
    const tlv::Element * list = request ? request->Field(tag) : nullptr;
    if (list == nullptr || !list->IsContainer())
    {
        return servers;
    }
    for (const auto & item : list->children)
    {
        IceServer server;
        const tlv::Element * urls = item.Field(WebRTC::ICEServerStruct::kURLs);
        if (urls != nullptr)
        {
            for (const auto & url : urls->children)
            {
                if (auto value = url.AsString())
                {
                    server.urls.push_back(*value);
                }
            }
        }
        server.username   = item.String(WebRTC::ICEServerStruct::kUsername).value_or("");
        server.credential = item.String(WebRTC::ICEServerStruct::kCredential).value_or("");
        if (!server.urls.empty())
        {
            servers.push_back(server);
        }
    }
    return servers;
}

void NotSupported(const char * what)
{
    LogError("%s not supported by the camera: only stored on the SoC", what);
}

} // namespace

CameraHost::CameraHost(MmicLink & link, CameraDevice & camera, WebRtcManager & webrtc) :
    mLink(link), mCamera(camera), mWebRtc(webrtc)
{}

void CameraHost::OnCommand(const MmicLink::ClusterCommand & command)
{
    std::vector<tlv::Element> params;
    Status status = Status::kInvalidCommand;
    if (!tlv::Parse(command.params.data(), command.params.size(), params))
    {
        LogError("Malformed TLV parameters");
    }
    else if (command.clusterId == ClusterId::kCameraAvStreamManagement)
    {
        status = HandleAvStreamManagement(command.commandId, params);
    }
    else if (command.clusterId == ClusterId::kWebRTCTransportProvider)
    {
        status = HandleWebRtcProvider(command.endpointId, command.commandId, params);
    }
    else if (command.clusterId == ClusterId::kCameraAvSettingsUserLevelManagement)
    {
        status = HandleAvSettings(command.commandId, params);
    }
    else
    {
        status = Status::kUnsupportedCluster;
    }

    if (status != Status::kSuccess)
    {
        LogError("Command 0x%02x of cluster 0x%04x failed: status 0x%02x", command.commandId, command.clusterId,
                 static_cast<unsigned>(status));
    }
    if (command.answerExpected)
    {
        mLink.SendAnswer(command.transactionId, static_cast<uint8_t>(status));
    }
}

Status CameraHost::HandleAvStreamManagement(uint32_t commandId, const std::vector<tlv::Element> & params)
{
    const tlv::Element * request = StructureAt(params, 0);
    if (request == nullptr)
    {
        return Status::kInvalidCommand;
    }

    switch (commandId)
    {
    case Avsm::Commands::kVideoStreamAllocate:
        return VideoStreamAllocate(*request);
    case Avsm::Commands::kAudioStreamAllocate:
        return AudioStreamAllocate(*request);
    case Avsm::Commands::kSnapshotStreamAllocate:
        return SnapshotStreamAllocate(*request);

    case Avsm::Commands::kVideoStreamModify:
    case Avsm::Commands::kSnapshotStreamModify:
        // Watermark and OSD are the only modifiable settings.
        if (request->Bool(Avsm::StreamRequest::kWatermarkEnabled).value_or(false) ||
            request->Bool(Avsm::StreamRequest::kOSDEnabled).value_or(false))
        {
            LogError("Watermark and OSD are not supported");
            return Status::kConstraintError;
        }
        return Status::kSuccess;

    case Avsm::Commands::kVideoStreamDeallocate: {
        auto id = request->UInt(Avsm::StreamRequest::kStreamID);
        if (!id)
        {
            return Status::kInvalidCommand;
        }
        // Unknown streams (e.g. after a host restart) are already released.
        mCamera.StopVideoStream(static_cast<uint16_t>(*id));
        return Status::kSuccess;
    }
    case Avsm::Commands::kAudioStreamDeallocate: {
        auto id = request->UInt(Avsm::StreamRequest::kStreamID);
        if (!id)
        {
            return Status::kInvalidCommand;
        }
        mCamera.StopAudioStream(static_cast<uint16_t>(*id));
        return Status::kSuccess;
    }
    case Avsm::Commands::kSnapshotStreamDeallocate: {
        auto id = request->UInt(Avsm::StreamRequest::kStreamID);
        if (!id)
        {
            return Status::kInvalidCommand;
        }
        mSnapshotStreams.erase(static_cast<uint16_t>(*id));
        return Status::kSuccess;
    }

    case Avsm::Commands::kSetStreamPriorities:
        // Only one stream of each type runs at a time: priorities do not change the behavior.
        return Status::kSuccess;

    case Avsm::Commands::kCaptureSnapshot:
        // TODO: transfer the image outside of MMIC (large payload).
        LogError("CaptureSnapshot is not implemented yet");
        return Status::kUnsupportedCommand;

    default:
        return Status::kUnsupportedCommand;
    }
}

Status CameraHost::VideoStreamAllocate(const tlv::Element & stream)
{
    namespace Fields = Avsm::VideoStreamStruct;

    auto id         = stream.UInt(Fields::kVideoStreamID);
    auto codec      = stream.UInt(Fields::kVideoCodec);
    auto minFps     = stream.UInt(Fields::kMinFrameRate);
    auto maxFps     = stream.UInt(Fields::kMaxFrameRate);
    auto minBitRate = stream.UInt(Fields::kMinBitRate);
    auto maxBitRate = stream.UInt(Fields::kMaxBitRate);
    auto keyFrameMs = stream.UInt(Fields::kKeyFrameInterval);
    uint16_t minWidth = 0, minHeight = 0, maxWidth = 0, maxHeight = 0;
    if (!id || !codec || !minFps || !maxFps || !minBitRate || !maxBitRate || !keyFrameMs ||
        !ReadResolution(&stream, Fields::kMinResolution, minWidth, minHeight) ||
        !ReadResolution(&stream, Fields::kMaxResolution, maxWidth, maxHeight) || maxWidth == 0 || maxHeight == 0)
    {
        return Status::kInvalidCommand;
    }

    if (*codec != static_cast<uint64_t>(Avsm::VideoCodec::kH264))
    {
        LogError("Video codec %u not supported, H.264 only", static_cast<unsigned>(*codec));
        return Status::kConstraintError;
    }
    if (stream.Bool(Fields::kWatermarkEnabled).value_or(false) || stream.Bool(Fields::kOSDEnabled).value_or(false))
    {
        LogError("Watermark and OSD are not supported");
        return Status::kConstraintError;
    }

    // Largest resolution of the requested range the encoder supports, keeping the aspect ratio.
    double scale = std::min({ 1.0, static_cast<double>(CameraDevice::kMaxEncodeWidth) / maxWidth,
                              static_cast<double>(CameraDevice::kMaxEncodeHeight) / maxHeight });
    const uint16_t width  = static_cast<uint16_t>(static_cast<uint32_t>(maxWidth * scale) & ~1u);
    const uint16_t height = static_cast<uint16_t>(static_cast<uint32_t>(maxHeight * scale) & ~1u);
    if (width < minWidth || height < minHeight)
    {
        LogError("Video stream %ux%u-%ux%u rejected: the camera encodes up to %ux%u", minWidth, minHeight, maxWidth, maxHeight,
                 CameraDevice::kMaxEncodeWidth, CameraDevice::kMaxEncodeHeight);
        return Status::kResourceExhausted;
    }

    const uint16_t frameRate = static_cast<uint16_t>(std::min<uint64_t>(*maxFps, CameraDevice::kMaxFrameRate));
    if (frameRate == 0 || frameRate < *minFps)
    {
        LogError("Video stream %u-%u fps rejected: the camera captures up to %u fps", static_cast<unsigned>(*minFps),
                 static_cast<unsigned>(*maxFps), CameraDevice::kMaxFrameRate);
        return Status::kResourceExhausted;
    }

    VideoStreamConfig config;
    config.id             = static_cast<uint16_t>(*id);
    config.width          = width;
    config.height         = height;
    config.frameRate      = frameRate;
    config.bitRateKbps    = static_cast<uint32_t>(std::max<uint64_t>(*maxBitRate / 1000, 1));
    config.keyFrameFrames = static_cast<uint32_t>(std::max<uint64_t>(frameRate * *keyFrameMs / 1000, 1));
    if (*maxBitRate == 0)
    {
        config.bitRateKbps = kDefaultVideoKbps;
    }
    return mCamera.StartVideoStream(config);
}

Status CameraHost::AudioStreamAllocate(const tlv::Element & stream)
{
    namespace Fields = Avsm::AudioStreamStruct;

    auto id         = stream.UInt(Fields::kAudioStreamID);
    auto codec      = stream.UInt(Fields::kAudioCodec);
    auto channels   = stream.UInt(Fields::kChannelCount);
    auto sampleRate = stream.UInt(Fields::kSampleRate);
    auto bitRate    = stream.UInt(Fields::kBitRate);
    if (!id || !codec || !channels || !sampleRate || !bitRate)
    {
        return Status::kInvalidCommand;
    }

    if (*codec != static_cast<uint64_t>(Avsm::AudioCodec::kOpus))
    {
        LogError("Audio codec %u not supported, Opus only", static_cast<unsigned>(*codec));
        return Status::kConstraintError;
    }
    if (*channels < 1 || *channels > 2 ||
        std::find(std::begin(kOpusSampleRates), std::end(kOpusSampleRates), *sampleRate) == std::end(kOpusSampleRates))
    {
        LogError("Audio stream %u channel(s) @ %u Hz not supported", static_cast<unsigned>(*channels),
                 static_cast<unsigned>(*sampleRate));
        return Status::kConstraintError;
    }

    AudioStreamConfig config;
    config.id         = static_cast<uint16_t>(*id);
    config.channels   = static_cast<uint8_t>(*channels);
    config.sampleRate = static_cast<uint32_t>(*sampleRate);
    config.bitRate    = static_cast<uint32_t>(std::clamp<uint64_t>(*bitRate, kMinOpusBitRate, kMaxOpusBitRate));
    return mCamera.StartAudioStream(config);
}

Status CameraHost::SnapshotStreamAllocate(const tlv::Element & stream)
{
    namespace Fields = Avsm::SnapshotStreamStruct;

    auto id    = stream.UInt(Fields::kSnapshotStreamID);
    auto codec = stream.UInt(Fields::kImageCodec);
    if (!id || !codec)
    {
        return Status::kInvalidCommand;
    }
    if (*codec != static_cast<uint64_t>(Avsm::ImageCodec::kJpeg))
    {
        LogError("Image codec %u not supported, JPEG only", static_cast<unsigned>(*codec));
        return Status::kConstraintError;
    }
    // Tracked only, capture is not implemented yet.
    mSnapshotStreams[static_cast<uint16_t>(*id)] = static_cast<uint8_t>(stream.UInt(Fields::kQuality).value_or(0));
    return Status::kSuccess;
}

Status CameraHost::HandleWebRtcProvider(uint16_t endpointId, uint32_t commandId, const std::vector<tlv::Element> & params)
{
    const tlv::Element * first = StructureAt(params, 0);
    if (first == nullptr)
    {
        return Status::kInvalidCommand;
    }

    switch (commandId)
    {
    case WebRTC::ProviderCommands::kSolicitOffer:
    case WebRTC::ProviderCommands::kProvideOffer: {
        // WebRTCSessionStruct created on the SoC, then the request.
        const tlv::Element * request = StructureAt(params, 1);
        auto sessionId               = first->UInt(WebRTC::SessionStruct::kId);
        if (request == nullptr || !sessionId)
        {
            return Status::kInvalidCommand;
        }

        const bool solicit = (commandId == WebRTC::ProviderCommands::kSolicitOffer);
        SessionParams session;
        session.sessionId    = static_cast<uint16_t>(*sessionId);
        session.endpointId   = endpointId;
        session.videoStreams = ReadStreamIds(*first, WebRTC::SessionStruct::kVideoStreams, WebRTC::SessionStruct::kVideoStreamID);
        session.audioStreams = ReadStreamIds(*first, WebRTC::SessionStruct::kAudioStreams, WebRTC::SessionStruct::kAudioStreamID);
        session.iceServers =
            ReadIceServers(request, solicit ? WebRTC::SolicitOfferRequest::kICEServers : WebRTC::ProvideOfferRequest::kICEServers);
        session.iceTransportPolicy =
            request
                ->String(solicit ? WebRTC::SolicitOfferRequest::kICETransportPolicy : WebRTC::ProvideOfferRequest::kICETransportPolicy)
                .value_or("");
        if (solicit)
        {
            return mWebRtc.SolicitOffer(session);
        }
        auto sdp = request->String(WebRTC::ProvideOfferRequest::kSdp);
        return sdp ? mWebRtc.ProvideOffer(session, *sdp) : Status::kInvalidCommand;
    }

    case WebRTC::ProviderCommands::kProvideAnswer: {
        auto sessionId = first->UInt(WebRTC::SessionRequest::kSessionID);
        auto sdp       = first->String(WebRTC::SessionRequest::kPayload);
        if (!sessionId || !sdp)
        {
            return Status::kInvalidCommand;
        }
        return mWebRtc.ProvideAnswer(static_cast<uint16_t>(*sessionId), *sdp);
    }

    case WebRTC::ProviderCommands::kProvideICECandidates: {
        auto sessionId           = first->UInt(WebRTC::SessionRequest::kSessionID);
        const tlv::Element * list = first->Field(WebRTC::SessionRequest::kPayload);
        if (!sessionId || list == nullptr || !list->IsContainer())
        {
            return Status::kInvalidCommand;
        }
        std::vector<IceCandidate> candidates;
        for (const auto & item : list->children)
        {
            auto candidate = item.String(WebRTC::ICECandidateStruct::kCandidate);
            if (candidate)
            {
                // SDPMid is nullable: String() is empty for null.
                candidates.push_back({ *candidate, item.String(WebRTC::ICECandidateStruct::kSDPMid).value_or("") });
            }
        }
        return mWebRtc.ProvideIceCandidates(static_cast<uint16_t>(*sessionId), candidates);
    }

    case WebRTC::ProviderCommands::kEndSession: {
        auto sessionId = first->UInt(WebRTC::SessionRequest::kSessionID);
        if (!sessionId)
        {
            return Status::kInvalidCommand;
        }
        return mWebRtc.EndSession(static_cast<uint16_t>(*sessionId));
    }

    default:
        return Status::kUnsupportedCommand;
    }
}

Status CameraHost::HandleAvSettings(uint32_t commandId, const std::vector<tlv::Element> & params)
{
    switch (commandId)
    {
    case AvSettings::Commands::kMPTZSetPosition:
    case AvSettings::Commands::kMPTZRelativeMove:
    case AvSettings::Commands::kMPTZMoveToPreset:
    case AvSettings::Commands::kMPTZSavePreset:
    case AvSettings::Commands::kMPTZRemovePreset:
        LogError("The camera has no mechanical pan / tilt / zoom");
        return Status::kUnsupportedCommand;

    case AvSettings::Commands::kDPTZSetViewport:
    case AvSettings::Commands::kDPTZRelativeMove: {
        // DPTZSetViewport request and DPTZStruct (resolved viewport) share the same layout.
        const tlv::Element * request = StructureAt(params, 0);
        auto streamId                = request ? request->UInt(AvSettings::DPTZ::kVideoStreamID) : std::nullopt;
        auto viewport                = ReadViewport(request ? request->Field(AvSettings::DPTZ::kViewport) : nullptr);
        if (!streamId || !viewport)
        {
            return Status::kInvalidCommand;
        }
        return mCamera.SetStreamViewport(static_cast<uint16_t>(*streamId), *viewport);
    }

    default:
        return Status::kUnsupportedCommand;
    }
}

void CameraHost::OnAttributeChange(const MmicLink::AttributeChange & change)
{
    if (change.clusterId != ClusterId::kCameraAvStreamManagement)
    {
        return;
    }

    std::vector<tlv::Element> elements;
    if (!tlv::Parse(change.value.data(), change.value.size(), elements) || elements.empty())
    {
        LogInfo("Attribute 0x%04x changed, no value forwarded", change.attributeId);
        return;
    }
    const tlv::Element & value = elements.front();
    const bool enabled         = value.AsBool().value_or(false);

    namespace Attributes = Avsm::Attributes;
    Status status        = Status::kSuccess;
    switch (change.attributeId)
    {
    case Attributes::kSoftLivestreamPrivacyModeEnabled:
        if (enabled)
        {
            mWebRtc.EndAllSessions(WebRTC::EndReason::kPrivacyMode);
        }
        break;
    case Attributes::kHardPrivacyModeOn:
        mCamera.SetPrivacyMode(enabled);
        if (enabled)
        {
            mWebRtc.EndAllSessions(WebRTC::EndReason::kPrivacyMode);
        }
        break;
    case Attributes::kSoftRecordingPrivacyModeEnabled:
        // Nothing is recorded by the host.
        break;
    case Attributes::kViewport:
        if (auto viewport = ReadViewport(&value))
        {
            mCamera.SetDefaultViewport(*viewport);
        }
        break;
    case Attributes::kMicrophoneMuted:
        status = mCamera.SetMicrophoneMuted(enabled);
        break;
    case Attributes::kMicrophoneVolumeLevel:
        status = mCamera.SetMicrophoneVolume(static_cast<uint8_t>(value.AsUInt().value_or(0)));
        break;
    case Attributes::kImageRotation:
        status = mCamera.SetImageRotation(static_cast<uint16_t>(value.AsUInt().value_or(0)));
        break;
    case Attributes::kImageFlipHorizontal:
        status = mCamera.SetImageFlipHorizontal(enabled);
        break;
    case Attributes::kImageFlipVertical:
        status = mCamera.SetImageFlipVertical(enabled);
        break;
    case Attributes::kHDRModeEnabled:
        if (enabled)
        {
            NotSupported("HDR");
        }
        break;
    case Attributes::kNightVision:
    case Attributes::kNightVisionIllum:
        if (value.AsUInt().value_or(0) != static_cast<uint64_t>(Avsm::TriStateAuto::kOff))
        {
            NotSupported("Night vision");
        }
        break;
    case Attributes::kSpeakerMuted:
    case Attributes::kSpeakerVolumeLevel:
        NotSupported("Speaker");
        break;
    case Attributes::kMicrophoneAGCEnabled:
        if (enabled)
        {
            NotSupported("Microphone AGC");
        }
        break;
    case Attributes::kLocalVideoRecordingEnabled:
    case Attributes::kLocalSnapshotRecordingEnabled:
        if (enabled)
        {
            NotSupported("Local recording");
        }
        break;
    case Attributes::kStatusLightEnabled:
    case Attributes::kStatusLightBrightness:
        NotSupported("Status light");
        break;
    default:
        break;
    }

    if (status != Status::kSuccess)
    {
        LogError("Attribute 0x%04x not applied: status 0x%02x", change.attributeId, static_cast<unsigned>(status));
    }
}
