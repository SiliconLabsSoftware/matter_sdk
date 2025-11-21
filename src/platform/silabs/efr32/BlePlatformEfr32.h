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
 *          EFR32-specific implementation of BlePlatformInterface
 */

#pragma once

#include <platform/silabs/ble/BlePlatformInterface.h>
#include <platform/silabs/ble/BlePlatformTypes.h>
#include "gatt_db.h"
#include "sl_bt_api.h"
#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <memory>

namespace chip {
namespace DeviceLayer {
namespace Internal {
namespace Silabs {

/**
 * @brief EFR32 implementation of BlePlatformInterface
 *
 * This class wraps the Silicon Labs Bluetooth API (sl_bt_api) for EFR32 platforms,
 * providing a unified interface for BLE operations.
 */
class BlePlatformEfr32 : public BlePlatformInterface
{
public:
    BlePlatformEfr32();
    ~BlePlatformEfr32() override = default;

    // ===== Initialization and Shutdown =====
    CHIP_ERROR Init() override;
    void Shutdown() override;

    // ===== Advertising Operations =====
    CHIP_ERROR ConfigureAdvertising(const BleAdvertisingConfig & config) override;
    CHIP_ERROR StartAdvertising() override;
    CHIP_ERROR StopAdvertising() override;
    bool IsAdvertising() const override;
    uint8_t GetAdvertisingHandle() const override;

    // ===== Connection Management =====
    CHIP_ERROR AddConnection(uint8_t connectionHandle, uint8_t bondingHandle, const bd_addr & address) override;
    bool RemoveConnection(uint8_t connectionHandle) override;
    BleConnectionState * GetConnectionState(uint8_t connectionHandle, bool allocate = false) override;
    uint16_t GetNumConnections() const override;
    uint16_t GetMTU(uint8_t connectionHandle) const override;

    // ===== GATT Operations =====
    CHIP_ERROR SendIndication(uint8_t connectionHandle, uint16_t characteristicHandle, const ByteSpan & data) override;
    CHIP_ERROR SendWriteResponse(uint8_t connectionHandle, uint16_t characteristicHandle, uint8_t status) override;
    CHIP_ERROR SendReadResponse(uint8_t connectionHandle, uint16_t characteristicHandle, const ByteSpan & data) override;

    // ===== Connection Parameters =====
    CHIP_ERROR SetConnectionParams(uint8_t connectionHandle, const BleConnectionParams & params) override;
    CHIP_ERROR SetDefaultConnectionParams(const BleConnectionParams & params) override;

    // ===== Event Handling =====
    bool CanHandleEvent(uint32_t eventType) override;
    bool ParseEvent(void * platformEvent, BleEvent & unifiedEvent) override;
    bool HandleEvent(const BleEvent & event, BLEChannel * sideChannel) override;

    // ===== Side Channel Support =====
    bool IsChipoBleConnection(uint8_t connectionHandle, uint8_t advertiserHandle, uint8_t chipobleAdvertisingHandle) override;
    bool IsChipoBleCharacteristic(uint16_t characteristicHandle) override;

    // ===== Platform-Specific Helpers =====
    CHIP_ERROR MapPlatformError(int platformError) override;
    void SetManager(void * manager) override;

    /**
     * @brief Get the singleton instance
     */
    static BlePlatformEfr32 & GetInstance();

private:
    static constexpr uint8_t kMaxConnections = BLE_LAYER_NUM_BLE_ENDPOINTS;
    static constexpr uint8_t kInvalidAdvertisingHandle = 0xFF;

    // Connection state management
    BleConnectionState mConnections[kMaxConnections];
    uint8_t mAdvertisingHandle;

    // Manager instance for callbacks
    void * mManager;

    // Helper methods
    CHIP_ERROR MapBLEError(sl_status_t status);
    bool IsChipoBleCharacteristicInternal(uint16_t characteristic);
};

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
