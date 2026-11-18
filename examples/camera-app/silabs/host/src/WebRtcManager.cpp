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

#include "WebRtcManager.h"

#include "CameraDevice.h"
#include "Log.h"
#include "Tlv.h"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

using camera::Status;
using camera::WebRTC::EndReason;
namespace RequestorCommands = camera::WebRTC::RequestorCommands;
namespace SessionRequest    = camera::WebRTC::SessionRequest;
namespace ICECandidateStruct = camera::WebRTC::ICECandidateStruct;

namespace {

constexpr int kH264PayloadType   = 96;
constexpr int kOpusPayloadType   = 111;
constexpr uint32_t kVideoSsrc    = 42;
constexpr uint32_t kAudioSsrc    = 43;
constexpr size_t kMaxFragmentSize = 1188; // 1200 bytes packets minus the RTP header

// Without Matter over TCP, a command sent by the SoC must fit in a single Matter message.
constexpr size_t kMaxSignalingPayload = 1000;
// ICE candidates are split over several ICECandidates commands below this size.
constexpr size_t kMaxCandidatesPayload = 800;

bool EqualsIgnoreCase(const std::string & a, const std::string & b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// Candidates are sent with ICECandidates commands, not inside the SDP.
std::string StripCandidates(const std::string & sdp)
{
    std::istringstream in(sdp);
    std::string line;
    std::string out;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.rfind("a=candidate:", 0) == 0 || line == "a=end-of-candidates")
        {
            continue;
        }
        out += line + "\r\n";
    }
    return out;
}

struct MediaSection
{
    std::string mid;
    int payloadType = -1;
};

// Finds the m-line of the given kind in a remote offer, and the payload type of the codec.
std::optional<MediaSection> FindMedia(rtc::Description & description, const std::string & kind, const std::string & codec)
{
    for (int index = 0; index < description.mediaCount(); index++)
    {
        auto entry = description.media(index);
        if (!std::holds_alternative<rtc::Description::Media *>(entry))
        {
            continue;
        }
        rtc::Description::Media * media = std::get<rtc::Description::Media *>(entry);
        if (media == nullptr || media->type() != kind)
        {
            continue;
        }

        MediaSection section;
        section.mid = media->mid();
        for (int payloadType : media->payloadTypes())
        {
            const rtc::Description::Media::RtpMap * map = media->rtpMap(payloadType);
            if (map == nullptr || !EqualsIgnoreCase(map->format, codec))
            {
                continue;
            }
            // For H.264, prefer the non-interleaved packetization used by the packetizer.
            const bool preferred = std::any_of(map->fmtps.begin(), map->fmtps.end(), [](const std::string & fmtp) {
                return fmtp.find("packetization-mode=1") != std::string::npos;
            });
            if (section.payloadType < 0 || preferred)
            {
                section.payloadType = payloadType;
            }
            if (preferred)
            {
                break;
            }
        }
        if (section.payloadType >= 0)
        {
            return section;
        }
    }
    return std::nullopt;
}

std::shared_ptr<rtc::Track> AddVideoTrack(rtc::PeerConnection & peer, const std::string & mid, int payloadType,
                                          CameraDevice & camera)
{
    rtc::Description::Video media(mid, rtc::Description::Direction::SendOnly);
    media.addH264Codec(payloadType);
    media.addSSRC(kVideoSsrc, "camera-video", "camera", "camera-video");
    auto track = peer.addTrack(media);

    auto config = std::make_shared<rtc::RtpPacketizationConfig>(kVideoSsrc, "camera-video", static_cast<uint8_t>(payloadType),
                                                                 rtc::H264RtpPacketizer::ClockRate);
    config->mid     = mid;
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, config, kMaxFragmentSize);
    packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(config));
    packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    packetizer->addToChain(std::make_shared<rtc::PliHandler>([&camera] { camera.RequestKeyFrame(); }));
    track->setMediaHandler(packetizer);
    return track;
}

