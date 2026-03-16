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
 *      Abstract base for side channel implementations (e.g. provisioning CLI).
 *      Aligns with existing BLEChannel.h / BLEChannelImpl in efr32 for refactor.
 *      Extends BleChannel with side-channel-specific API (GATT read/write/CCCD,
 *      connection state, CLI GAP/GATT methods, getters).
 */

#pragma once

#include "BleChannel.h"
#include "BlePlatformTypes.h"
#include <lib/core/CHIPError.h>
#include <lib/core/Optional.h>
#include <lib/support/Span.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

/**
 * Abstract base for side channel. Implements BleChannel and adds the same
 * surface as the existing BLEChannel (efr32): ConfigureAdvertising with
 * AdvConfigStruct, StartAdvertising() (no params), AddConnection/RemoveConnection,
 * HandleReadRequest/HandleWriteRequest (ByteSpan/MutableByteSpan), HandleCCCDWriteRequest,
 * UpdateMtu, and CLI methods (GAP/GATT client) plus getters.
 */
class BleSideChannel : public BleChannel
{
public:
    ~BleSideChannel() override = default;

    // ----- Side-channel advertising (matches existing BLEChannel) -----
    /** Configure advertising data and parameters; must be called before StartAdvertising(). */
    virtual CHIP_ERROR ConfigureAdvertising(const AdvConfigStruct & config) = 0;
    /** Start advertising using configured parameters. */
    virtual CHIP_ERROR StartAdvertising(void) = 0;
    virtual CHIP_ERROR StopAdvertising(void)  = 0;

    // ----- Connection state (side channel) -----
    virtual void AddConnection(uint8_t connectionHandle, uint8_t bondingHandle) = 0;
    virtual bool RemoveConnection(uint8_t connectionHandle)                   = 0;

    // ----- GATT server (side channel) -----
    virtual void HandleReadRequest(void * platformEvent, ByteSpan data)              = 0;
    virtual void HandleWriteRequest(void * platformEvent, MutableByteSpan data)       = 0;
    virtual CHIP_ERROR HandleCCCDWriteRequest(void * platformEvent, bool & isNewSubscription) = 0;
    virtual void UpdateMtu(void * platformEvent)                                       = 0;

    // ----- Event handling (side channel) -----
    virtual bool CanHandleEvent(uint32_t eventId) { return false; }
    virtual void ParseEvent(void * platformEvent) {}

    // ----- CLI: GAP -----
    virtual CHIP_ERROR GeneratAdvertisingData(uint8_t discoverMove, uint8_t connectMode,
                                             const Optional<uint16_t> & maxEvents) = 0;
    virtual CHIP_ERROR SetAdvertisingParams(uint32_t intervalMin, uint32_t intervalMax, uint16_t duration,
                                            const Optional<uint16_t> & maxEvents,
                                            const Optional<uint8_t> & channelMap)   = 0;
    virtual CHIP_ERROR OpenConnection(const uint8_t * address, size_t addressLen, uint8_t addrType) = 0;
    virtual CHIP_ERROR SetConnectionParams(const Optional<uint8_t> & connectionHandle, uint32_t intervalMin,
                                           uint32_t intervalMax, uint16_t latency, uint16_t timeout) = 0;
    virtual CHIP_ERROR CloseConnection(void) = 0;
    virtual CHIP_ERROR SetAdvHandle(uint8_t handle) = 0;

    // ----- CLI: GATT (client) -----
    virtual CHIP_ERROR DiscoverServices()                                               = 0;
    virtual CHIP_ERROR DiscoverCharacteristics(uint32_t serviceHandle)                    = 0;
    virtual CHIP_ERROR SetCharacteristicNotification(uint8_t characteristicHandle, uint8_t flags) = 0;
    virtual CHIP_ERROR SetCharacteristicValue(uint8_t characteristicHandle, const ByteSpan & value) = 0;

    // ----- Getters (matches existing BLEChannel) -----
    virtual uint8_t GetAdvHandle(void) const { return 0xff; }
    virtual uint8_t GetConnectionHandle(void) const { return 0; }
    virtual uint8_t GetBondingHandle(void) const { return 0; }
};

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
