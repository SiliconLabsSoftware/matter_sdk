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

#include "MmicLink.h"

#include "Log.h"
#include "mmic.h"
#include "mmic_cluster_names.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace {

constexpr int kReadPollTimeoutMs = 200;

const char * NameOrUnknown(const char * name)
{
    return (name != nullptr) ? name : "Unknown";
}

bool ConfigurePort(int fd)
{
    termios tio;
    if (tcgetattr(fd, &tio) != 0)
    {
        return false;
    }

    cfmakeraw(&tio);
    if (cfsetispeed(&tio, B115200) != 0 || cfsetospeed(&tio, B115200) != 0)
    {
        return false;
    }

    // 8N1, no flow control, receiver enabled, modem lines ignored.
    tio.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tio.c_cflag |= static_cast<tcflag_t>(CS8 | CLOCAL | CREAD);
    tio.c_iflag &= ~static_cast<tcflag_t>(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
    tio.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    tio.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        return false;
    }
    tcflush(fd, TCIOFLUSH);
    return true;
}

} // namespace

MmicLink::~MmicLink()
{
    Stop();
}

bool MmicLink::Open(const std::string & device)
{
    mFd = open(device.c_str(), O_RDWR | O_NOCTTY);
    if (mFd < 0)
    {
        LogError("Failed to open %s: %s", device.c_str(), strerror(errno));
        return false;
    }
    if (!ConfigurePort(mFd))
    {
        LogError("Failed to configure %s: %s", device.c_str(), strerror(errno));
        close(mFd);
        mFd = -1;
        return false;
    }
    LogInfo("MMIC link on %s @ 115200 8N1", device.c_str());
    return true;
}

void MmicLink::Start(CommandHandler commandHandler, AttributeHandler attributeHandler)
{
    mCommandHandler   = std::move(commandHandler);
    mAttributeHandler = std::move(attributeHandler);
    mRunning          = true;
    mReader           = std::thread(&MmicLink::ReaderLoop, this);
    mDispatcher       = std::thread(&MmicLink::DispatchLoop, this);
}

void MmicLink::Stop()
{
    if (mRunning.exchange(false))
    {
        mQueueCondition.notify_all();
        if (mReader.joinable())
        {
            mReader.join();
        }
        if (mDispatcher.joinable())
        {
            mDispatcher.join();
        }
    }
    if (mFd >= 0)
    {
        close(mFd);
        mFd = -1;
    }
}

void MmicLink::ReaderLoop()
{
    uint8_t buffer[512];
    while (mRunning)
    {
        pollfd descriptor = { mFd, POLLIN, 0 };
        int ready         = poll(&descriptor, 1, kReadPollTimeoutMs);
        if (ready < 0 && errno != EINTR)
        {
            LogError("MMIC link: poll failed: %s", strerror(errno));
            break;
        }
        if (ready <= 0)
        {
            continue;
        }

        ssize_t count = read(mFd, buffer, sizeof(buffer));
        if (count > 0)
        {
            OnBytes(buffer, static_cast<size_t>(count));
        }
        else if (count < 0 && errno != EINTR && errno != EAGAIN)
        {
            LogError("MMIC link: read failed: %s", strerror(errno));
            break;
        }
    }
}

void MmicLink::OnBytes(const uint8_t * data, size_t length)
{
    mRxBuffer.insert(mRxBuffer.end(), data, data + length);

    size_t start = 0;
    while (mRxBuffer.size() - start >= MMIC_PACKET_OVERHEAD)
    {
        const uint8_t * frame = mRxBuffer.data() + start;
        if (frame[0] != MMIC_HEADER_CMD && frame[0] != MMIC_HEADER_ANS)
        {
            start++;
            continue;
        }
        const uint16_t frameLength = mmic_read_length(frame);
        if (frameLength < MMIC_PACKET_OVERHEAD || frameLength > kMaxFrameSize)
        {
            start++;
            continue;
        }
        if (mRxBuffer.size() - start < frameLength)
        {
            break;
        }

        // A corrupted frame is skipped one byte at a time to resynchronize.
        if (mmic_deserialize_packet(frame, frameLength, frame[0], nullptr, nullptr, nullptr) != MMIC_ERROR_OK)
        {
            start++;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            mQueue.emplace_back(frame, frame + frameLength);
        }
        mQueueCondition.notify_one();
        start += frameLength;
    }
    mRxBuffer.erase(mRxBuffer.begin(), mRxBuffer.begin() + static_cast<std::ptrdiff_t>(start));

    // Drop data that can never form a frame.
    if (mRxBuffer.size() > 2 * kMaxFrameSize)
    {
        mRxBuffer.clear();
    }
}

void MmicLink::DispatchLoop()
{
    while (true)
    {
        std::vector<uint8_t> frame;
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mQueueCondition.wait(lock, [this] { return !mRunning || !mQueue.empty(); });
            if (!mRunning)
            {
                return;
            }
            frame = std::move(mQueue.front());
            mQueue.pop_front();
        }
        OnFrame(frame);
    }
}