std::shared_ptr<rtc::Track> AddAudioTrack(rtc::PeerConnection & peer, const std::string & mid, int payloadType)
{
    rtc::Description::Audio media(mid, rtc::Description::Direction::SendOnly);
    media.addOpusCodec(payloadType);
    media.addSSRC(kAudioSsrc, "camera-audio", "camera", "camera-audio");
    auto track = peer.addTrack(media);

    auto config = std::make_shared<rtc::RtpPacketizationConfig>(kAudioSsrc, "camera-audio", static_cast<uint8_t>(payloadType),
                                                                 rtc::OpusRtpPacketizer::DefaultClockRate);
    config->mid     = mid;
    auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(config);
    packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(config));
    packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    track->setMediaHandler(packetizer);
    return track;
}

const char * StateName(rtc::PeerConnection::State state)
{
    switch (state)
    {
    case rtc::PeerConnection::State::New:
        return "new";
    case rtc::PeerConnection::State::Connecting:
        return "connecting";
    case rtc::PeerConnection::State::Connected:
        return "connected";
    case rtc::PeerConnection::State::Disconnected:
        return "disconnected";
    case rtc::PeerConnection::State::Failed:
        return "failed";
    case rtc::PeerConnection::State::Closed:
        return "closed";
    }
    return "unknown";
}

} // namespace

struct WebRtcManager::Session
{
    uint16_t id         = 0;
    uint16_t endpointId = 0;
    std::shared_ptr<rtc::PeerConnection> peer;
    std::shared_ptr<rtc::Track> videoTrack;
    std::shared_ptr<rtc::Track> audioTrack;
    std::optional<uint16_t> videoStreamId;
    std::optional<uint16_t> audioStreamId;
    bool connected         = false;
    bool descriptionSent   = false;
    bool gatheringComplete = false;
    bool candidatesSent    = false;
    std::vector<IceCandidate> localCandidates;
    // First media timestamp of the session: RTP timestamps are relative to it.
    std::optional<std::chrono::microseconds> mediaStart;
};

WebRtcManager::WebRtcManager(CameraDevice & camera, SignalingSender sender, std::string bindAddress) :
    mCamera(camera), mSender(std::move(sender)), mBindAddress(std::move(bindAddress))
{}

WebRtcManager::~WebRtcManager()
{
    std::map<uint16_t, std::shared_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        sessions.swap(mSessions);
    }
    for (auto & entry : sessions)
    {
        entry.second->peer->close();
    }
    ReleaseRetired();
}

void WebRtcManager::ReleaseRetired()
{
    std::vector<std::shared_ptr<rtc::PeerConnection>> retired;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        retired.swap(mRetired);
    }
}

Status WebRtcManager::SolicitOffer(const SessionParams & params)
{
    return CreateSession(params, nullptr);
}

Status WebRtcManager::ProvideOffer(const SessionParams & params, const std::string & sdp)
{
    return CreateSession(params, &sdp);
}

Status WebRtcManager::CreateSession(const SessionParams & params, const std::string * remoteOffer)
{
    ReleaseRetired();

    // A new negotiation on an existing session replaces it.
    std::shared_ptr<Session> previous = RemoveSession(params.sessionId);
    if (previous)
    {
        previous->peer->close();
    }

    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mSessions.size() >= kMaxSessions)
        {
            LogError("WebRTC session %u rejected: %zu sessions already open", params.sessionId, mSessions.size());
            return Status::kResourceExhausted;
        }
    }

    auto session        = std::make_shared<Session>();
    session->id         = params.sessionId;
    session->endpointId = params.endpointId;
    for (uint16_t id : params.videoStreams)
    {
        if (mCamera.HasVideoStream(id))
        {
            session->videoStreamId = id;
            break;
        }
    }
    for (uint16_t id : params.audioStreams)
    {
        if (mCamera.HasAudioStream(id))
        {
            session->audioStreamId = id;
            break;
        }
    }
    if ((!params.videoStreams.empty() && !session->videoStreamId) || (!params.audioStreams.empty() && !session->audioStreamId))
    {
        LogError("WebRTC session %u rejected: requested stream not running on the camera", params.sessionId);
        return Status::kNotFound;
    }
    if (!session->videoStreamId && !session->audioStreamId)
    {
        return Status::kInvalidCommand;
    }

    try
    {
        rtc::Configuration config;
        for (const auto & server : params.iceServers)
        {
            for (const auto & url : server.urls)
            {
                rtc::IceServer iceServer(url);
                if (!server.username.empty())
                {
                    iceServer.username = server.username;
                    iceServer.password = server.credential;
                }
                config.iceServers.push_back(iceServer);
            }
        }
        if (params.iceTransportPolicy == "relay")
        {
            config.iceTransportPolicy = rtc::TransportPolicy::Relay;
        }
        if (!mBindAddress.empty())
        {
            config.bindAddress = mBindAddress;
        }
        config.disableAutoNegotiation = true;
        session->peer                 = std::make_shared<rtc::PeerConnection>(config);

        if (remoteOffer == nullptr)
        {
            if (session->videoStreamId)
            {
                session->videoTrack = AddVideoTrack(*session->peer, "video", kH264PayloadType, mCamera);
            }
            if (session->audioStreamId)
            {
                session->audioTrack = AddAudioTrack(*session->peer, "audio", kOpusPayloadType);
            }
        }
        else
        {
            // Answer with tracks matching the m-lines of the offer.
            rtc::Description offer(*remoteOffer, rtc::Description::Type::Offer);
            auto video = FindMedia(offer, "video", "H264");
            auto audio = FindMedia(offer, "audio", "opus");
            if (session->videoStreamId && video)
            {
                session->videoTrack = AddVideoTrack(*session->peer, video->mid, video->payloadType, mCamera);
            }
            if (session->audioStreamId && audio)
            {
                session->audioTrack = AddAudioTrack(*session->peer, audio->mid, audio->payloadType);
            }
            if (!session->videoTrack && !session->audioTrack)
            {
                LogError("WebRTC session %u rejected: the offer has no H.264 video or Opus audio", params.sessionId);
                return Status::kConstraintError;
            }
        }
    }
    catch (const std::exception & e)
    {
        LogError("WebRTC session %u: %s", params.sessionId, e.what());
        return Status::kInvalidCommand;
    }

    const uint16_t id = session->id;
    session->peer->onLocalDescription([this, id](rtc::Description description) {
        OnLocalDescription(id, std::string(description), description.type() == rtc::Description::Type::Offer);
    });
    session->peer->onLocalCandidate(
        [this, id](rtc::Candidate candidate) { OnLocalCandidate(id, candidate.candidate(), candidate.mid()); });
    session->peer->onGatheringStateChange([this, id](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete)
        {
            OnGatheringComplete(id);
        }
    });
    session->peer->onStateChange([this, id](rtc::PeerConnection::State state) {
        LogInfo("WebRTC session %u: %s", id, StateName(state));
        if (state == rtc::PeerConnection::State::Connected)
        {
            OnConnected(id);
        }
        else if (state == rtc::PeerConnection::State::Failed)
        {
            OnClosed(id, EndReason::kICEFailed);
        }
        else if (state == rtc::PeerConnection::State::Closed)
        {
            OnClosed(id, EndReason::kUserHangup);
        }
    });

    std::shared_ptr<rtc::PeerConnection> peer = session->peer;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mSessions[id] = session;
    }
    LogInfo("WebRTC session %u created (video stream %d, audio stream %d)", id,
            session->videoStreamId ? *session->videoStreamId : -1, session->audioStreamId ? *session->audioStreamId : -1);

    // The local description is sent to the SoC from OnLocalDescription().
    try
    {
        if (remoteOffer != nullptr)
        {
            peer->setRemoteDescription(rtc::Description(*remoteOffer, rtc::Description::Type::Offer));
        }
        peer->setLocalDescription();
    }
    catch (const std::exception & e)
    {
        LogError("WebRTC session %u negotiation failed: %s", id, e.what());
        RemoveSession(id);
        peer->close();
        return Status::kInvalidCommand;
    }
    return Status::kSuccess;
}

Status WebRtcManager::ProvideAnswer(uint16_t sessionId, const std::string & sdp)
{
    std::shared_ptr<rtc::PeerConnection> peer;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mSessions.find(sessionId);
        if (it == mSessions.end())
        {
            return Status::kNotFound;
        }
        peer = it->second->peer;
    }
    try
    {
        peer->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
    }
    catch (const std::exception & e)
    {
        LogError("WebRTC session %u: invalid answer: %s", sessionId, e.what());
        return Status::kInvalidCommand;
    }
    return Status::kSuccess;
}

Status WebRtcManager::ProvideIceCandidates(uint16_t sessionId, const std::vector<IceCandidate> & candidates)
{
    std::shared_ptr<rtc::PeerConnection> peer;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mSessions.find(sessionId);
        if (it == mSessions.end())
        {
            return Status::kNotFound;
        }
        peer = it->second->peer;
    }
    for (const auto & candidate : candidates)
    {
        try
        {
            if (candidate.mid.empty())
            {
                peer->addRemoteCandidate(rtc::Candidate(candidate.candidate));
            }
            else
            {
                peer->addRemoteCandidate(rtc::Candidate(candidate.candidate, candidate.mid));
            }
        }
        catch (const std::exception & e)
        {
            LogError("WebRTC session %u: ignored candidate '%s': %s", sessionId, candidate.candidate.c_str(), e.what());
        }
    }
    return Status::kSuccess;
}

Status WebRtcManager::EndSession(uint16_t sessionId)
{
    ReleaseRetired();
    std::shared_ptr<Session> session = RemoveSession(sessionId);
    if (session)
    {
        LogInfo("WebRTC session %u ended by the requestor", sessionId);
        session->peer->close();
    }
    // Ending an unknown session (e.g. after a host restart) is not an error.
    return Status::kSuccess;
}

void WebRtcManager::EndAllSessions(EndReason reason)
{
    std::map<uint16_t, std::shared_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        sessions.swap(mSessions);
    }
    for (auto & entry : sessions)
    {
        SendEnd(entry.second->endpointId, entry.first, reason);
        entry.second->peer->close();
    }
}

std::shared_ptr<WebRtcManager::Session> WebRtcManager::RemoveSession(uint16_t sessionId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mSessions.find(sessionId);
    if (it == mSessions.end())
    {
        return nullptr;
    }
    std::shared_ptr<Session> session = it->second;
    mSessions.erase(it);
    return session;
}

void WebRtcManager::OnLocalDescription(uint16_t sessionId, const std::string & sdp, bool offer)
{
    uint16_t endpointId = 0;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mSessions.find(sessionId);
        if (it == mSessions.end())
        {
            return;
        }
        it->second->descriptionSent = true;
        endpointId                  = it->second->endpointId;
    }

    const std::string localSdp = StripCandidates(sdp);
    if (localSdp.size() > kMaxSignalingPayload)
    {
        LogError("WebRTC session %u: %zu bytes SDP, may not fit in a Matter message", sessionId, localSdp.size());
    }

    tlv::Writer writer;
    writer.StartStructure()
        .PutUInt(tlv::ContextTag(SessionRequest::kSessionID), sessionId)
        .PutString(tlv::ContextTag(SessionRequest::kPayload), localSdp)
        .EndContainer();
    LogInfo("WebRTC session %u: sending %s (%zu bytes)", sessionId, offer ? "offer" : "answer", localSdp.size());
    mSender(endpointId, offer ? RequestorCommands::kOffer : RequestorCommands::kAnswer, writer.Data());

    SendCandidates(sessionId);
}

void WebRtcManager::OnLocalCandidate(uint16_t sessionId, const std::string & candidate, const std::string & mid)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mSessions.find(sessionId);
    if (it != mSessions.end())
    {
        it->second->localCandidates.push_back({ candidate, mid });
    }
}

void WebRtcManager::OnGatheringComplete(uint16_t sessionId)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mSessions.find(sessionId);
        if (it == mSessions.end())
        {
            return;
        }
        it->second->gatheringComplete = true;
    }
    SendCandidates(sessionId);
}

