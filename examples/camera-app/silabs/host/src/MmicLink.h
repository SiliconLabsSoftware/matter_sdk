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
 * MMIC link with the camera SoC over a UART (examples/platform/silabs/mmic).
 *
 * A reader thread reassembles the frames received on the UART, a dispatcher thread hands the
 * requests of the SoC (cluster_command, attribute_changed) to the application. Sending is
 * thread-safe.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MmicLink
{
public:
    // Largest frame accepted by the SoC UART transport (SL_MATTER_MMIC_UART_MAX_FRAME_SIZE).
    static constexpr size_t kMaxFrameSize = 2048;

    struct ClusterCommand
    {
        uint16_t transactionId = 0;
        bool answerExpected    = false;
        uint16_t endpointId    = 0;
        uint32_t clusterId     = 0;
        uint32_t commandId     = 0;
        std::vector<uint8_t> params; // Matter TLV
    };

    struct AttributeChange
    {
        uint16_t endpointId  = 0;
        uint32_t clusterId   = 0;
        uint32_t attributeId = 0;
        std::vector<uint8_t> value; // Matter TLV, empty when not provided
    };

    using CommandHandler   = std::function<void(const ClusterCommand &)>;
    using AttributeHandler = std::function<void(const AttributeChange &)>;

    ~MmicLink();

    // Opens the UART at 115200 8N1, no flow control.
    bool Open(const std::string & device);
    void Start(CommandHandler commandHandler, AttributeHandler attributeHandler);
    void Stop();

    // Answers a cluster_command received from the SoC.
    bool SendAnswer(uint16_t transactionId, uint8_t status, const std::vector<uint8_t> & data = {});

    // Asks the SoC to send a cluster command on the Matter network. The SoC answer is logged.
    bool SendClusterCommand(uint16_t endpointId, uint32_t clusterId, uint32_t commandId, const std::vector<uint8_t> & params);

private:
    void ReaderLoop();
    void DispatchLoop();
    void OnBytes(const uint8_t * data, size_t length);
    void OnFrame(const std::vector<uint8_t> & frame);
    bool SendFrame(uint8_t header, uint8_t opcode, const std::vector<uint8_t> & payload);
    bool Write(const uint8_t * data, size_t length);

    int mFd = -1;
    std::atomic<bool> mRunning{ false };
    std::thread mReader;
    std::thread mDispatcher;

    std::mutex mWriteMutex;

    std::mutex mQueueMutex;
    std::condition_variable mQueueCondition;
    std::deque<std::vector<uint8_t>> mQueue;

    std::vector<uint8_t> mRxBuffer;

    std::mutex mRequestMutex;
    uint16_t mNextTransactionId = 0;
    std::map<uint16_t, std::string> mPendingRequests; // Description of the requests sent to the SoC, for logs

    CommandHandler mCommandHandler;
    AttributeHandler mAttributeHandler;
};
