/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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
 *          Common types and structures for BLE Platform Interface abstraction
 */

#pragma once

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <stdint.h>
#include <string.h>

// Forward declaration - bd_addr is defined in platform-specific headers
// For EFR32: sl_bt_api.h
// For SiWx917/rs911x: rsi_ble.h or similar
struct bd_addr;

namespace chip {
namespace DeviceLayer {
namespace Internal {
namespace Silabs {

/**
 * @brief Unified BLE connection state structure
 */
struct BleConnectionState
{
    uint8_t connectionHandle;
    uint8_t bondingHandle;
    uint16_t mtu : 10;
    uint16_t allocated : 1;
    uint16_t subscribed : 1;
    uint16_t unused : 4;
    bd_addr address;

    BleConnectionState() { memset(this, 0, sizeof(*this)); }
};

/**
 * @brief Unified advertising configuration structure
 */
struct BleAdvertisingConfig
{
    ByteSpan advData;
    ByteSpan responseData;
    uint32_t intervalMin;
    uint32_t intervalMax;
    uint8_t advConnectableMode;
    uint16_t duration;
    uint8_t maxEvents;
    uint8_t advertisingHandle;

    BleAdvertisingConfig() : intervalMin(0), intervalMax(0), advConnectableMode(0), duration(0), maxEvents(0), advertisingHandle(0xFF) {}
};

/**
 * @brief Unified connection parameters structure
 */
struct BleConnectionParams
{
    uint32_t intervalMin;
    uint32_t intervalMax;
    uint16_t latency;
    uint16_t timeout;
    uint16_t minCeLength;
    uint16_t maxCeLength;

    BleConnectionParams() : intervalMin(0), intervalMax(0), latency(0), timeout(0), minCeLength(0), maxCeLength(0) {}
};

/**
 * @brief Unified event types for platform abstraction
 */
enum class BleEventType : uint8_t
{
    kSystemBoot,
    kConnectionOpened,
    kConnectionParameters,
    kConnectionClosed,
    kGattWriteRequest,
    kGattReadRequest,
    kGattMtuExchanged,
    kGattCharacteristicStatus,
    kSoftTimer,
    kUnknown
};

/**
 * @brief Unified connection opened event data
 */
struct BleConnectionOpenedEvent
{
    uint8_t connection;
    uint8_t bonding;
    uint8_t advertiser;
    bd_addr address;
    uint8_t addressType;
};

/**
 * @brief Unified connection closed event data
 */
struct BleConnectionClosedEvent
{
    uint8_t connection;
    uint16_t reason;
};

/**
 * @brief Unified GATT write request event data
 */
struct BleGattWriteRequestEvent
{
    uint8_t connection;
    uint16_t characteristic;
    uint8_t * attValue;
    uint16_t attValueLen;
};

/**
 * @brief Unified GATT read request event data
 */
struct BleGattReadRequestEvent
{
    uint8_t connection;
    uint16_t characteristic;
};

/**
 * @brief Unified MTU exchanged event data
 */
struct BleMtuExchangedEvent
{
    uint8_t connection;
    uint16_t mtu;
};

/**
 * @brief Unified characteristic status event data
 */
struct BleCharacteristicStatusEvent
{
    uint8_t connection;
    uint16_t characteristic;
    uint8_t statusFlags;
};

/**
 * @brief Unified event structure
 */
struct BleEvent
{
    BleEventType type;
    union {
        BleConnectionOpenedEvent connectionOpened;
        BleConnectionClosedEvent connectionClosed;
        BleGattWriteRequestEvent gattWriteRequest;
        BleGattReadRequestEvent gattReadRequest;
        BleMtuExchangedEvent mtuExchanged;
        BleCharacteristicStatusEvent characteristicStatus;
    } data;

    BleEvent() : type(BleEventType::kUnknown) { memset(&data, 0, sizeof(data)); }
};

/**
 * @brief Callback function type for BLE events
 */
using BleEventCallback = void (*)(const BleEvent & event, void * context);

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
