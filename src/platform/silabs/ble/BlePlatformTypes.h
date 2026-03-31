/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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
 *    @file
 *      Platform-agnostic BLE types used by BleChannel and BLEManagerImpl.
 *      Shared across Matter BLE and side channel implementations.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

/**
 * Connection handle for Silabs BleChannel APIs.
 */
using BleConnectionHandle = uint8_t;

inline constexpr BleConnectionHandle kInvalidBleConnectionHandle = 0xFF;

/**
 * Opaque platform-event token forwarded by BLEManagerImpl to channel parsers.
 * Implementations cast this value to their native event pointer type.
 */
using BlePlatformEvent = uintptr_t;

/**
 * BLE event types delivered from platform stack to channel (ParseEvent)
 * and then to BLEManagerImpl via callback (OnMatterBleEvent).
 */
enum class BleEventType : uint32_t
{
    kConnectionOpened,
    kConnectionClosed,
    kConnectionParameters,
    kMtuExchanged,
    kGattWrite,
    kGattRead,
    kIndicationConfirmation,
    kSoftTimerExpired,
    kBoot,
    kUnknown
};

/**
 * Unified BLE event passed from channel to BLEManagerImpl.
 *
 * @tparam T  Type of `data` (default `const uint8_t *` for non-owning payload octets).
 *            Platform channels may use other instantiations when needed.
 */
template <typename T = const uint8_t *>
struct BleEvent
{
    BleEventType type              = BleEventType::kUnknown;
    BleConnectionHandle connection = kInvalidBleConnectionHandle;
    T data{};
    size_t dataLength        = 0;
    uint16_t attributeHandle = 0;
    uint32_t timerId         = 0;
};

/**
 * Advertising parameters for StartAdvertising.
 */
struct BleAdvertisingParams
{
    uint16_t minInterval    = 0;
    uint16_t maxInterval    = 0;
    bool connectable        = true;
    uint8_t * customData    = nullptr;
    size_t customDataLength = 0;
};

/**
 * Connection parameters for UpdateConnectionParams.
 */
struct BleConnectionParams
{
    uint16_t minInterval = 0;
    uint16_t maxInterval = 0;
    uint16_t latency     = 0;
    uint16_t timeout     = 0;
};

/**
 * Event callback type: channel calls this when it has parsed a platform event
 * and produced a BleEvent (e.g. for Matter BLE).
 */
using BleEventCallback = void (*)(const BleEvent<const uint8_t *> & event, void * context);

/**
 * Side-channel advertising configuration (aligns with existing BLEChannel
 * AdvConfigStruct). Used by BleSideChannel::ConfigureAdvertising().
 */
struct AdvConfigStruct
{
    ByteSpan advData;
    ByteSpan responseData;
    uint32_t intervalMin       = 0;
    uint32_t intervalMax       = 0;
    uint8_t advConnectableMode = 0;
    uint16_t duration          = 0;
    uint8_t maxEvents          = 0;
};

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