void MmicLink::OnFrame(const std::vector<uint8_t> & frame)
{
    mmic_command_id_e opcode = INVALID_COMMAND_ID;
    const uint8_t * payload  = nullptr;
    uint16_t payloadLength   = 0;
    if (mmic_deserialize_packet(frame.data(), frame.size(), frame[0], &opcode, &payload, &payloadLength) != MMIC_ERROR_OK)
    {
        return;
    }

    if (frame[0] == MMIC_HEADER_ANS)
    {
        // Answer of the SoC to a cluster_command sent by the host.
        if (opcode == cluster_command && payloadLength >= sizeof(clusterCommandAnswerHeader_t))
        {
            clusterCommandAnswerHeader_t answer;
            std::memcpy(&answer, payload, sizeof(answer));
            std::string description;
            {
                std::lock_guard<std::mutex> lock(mRequestMutex);
                auto it = mPendingRequests.find(answer.transactionId);
                if (it != mPendingRequests.end())
                {
                    description = it->second;
                    mPendingRequests.erase(it);
                }
            }
            if (answer.status != 0)
            {
                LogError("SoC rejected %s: status 0x%02x", description.c_str(), answer.status);
            }
        }
        return;
    }

    if (opcode == cluster_command && payloadLength >= sizeof(clusterCommandHeader_t))
    {
        clusterCommandHeader_t header;
        std::memcpy(&header, payload, sizeof(header));

        ClusterCommand command;
        command.transactionId  = header.transactionId;
        command.answerExpected = (header.flags & MMIC_CLUSTER_COMMAND_FLAG_ANSWER_EXPECTED) != 0;
        command.endpointId     = header.endpointId;
        command.clusterId      = header.clusterId;
        command.commandId      = header.commandId;
        command.params.assign(payload + sizeof(header), payload + payloadLength);

        LogInfo("SoC -> %s.%s (endpoint %u%s)", NameOrUnknown(mmic_cluster_name(command.clusterId)),
                NameOrUnknown(mmic_command_name(command.clusterId, command.commandId)), command.endpointId,
                command.answerExpected ? ", answer expected" : "");
        if (mCommandHandler)
        {
            mCommandHandler(command);
        }
    }
    else if (opcode == attribute_changed && payloadLength >= sizeof(attributeChangedHeader_t))
    {
        attributeChangedHeader_t header;
        std::memcpy(&header, payload, sizeof(header));

        AttributeChange change;
        change.endpointId  = header.endpointId;
        change.clusterId   = header.clusterId;
        change.attributeId = header.attributeId;
        change.value.assign(payload + sizeof(header), payload + payloadLength);

        LogInfo("SoC -> attribute %s.%s changed (endpoint %u)", NameOrUnknown(mmic_cluster_name(change.clusterId)),
                NameOrUnknown(mmic_attribute_name(change.clusterId, change.attributeId)), change.endpointId);
        if (mAttributeHandler)
        {
            mAttributeHandler(change);
        }
    }
    else
    {
        LogError("Unexpected request from SoC (op code %u)", static_cast<unsigned>(opcode));
    }
}

bool MmicLink::SendAnswer(uint16_t transactionId, uint8_t status, const std::vector<uint8_t> & data)
{
    clusterCommandAnswerHeader_t header;
    header.transactionId = transactionId;
    header.status        = status;

    std::vector<uint8_t> payload(sizeof(header));
    std::memcpy(payload.data(), &header, sizeof(header));
    payload.insert(payload.end(), data.begin(), data.end());
    return SendFrame(MMIC_HEADER_ANS, cluster_command, payload);
}

bool MmicLink::SendClusterCommand(uint16_t endpointId, uint32_t clusterId, uint32_t commandId,
                                  const std::vector<uint8_t> & params)
{
    clusterCommandHeader_t header;
    header.flags      = 0;
    header.endpointId = endpointId;
    header.clusterId  = clusterId;
    header.commandId  = commandId;
    {
        std::lock_guard<std::mutex> lock(mRequestMutex);
        header.transactionId = mNextTransactionId++;
        std::string description = "cluster " + std::to_string(clusterId) + " command " + std::to_string(commandId);
        mPendingRequests[header.transactionId] = description;
        // Bounded: entries of requests never answered are dropped.
        if (mPendingRequests.size() > 64)
        {
            mPendingRequests.erase(mPendingRequests.begin());
        }
    }

    std::vector<uint8_t> payload(sizeof(header));
    std::memcpy(payload.data(), &header, sizeof(header));
    payload.insert(payload.end(), params.begin(), params.end());
    return SendFrame(MMIC_HEADER_CMD, cluster_command, payload);
}

bool MmicLink::SendFrame(uint8_t header, uint8_t opcode, const std::vector<uint8_t> & payload)
{
    if (payload.size() + MMIC_PACKET_OVERHEAD > kMaxFrameSize)
    {
        LogError("MMIC frame too large (%zu bytes, max %zu)", payload.size() + MMIC_PACKET_OVERHEAD, kMaxFrameSize);
        return false;
    }

    uint8_t * frame  = nullptr;
    size_t frameSize = 0;
    if (mmic_serialize_packet(header, static_cast<mmic_command_id_e>(opcode), payload.data(), payload.size(), &frame,
                              &frameSize) != MMIC_ERROR_OK)
    {
        return false;
    }
    bool written = Write(frame, frameSize);
    free(frame);
    return written;
}

bool MmicLink::Write(const uint8_t * data, size_t length)
{
    std::lock_guard<std::mutex> lock(mWriteMutex);
    size_t written = 0;
    while (written < length)
    {
        ssize_t count = write(mFd, data + written, length - written);
        if (count < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            LogError("MMIC link: write failed: %s", strerror(errno));
            return false;
        }
        written += static_cast<size_t>(count);
    }
    return true;
}