void WebRtcManager::SendCandidates(uint16_t sessionId)
{
    std::vector<IceCandidate> candidates;
    uint16_t endpointId = 0;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mSessions.find(sessionId);
        // Sent once, after the local description so that the requestor receives them in order.
        if (it == mSessions.end() || !it->second->descriptionSent || !it->second->gatheringComplete || it->second->candidatesSent)
        {
            return;
        }
        it->second->candidatesSent = true;
        candidates                 = it->second->localCandidates;
        endpointId                 = it->second->endpointId;
    }

    if (candidates.empty())
    {
        LogError("WebRTC session %u: no local ICE candidate", sessionId);
        return;
    }

    size_t index = 0;
    while (index < candidates.size())
    {
        tlv::Writer writer;
        writer.StartStructure()
            .PutUInt(tlv::ContextTag(SessionRequest::kSessionID), sessionId)
            .StartArray(tlv::ContextTag(SessionRequest::kPayload));
        size_t size  = 0;
        size_t count = 0;
        while (index < candidates.size() && (count == 0 || size + candidates[index].candidate.size() < kMaxCandidatesPayload))
        {
            const IceCandidate & candidate = candidates[index++];
            writer.StartStructure().PutString(tlv::ContextTag(ICECandidateStruct::kCandidate), candidate.candidate);
            if (candidate.mid.empty())
            {
                writer.PutNull(tlv::ContextTag(ICECandidateStruct::kSDPMid));
            }
            else
            {
                writer.PutString(tlv::ContextTag(ICECandidateStruct::kSDPMid), candidate.mid);
            }
            writer.PutNull(tlv::ContextTag(ICECandidateStruct::kSDPMLineIndex)).EndContainer();
            size += candidate.candidate.size() + candidate.mid.size();
            count++;
        }
        writer.EndContainer().EndContainer();
        LogInfo("WebRTC session %u: sending %zu ICE candidate(s)", sessionId, count);
        mSender(endpointId, RequestorCommands::kICECandidates, writer.Data());
    }
}

void WebRtcManager::OnConnected(uint16_t sessionId)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mSessions.find(sessionId);
        if (it == mSessions.end())
        {
            return;
        }
        it->second->connected = true;
    }
    // The peer needs a key frame to start decoding.
    mCamera.RequestKeyFrame();
}

void WebRtcManager::OnClosed(uint16_t sessionId, EndReason reason)
{
    // Called from a libdatachannel thread: the peer connection is released on the next session change.
    std::shared_ptr<Session> session = RemoveSession(sessionId);
    if (session)
    {
        LogInfo("WebRTC session %u closed, notifying the requestor", sessionId);
        SendEnd(session->endpointId, sessionId, reason);
        std::lock_guard<std::mutex> lock(mMutex);
        mRetired.push_back(session->peer);
    }
}

void WebRtcManager::SendEnd(uint16_t endpointId, uint16_t sessionId, EndReason reason)
{
    tlv::Writer writer;
    writer.StartStructure()
        .PutUInt(tlv::ContextTag(SessionRequest::kSessionID), sessionId)
        .PutUInt(tlv::ContextTag(SessionRequest::kPayload), static_cast<uint8_t>(reason))
        .EndContainer();
    mSender(endpointId, RequestorCommands::kEnd, writer.Data());
}

void WebRtcManager::OnVideoFrame(uint16_t streamId, const uint8_t * data, size_t size, std::chrono::microseconds timestamp)
{
    std::vector<std::pair<std::shared_ptr<rtc::Track>, std::chrono::microseconds>> targets;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto & entry : mSessions)
        {
            Session & session = *entry.second;
            if (session.connected && session.videoTrack && session.videoStreamId == streamId)
            {
                if (!session.mediaStart)
                {
                    session.mediaStart = timestamp;
                }
                targets.emplace_back(session.videoTrack, timestamp - *session.mediaStart);
            }
        }
    }
    for (auto & target : targets)
    {
        try
        {
            if (target.first->isOpen())
            {
                target.first->sendFrame(reinterpret_cast<const rtc::byte *>(data), size,
                                        rtc::FrameInfo(std::chrono::duration<double>(target.second)));
            }
        }
        catch (const std::exception & e)
        {
            LogError("Video frame not sent: %s", e.what());
        }
    }
}

void WebRtcManager::OnAudioFrame(uint16_t streamId, const uint8_t * data, size_t size, std::chrono::microseconds timestamp)
{
    std::vector<std::pair<std::shared_ptr<rtc::Track>, std::chrono::microseconds>> targets;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto & entry : mSessions)
        {
            Session & session = *entry.second;
            if (session.connected && session.audioTrack && session.audioStreamId == streamId)
            {
                if (!session.mediaStart)
                {
                    session.mediaStart = timestamp;
                }
                targets.emplace_back(session.audioTrack, timestamp - *session.mediaStart);
            }
        }
    }
    for (auto & target : targets)
    {
        try
        {
            if (target.first->isOpen())
            {
                target.first->sendFrame(reinterpret_cast<const rtc::byte *>(data), size,
                                        rtc::FrameInfo(std::chrono::duration<double>(target.second)));
            }
        }
        catch (const std::exception & e)
        {
            LogError("Audio frame not sent: %s", e.what());
        }
    }
}
