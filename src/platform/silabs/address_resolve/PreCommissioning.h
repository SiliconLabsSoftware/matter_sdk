/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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
#include <lib/core/CHIPError.h>
#include <transport/raw/PeerAddress.h>
#include <lib/core/PeerId.h>
#include <messaging/ReliableMessageProtocolConfig.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

/**
 * @brief PreCommissioning class
 * This class is used to setup the Thread and Matter fabric before commissioning.
 * To do so, in its current implementation, at construction time, it writes
 * the Thread and Matter fabric information in the NVM3, as well as the target's
 * IP address and port, MRP configuration and peer id.
 *
 * @note In post POC, the writes will need to be done through commander nvm3 commands
 * to the pre-commissioned device and the PreCommissioning object will instead read
 * the information from the NVM3 where needed.
 */
class PreCommissioning
{
public:
    PreCommissioning()  = default;
    ~PreCommissioning() = default;

    static PreCommissioning & GetInstance()
    {
        static PreCommissioning instance;
        return instance;
    }

    CHIP_ERROR Init();
    CHIP_ERROR GetTargetAddress(Transport::PeerAddress & address) const;
    CHIP_ERROR GetTargetMrpConfig(ReliableMessageProtocolConfig & mrpConfig) const;
    CHIP_ERROR GetTargetPeerId(PeerId & peerId) const;
    CHIP_ERROR GetTargetExtAddress(otExtAddress & extAddress) const;
private:
    // All Load or Setup functions must be called before Server::Init() to ensure the fabric is loaded before the server is started.
    // These methods will perform the writes to the NVM3, they can be recycled for OOB commissioning.
    CHIP_ERROR SetupThread();
    CHIP_ERROR SetupMatterFabric();
    CHIP_ERROR SetupTarget();

    // These methods will perform the reads from the NVM3, they assume commander nvm3 writes have already been performed.
    CHIP_ERROR LoadThread();
    CHIP_ERROR LoadFabric();
    CHIP_ERROR LoadTarget();

    Transport::PeerAddress mTargetAddress{};
    ReliableMessageProtocolConfig mTargetMrpConfig{ System::Clock::Milliseconds32(2000),
                                                    System::Clock::Milliseconds32(2000),
                                                    System::Clock::Milliseconds16(4000) };
    PeerId mTargetPeerId{};

    otExtAddress mTargetExtAddress{};
};
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip