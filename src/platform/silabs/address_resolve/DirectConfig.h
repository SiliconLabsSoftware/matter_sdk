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

#include <platform/silabs/SilabsConfig.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

constexpr inline uint32_t UserConfigKey(uint8_t keyBaseOffset, uint8_t id)
{
    return kUserNvm3KeyDomainLoLimit | static_cast<uint32_t>(keyBaseOffset) << 8 | id;
}
    // NVM3 key base offsets to store matter direct config values
    // ** Key base from 0x7 are available, using 0x7 for now**
    // Note: This could also be located in the user space within kUserNvm3KeyDomainLoLimit to kUserNvm3KeyDomainHiLimit
    // Persistent config values set at manufacturing time. Retained during factory reset.
    static constexpr uint8_t kMatterDirectConfig_KeyBase = 0x7;

    // Target Extended Address
    static constexpr uint32_t kTargetExtendedAddress         = UserConfigKey(kMatterDirectConfig_KeyBase, 0x00);
    // Resolve result config: IPV6 address, IPV6 address length, port
    static constexpr uint32_t kPairedIPV6Address             = UserConfigKey(kMatterDirectConfig_KeyBase, 0x01);
    static constexpr uint32_t kPairedPort                    = UserConfigKey(kMatterDirectConfig_KeyBase, 0x02);
    // MRP remote config: idle interval, active interval, active threshold time
    static constexpr uint32_t kMRPRemoteIdleInterval          = UserConfigKey(kMatterDirectConfig_KeyBase, 0x03);
    static constexpr uint32_t kMRPRemoteActiveInterval        = UserConfigKey(kMatterDirectConfig_KeyBase, 0x04);
    static constexpr uint32_t kMRPRemoteActiveThresholdTime   = UserConfigKey(kMatterDirectConfig_KeyBase, 0x05);

    // Peer id config: compressed fabric id, node id
    static constexpr uint32_t kPairedCompressedFabricId      = UserConfigKey(kMatterDirectConfig_KeyBase, 0x06);
    static constexpr uint32_t kPairedNodeId                  = UserConfigKey(kMatterDirectConfig_KeyBase, 0x07);

    // Operational keypair config: public key, private key
    static constexpr uint32_t kOperationalKeypair            = UserConfigKey(kMatterDirectConfig_KeyBase, 0x08);

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip