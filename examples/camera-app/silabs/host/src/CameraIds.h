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

/**
 * Matter identifiers and TLV field tags of the camera clusters, as forwarded by the SoC
 * (see examples/camera-app/silabs/src/ForwardingCamera.cpp). Values come from the Matter
 * specification / zzz_generated/app-common.
 */

#pragma once

#include <cstdint>

namespace camera {

// Interaction Model status codes.
enum class Status : uint8_t
{
    kSuccess                = 0x00,
    kFailure                = 0x01,
    kUnsupportedEndpoint    = 0x7F,
    kUnsupportedCommand     = 0x81,
    kInvalidCommand         = 0x85,
    kUnsupportedAttribute   = 0x86,
    kConstraintError        = 0x87,
    kResourceExhausted      = 0x89,
    kNotFound               = 0x8B,
    kBusy                   = 0x9C,
    kUnsupportedCluster     = 0xC3,
    kInvalidInState         = 0xCB,
    kDynamicConstraintError = 0xCF,
};

namespace ClusterId {
constexpr uint32_t kCameraAvStreamManagement            = 0x0551;
constexpr uint32_t kCameraAvSettingsUserLevelManagement = 0x0552;
constexpr uint32_t kWebRTCTransportProvider             = 0x0553;
constexpr uint32_t kWebRTCTransportRequestor            = 0x0554;
} // namespace ClusterId

namespace AvStreamManagement {

namespace Commands {
constexpr uint32_t kAudioStreamAllocate      = 0x00;
constexpr uint32_t kAudioStreamDeallocate    = 0x02;
constexpr uint32_t kVideoStreamAllocate      = 0x03;
constexpr uint32_t kVideoStreamModify        = 0x05;
constexpr uint32_t kVideoStreamDeallocate    = 0x06;
constexpr uint32_t kSnapshotStreamAllocate   = 0x07;
constexpr uint32_t kSnapshotStreamModify     = 0x09;
constexpr uint32_t kSnapshotStreamDeallocate = 0x0A;
constexpr uint32_t kSetStreamPriorities      = 0x0B;
constexpr uint32_t kCaptureSnapshot          = 0x0C;
} // namespace Commands

namespace Attributes {
constexpr uint32_t kHDRModeEnabled                   = 0x0D;
constexpr uint32_t kSoftRecordingPrivacyModeEnabled  = 0x13;
constexpr uint32_t kSoftLivestreamPrivacyModeEnabled = 0x14;
constexpr uint32_t kHardPrivacyModeOn                = 0x15;
constexpr uint32_t kNightVision                      = 0x16;
constexpr uint32_t kNightVisionIllum                 = 0x17;
constexpr uint32_t kViewport                         = 0x18;
constexpr uint32_t kSpeakerMuted                     = 0x19;
constexpr uint32_t kSpeakerVolumeLevel               = 0x1A;
constexpr uint32_t kMicrophoneMuted                  = 0x1D;
constexpr uint32_t kMicrophoneVolumeLevel            = 0x1E;
constexpr uint32_t kMicrophoneAGCEnabled             = 0x21;
constexpr uint32_t kImageRotation                    = 0x22;
constexpr uint32_t kImageFlipHorizontal              = 0x23;
constexpr uint32_t kImageFlipVertical                = 0x24;
constexpr uint32_t kLocalVideoRecordingEnabled       = 0x25;
constexpr uint32_t kLocalSnapshotRecordingEnabled    = 0x26;
constexpr uint32_t kStatusLightEnabled               = 0x27;
constexpr uint32_t kStatusLightBrightness            = 0x28;
} // namespace Attributes

namespace VideoStreamStruct {
constexpr uint8_t kVideoStreamID    = 0;
constexpr uint8_t kStreamUsage      = 1;
constexpr uint8_t kVideoCodec       = 2;
constexpr uint8_t kMinFrameRate     = 3;
constexpr uint8_t kMaxFrameRate     = 4;
constexpr uint8_t kMinResolution    = 5;
constexpr uint8_t kMaxResolution    = 6;
constexpr uint8_t kMinBitRate       = 7;
constexpr uint8_t kMaxBitRate       = 8;
constexpr uint8_t kKeyFrameInterval = 9;
constexpr uint8_t kWatermarkEnabled = 10;
constexpr uint8_t kOSDEnabled       = 11;
} // namespace VideoStreamStruct

namespace AudioStreamStruct {
constexpr uint8_t kAudioStreamID = 0;
constexpr uint8_t kStreamUsage   = 1;
constexpr uint8_t kAudioCodec    = 2;
constexpr uint8_t kChannelCount  = 3;
constexpr uint8_t kSampleRate    = 4;
constexpr uint8_t kBitRate       = 5;
constexpr uint8_t kBitDepth      = 6;
} // namespace AudioStreamStruct

namespace SnapshotStreamStruct {
constexpr uint8_t kSnapshotStreamID = 0;
constexpr uint8_t kImageCodec       = 1;
constexpr uint8_t kFrameRate        = 2;
constexpr uint8_t kMinResolution    = 3;
constexpr uint8_t kMaxResolution    = 4;
constexpr uint8_t kQuality          = 5;
constexpr uint8_t kWatermarkEnabled = 9;
constexpr uint8_t kOSDEnabled       = 10;
} // namespace SnapshotStreamStruct

namespace VideoResolutionStruct {
constexpr uint8_t kWidth  = 0;
constexpr uint8_t kHeight = 1;
} // namespace VideoResolutionStruct

namespace CaptureSnapshotRequest {
constexpr uint8_t kSnapshotStreamID    = 0; // Nullable
constexpr uint8_t kRequestedResolution = 1;
} // namespace CaptureSnapshotRequest

namespace CaptureSnapshotResponse {
constexpr uint8_t kData       = 0;
constexpr uint8_t kImageCodec = 1;
constexpr uint8_t kResolution = 2;
} // namespace CaptureSnapshotResponse

// VideoStreamModify / VideoStreamDeallocate / AudioStreamDeallocate / SnapshotStreamModify /
// SnapshotStreamDeallocate: the stream ID is field 0, watermarkEnabled 1, OSDEnabled 2.
namespace StreamRequest {
constexpr uint8_t kStreamID         = 0;
constexpr uint8_t kWatermarkEnabled = 1;
constexpr uint8_t kOSDEnabled       = 2;
} // namespace StreamRequest

namespace SetStreamPrioritiesRequest {
constexpr uint8_t kStreamPriorities = 0;
} // namespace SetStreamPrioritiesRequest

enum class VideoCodec : uint8_t
{
    kH264 = 0x00,
    kHevc = 0x01,
    kVvc  = 0x02,
    kAv1  = 0x03,
};

enum class AudioCodec : uint8_t
{
    kOpus  = 0x00,
    kAacLc = 0x01,
};

enum class ImageCodec : uint8_t
{
    kJpeg = 0x00,
    kHeic = 0x01,
};

enum class TriStateAuto : uint8_t
{
    kOff  = 0x00,
    kOn   = 0x01,
    kAuto = 0x02,
};

} // namespace AvStreamManagement

namespace ViewportStruct {
constexpr uint8_t kX1 = 0;
constexpr uint8_t kY1 = 1;
constexpr uint8_t kX2 = 2;
constexpr uint8_t kY2 = 3;
} // namespace ViewportStruct

namespace WebRTC {

namespace ProviderCommands {
constexpr uint32_t kSolicitOffer         = 0x00;
constexpr uint32_t kProvideOffer         = 0x02;
constexpr uint32_t kProvideAnswer        = 0x04;
constexpr uint32_t kProvideICECandidates = 0x05;
constexpr uint32_t kEndSession           = 0x06;
} // namespace ProviderCommands

namespace RequestorCommands {
constexpr uint32_t kOffer         = 0x00;
constexpr uint32_t kAnswer        = 0x01;
constexpr uint32_t kICECandidates = 0x02;
constexpr uint32_t kEnd           = 0x03;
} // namespace RequestorCommands

namespace SessionStruct {
constexpr uint8_t kId             = 0;
constexpr uint8_t kPeerNodeID     = 1;
constexpr uint8_t kPeerEndpointID = 2;
constexpr uint8_t kStreamUsage    = 3;
constexpr uint8_t kVideoStreamID  = 4;
constexpr uint8_t kAudioStreamID  = 5;
constexpr uint8_t kVideoStreams   = 7;
constexpr uint8_t kAudioStreams   = 8;
} // namespace SessionStruct

namespace SolicitOfferRequest {
constexpr uint8_t kICEServers         = 4;
constexpr uint8_t kICETransportPolicy = 5;
} // namespace SolicitOfferRequest

namespace ProvideOfferRequest {
constexpr uint8_t kSdp                = 1;
constexpr uint8_t kICEServers         = 6;
constexpr uint8_t kICETransportPolicy = 7;
} // namespace ProvideOfferRequest

// ProvideAnswer / ProvideICECandidates / EndSession requests, and all the requestor commands:
// the session ID is field 0, the payload (sdp, ICE candidates, reason) field 1.
namespace SessionRequest {
constexpr uint8_t kSessionID = 0;
constexpr uint8_t kPayload   = 1;
} // namespace SessionRequest

namespace ICEServerStruct {
constexpr uint8_t kURLs       = 0;
constexpr uint8_t kUsername   = 1;
constexpr uint8_t kCredential = 2;
} // namespace ICEServerStruct

namespace ICECandidateStruct {
constexpr uint8_t kCandidate     = 0;
constexpr uint8_t kSDPMid        = 1;
constexpr uint8_t kSDPMLineIndex = 2;
} // namespace ICECandidateStruct

enum class EndReason : uint8_t
{
    kICEFailed      = 0x00,
    kICETimeout     = 0x01,
    kUserHangup     = 0x02,
    kOutOfResources = 0x08,
    kPrivacyMode    = 0x0B,
    kUnknownReason  = 0x0C,
};

} // namespace WebRTC

namespace AvSettings {

namespace Commands {
constexpr uint32_t kMPTZSetPosition  = 0x00;
constexpr uint32_t kMPTZRelativeMove = 0x01;
constexpr uint32_t kMPTZMoveToPreset = 0x02;
constexpr uint32_t kMPTZSavePreset   = 0x03;
constexpr uint32_t kMPTZRemovePreset = 0x04;
constexpr uint32_t kDPTZSetViewport  = 0x05;
constexpr uint32_t kDPTZRelativeMove = 0x06;
} // namespace Commands

// DPTZSetViewport request and DPTZStruct share the same layout.
namespace DPTZ {
constexpr uint8_t kVideoStreamID = 0;
constexpr uint8_t kViewport      = 1;
} // namespace DPTZ

} // namespace AvSettings

} // namespace camera
