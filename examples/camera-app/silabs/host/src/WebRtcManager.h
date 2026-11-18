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
 * WebRTC sessions (libdatachannel). The Matter signaling is done by the SoC: the provider commands
 * are forwarded to the host, and the SDP / ICE candidates produced here are sent back to the SoC
 * as WebRTCTransportRequestor commands, which relays them to the requestor. The media flows
 * directly from the host network interface to the peer.
 */

#pragma once

#include "CameraIds.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace rtc {
class PeerConnection;
class Track;
} // namespace rtc

class CameraDevice;

struct IceServer
{
    std::vector<std::string> urls;
    std::string username;
    std::string credential;
};

struct IceCandidate
{
    std::string candidate;
    std::string mid;
};

struct SessionParams
{
    uint16_t sessionId  = 0;
    uint16_t endpointId = 0; // Camera endpoint on the SoC
    std::vector<uint16_t> videoStreams;
    std::vector<uint16_t> audioStreams;
    std::vector<IceServer> iceServers;
    std::string iceTransportPolicy;
};

class WebRtcManager
{
public:
    static constexpr size_t kMaxSessions = 4;

    // Sends a WebRTCTransportRequestor command (TLV request structure) to the SoC.
    using SignalingSender = std::function<void(uint16_t endpointId, uint32_t commandId, const std::vector<uint8_t> & params)>;

    WebRtcManager(CameraDevice & camera, SignalingSender sender, std::string bindAddress);
    ~WebRtcManager();

    camera::Status SolicitOffer(const SessionParams & params);
    camera::Status ProvideOffer(const SessionParams & params, const std::string & sdp);
    camera::Status ProvideAnswer(uint16_t sessionId, const std::string & sdp);
    camera::Status ProvideIceCandidates(uint16_t sessionId, const std::vector<IceCandidate> & candidates);
    camera::Status EndSession(uint16_t sessionId);

    // Ends every session and notifies the SoC (e.g. privacy mode enabled).
    void EndAllSessions(camera::WebRTC::EndReason reason);

    void OnVideoFrame(uint16_t streamId, const uint8_t * data, size_t size, std::chrono::microseconds timestamp);
    void OnAudioFrame(uint16_t streamId, const uint8_t * data, size_t size, std::chrono::microseconds timestamp);

private:
    struct Session;

    camera::Status CreateSession(const SessionParams & params, const std::string * remoteOffer);
    void OnLocalDescription(uint16_t sessionId, const std::string & sdp, bool offer);
    void OnLocalCandidate(uint16_t sessionId, const std::string & candidate, const std::string & mid);
    void OnGatheringComplete(uint16_t sessionId);
    void OnConnected(uint16_t sessionId);
    void OnClosed(uint16_t sessionId, camera::WebRTC::EndReason reason);
    void SendCandidates(uint16_t sessionId);
    void SendEnd(uint16_t endpointId, uint16_t sessionId, camera::WebRTC::EndReason reason);
    std::shared_ptr<Session> RemoveSession(uint16_t sessionId);
    void ReleaseRetired();

    CameraDevice & mCamera;
    SignalingSender mSender;
    std::string mBindAddress;

    std::mutex mMutex;
    std::map<uint16_t, std::shared_ptr<Session>> mSessions;
    // Peer connections closed from a libdatachannel callback, released later from another thread.
    std::vector<std::shared_ptr<rtc::PeerConnection>> mRetired;
};
