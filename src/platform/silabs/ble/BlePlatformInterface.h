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
 *          Abstract interface for BLE platform operations
 */

#pragma once

#include "BlePlatformTypes.h"
#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <stdint.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

// Forward declaration
class BLEChannel;

namespace Silabs {

/**
 * @brief Abstract interface for platform-specific BLE operations
 *
 * This interface abstracts platform-specific BLE stack operations,
 * allowing BLEManagerImpl to work with different BLE stacks (EFR32 sl_bt_api,
 * SiWx917 RSI BLE, etc.) through a common interface.
 */
class BlePlatformInterface
{
public:
    virtual ~BlePlatformInterface() = default;

    // ===== Initialization and Shutdown =====

    /**
     * @brief Initialize the BLE platform interface
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR Init() = 0;

    /**
     * @brief Shutdown the BLE platform interface
     */
    virtual void Shutdown() = 0;

    // ===== Advertising Operations =====

    /**
     * @brief Configure advertising data and parameters
     * @param config Advertising configuration
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR ConfigureAdvertising(const BleAdvertisingConfig & config) = 0;

    /**
     * @brief Start advertising
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR StartAdvertising() = 0;

    /**
     * @brief Stop advertising
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR StopAdvertising() = 0;

    /**
     * @brief Check if advertising is currently active
     * @return true if advertising, false otherwise
     */
    virtual bool IsAdvertising() const = 0;

    /**
     * @brief Get the advertising handle
     * @return Advertising handle (0xFF if not set)
     */
    virtual uint8_t GetAdvertisingHandle() const = 0;

    // ===== Connection Management =====

    /**
     * @brief Add a connection to the connection state
     * @param connectionHandle Connection handle
     * @param bondingHandle Bonding handle
     * @param address Remote device address
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR AddConnection(uint8_t connectionHandle, uint8_t bondingHandle, const bd_addr & address) = 0;

    /**
     * @brief Remove a connection from the connection state
     * @param connectionHandle Connection handle
     * @return true if connection was found and removed, false otherwise
     */
    virtual bool RemoveConnection(uint8_t connectionHandle) = 0;

    /**
     * @brief Get connection state for a connection handle
     * @param connectionHandle Connection handle
     * @param allocate If true, allocate state if not found
     * @return Pointer to connection state, or nullptr if not found/allocated
     */
    virtual BleConnectionState * GetConnectionState(uint8_t connectionHandle, bool allocate = false) = 0;

    /**
     * @brief Get number of active connections
     * @return Number of active connections
     */
    virtual uint16_t GetNumConnections() const = 0;

    /**
     * @brief Get MTU for a connection
     * @param connectionHandle Connection handle
     * @return MTU value, or 0 if connection not found
     */
    virtual uint16_t GetMTU(uint8_t connectionHandle) const = 0;

    // ===== GATT Operations =====

    /**
     * @brief Send GATT indication
     * @param connectionHandle Connection handle
     * @param characteristicHandle Characteristic handle
     * @param data Data to send
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR SendIndication(uint8_t connectionHandle, uint16_t characteristicHandle, const ByteSpan & data) = 0;

    /**
     * @brief Send GATT write response
     * @param connectionHandle Connection handle
     * @param characteristicHandle Characteristic handle
     * @param status Status code (0 = success)
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR SendWriteResponse(uint8_t connectionHandle, uint16_t characteristicHandle, uint8_t status) = 0;

    /**
     * @brief Send GATT read response
     * @param connectionHandle Connection handle
     * @param characteristicHandle Characteristic handle
     * @param data Data to send
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR SendReadResponse(uint8_t connectionHandle, uint16_t characteristicHandle, const ByteSpan & data) = 0;

    // ===== Connection Parameters =====

    /**
     * @brief Set connection parameters
     * @param connectionHandle Connection handle (0xFF for default)
     * @param params Connection parameters
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR SetConnectionParams(uint8_t connectionHandle, const BleConnectionParams & params) = 0;

    /**
     * @brief Set default connection parameters
     * @param params Connection parameters
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR SetDefaultConnectionParams(const BleConnectionParams & params) = 0;

    // ===== Event Handling =====

    /**
     * @brief Check if this platform can handle a specific event type
     * @param eventType Event type ID (platform-specific)
     * @return true if event can be handled, false otherwise
     */
    virtual bool CanHandleEvent(uint32_t eventType) = 0;

    /**
     * @brief Parse a platform-specific event and convert to unified event
     * @param platformEvent Platform-specific event pointer (void* for type erasure)
     * @param unifiedEvent Output unified event structure
     * @return true if event was parsed successfully, false otherwise
     */
    virtual bool ParseEvent(void * platformEvent, BleEvent & unifiedEvent) = 0;

    /**
     * @brief Handle a unified BLE event
     * @param event Unified event structure
     * @param sideChannel Optional side channel instance for event routing
     * @return true if event was handled, false otherwise
     */
    virtual bool HandleEvent(const BleEvent & event, BLEChannel * sideChannel) = 0;

    // ===== Side Channel Support =====

    /**
     * @brief Check if a connection belongs to CHIPoBLE or side channel
     * @param connectionHandle Connection handle
     * @param advertiserHandle Advertising handle from connection event
     * @param chipobleAdvertisingHandle CHIPoBLE advertising handle
     * @return true if CHIPoBLE connection, false if side channel or unknown
     */
    virtual bool IsChipoBleConnection(uint8_t connectionHandle, uint8_t advertiserHandle, uint8_t chipobleAdvertisingHandle) = 0;

    /**
     * @brief Check if a characteristic belongs to CHIPoBLE
     * @param characteristicHandle Characteristic handle
     * @return true if CHIPoBLE characteristic, false otherwise
     */
    virtual bool IsChipoBleCharacteristic(uint16_t characteristicHandle) = 0;

    // ===== Platform-Specific Helpers =====

    /**
     * @brief Get platform-specific error code mapping
     * @param platformError Platform-specific error code
     * @return CHIP error code
     */
    virtual CHIP_ERROR MapPlatformError(int platformError) = 0;

    /**
     * @brief Set the manager instance (for callbacks)
     * @param manager Pointer to BLEManagerImpl instance
     */
    virtual void SetManager(void * manager) = 0;
};

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
