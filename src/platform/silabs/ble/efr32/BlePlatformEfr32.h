/*
 *
 *    Copyright (c) 2020-2021 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
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
 *          EFR32 platform-specific implementation of BlePlatformInterface
 */

#pragma once

#include <platform/silabs/ble/BlePlatformInterface.h>
#include "gatt_db.h"
#include "sl_bt_api.h"
#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {
class BLEManagerImpl;
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip

namespace chip {
namespace DeviceLayer {
namespace Internal {
namespace Silabs {

/**
 * @brief EFR32 platform implementation of BlePlatformInterface
 */
class BlePlatformEfr32 : public BlePlatformInterface
{
public:
    static BlePlatformEfr32 & GetInstance()
    {
        static BlePlatformEfr32 sInstance;
        return sInstance;
    }

    // BlePlatformInterface implementation
    CHIP_ERROR Init() override;
    void SetManager(BLEManagerImpl * manager) override;
    CHIP_ERROR ConfigureAdvertising(const BleAdvertisingConfig & config) override;
    CHIP_ERROR StartAdvertising(uint32_t intervalMin, uint32_t intervalMax, bool connectable) override;
    CHIP_ERROR StopAdvertising() override;
    uint8_t GetAdvertisingHandle() const override { return mAdvertisingSetHandle; }
    CHIP_ERROR SendIndication(uint8_t connection, uint16_t characteristic, ByteSpan data) override;
    uint16_t GetMTU(uint8_t connection) const override;
    CHIP_ERROR CloseConnection(uint8_t connection) override;
    bool ParseEvent(void * platformEvent, BleEvent & unifiedEvent) override;
    CHIP_ERROR MapPlatformError(int32_t platformError) override;
    bool CanHandleEvent(uint32_t event) override;
    bool IsChipoBleCharacteristic(uint16_t characteristic) const override;
    bool IsTxCccdHandle(uint16_t characteristic) const override { return false; } // EFR32 uses characteristic_status events
    bool IsChipoBleConnection(uint8_t connection, uint8_t advertiser, uint8_t chipoBleAdvertiser) const override;
    BleConnectionState * GetConnectionState(uint8_t connection, bool allocate) override;
    void AddConnection(uint8_t connection, uint8_t bonding, const uint8_t * address) override;
    void RemoveConnection(uint8_t connection) override;
    CHIP_ERROR SendReadResponse(uint8_t connection, uint16_t characteristic, ByteSpan data) override;
    CHIP_ERROR SendWriteResponse(uint8_t connection, uint16_t characteristic, uint8_t status) override;
    bool HandleNonChipoBleConnection(uint8_t connection, uint8_t advertiser, uint8_t bonding, const uint8_t * address,
                                      uint8_t chipoBleAdvertiser) override;
    BlePlatformInterface::WriteType HandleChipoBleWrite(void * platformEvent, uint8_t connection, uint16_t characteristic) override;
    bool HandleNonChipoBleWrite(void * platformEvent, uint8_t connection, uint16_t characteristic) override;
    bool HandleNonChipoBleRead(void * platformEvent, uint8_t connection, uint16_t characteristic) override;
    bool HandleNonChipoBleMtuUpdate(void * platformEvent, uint8_t connection) override;
    CHIP_ERROR MapDisconnectReason(uint16_t platformReason) override;
    bool HandleNonChipoBleDisconnect(void * platformEvent, uint8_t connection) override;
    TxCccdWriteResult HandleTxCccdWrite(void * platformEvent, const BleEvent & unifiedEvent) override;
    bool HandleNonChipoBleCccdWrite(void * platformEvent, const BleEvent & unifiedEvent) override;

private:
    BlePlatformEfr32() = default;
    ~BlePlatformEfr32() = default;

    BLEManagerImpl * mManager = nullptr;
    uint8_t mAdvertisingSetHandle = 0xff;
    static constexpr uint8_t kMaxConnections = 8;
    BleConnectionState mConnections[kMaxConnections];
    bd_addr mRandomizedAddr = { 0 };
    bool mRandomAddrConfigured = false;
};

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
