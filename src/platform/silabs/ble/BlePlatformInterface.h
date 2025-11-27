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
 *          Platform abstraction interface for BLE operations on Silicon Labs platforms.
 */

#pragma once

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <stdint.h>

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

// Forward declarations
class BlePlatformInterface;

// Unified BLE event types
enum class BleEventType : uint8_t
{
    kConnectionOpened,
    kConnectionClosed,
    kGattWriteRequest,
    kGattMtuExchanged,
    kGattIndicationConfirmation,
    kGattReadRequest,
    kSystemBoot,
    kConnectionParameters,
    kGattCharacteristicStatus,
    kSystemSoftTimer
};

// Unified BLE event structure
struct BleEvent
{
    BleEventType type;
    union
    {
        struct
        {
            uint8_t connection;
            uint8_t bonding;
            uint8_t advertiser;
            uint8_t address[6];
        } connectionOpened;
        struct
        {
            uint8_t connection;
            uint16_t reason;
        } connectionClosed;
        struct
        {
            uint8_t connection;
            uint16_t characteristic;
            uint16_t length;
            const uint8_t * data;
        } gattWriteRequest;
        struct
        {
            uint8_t connection;
            uint16_t mtu;
        } mtuExchanged;
        struct
        {
            uint8_t connection;
            uint16_t status;
        } indicationConfirmation;
        struct
        {
            uint8_t connection;
            uint16_t characteristic;
            uint16_t offset;
        } gattReadRequest;
        struct
        {
            uint8_t connection;
            uint16_t characteristic;
            uint16_t flags;
        } characteristicStatus;
        struct
        {
            uint8_t handle;
        } softTimer;
    } data;
};

// BLE advertising configuration
struct BleAdvertisingConfig
{
    ByteSpan advData;
    ByteSpan responseData;
    uint8_t advertisingHandle;
};

// BLE connection state
struct BleConnectionState
{
    uint16_t mtu;
    uint8_t connectionHandle;
    uint8_t bondingHandle;
    uint8_t address[6];
    bool allocated;
    bool subscribed;
};

/**
 * @brief Abstract interface for platform-specific BLE operations
 *
 * This interface provides a unified abstraction for BLE operations across
 * different Silicon Labs platforms (EFR32 and SiWx917).
 */
class BlePlatformInterface
{
public:
    virtual ~BlePlatformInterface() = default;

    /**
     * @brief Initialize the BLE platform
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR Init() = 0;

    /**
     * @brief Set the BLEManagerImpl instance (for callbacks)
     * @param manager Pointer to BLEManagerImpl instance
     */
    virtual void SetManager(BLEManagerImpl * manager) = 0;

    /**
     * @brief Configure advertising data and parameters
     * @param config Advertising configuration
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR ConfigureAdvertising(const BleAdvertisingConfig & config) = 0;

    /**
     * @brief Start advertising
     * @param intervalMin Minimum advertising interval (in units of 0.625ms)
     * @param intervalMax Maximum advertising interval (in units of 0.625ms)
     * @param connectable Whether advertising should be connectable
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR StartAdvertising(uint32_t intervalMin, uint32_t intervalMax, bool connectable) = 0;

    /**
     * @brief Stop advertising
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR StopAdvertising() = 0;

    /**
     * @brief Get the advertising handle
     * @return Advertising handle (0xff if invalid)
     */
    virtual uint8_t GetAdvertisingHandle() const = 0;

    /**
     * @brief Send a GATT indication
     * @param connection Connection handle
     * @param characteristic Characteristic handle
     * @param data Data to send
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR SendIndication(uint8_t connection, uint16_t characteristic, ByteSpan data) = 0;

    /**
     * @brief Get MTU for a connection
     * @param connection Connection handle
     * @return MTU value, 0 if connection not found
     */
    virtual uint16_t GetMTU(uint8_t connection) const = 0;

    /**
     * @brief Close a BLE connection
     * @param connection Connection handle
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR CloseConnection(uint8_t connection) = 0;

    /**
     * @brief Parse a platform-specific event into unified format
     * @param platformEvent Platform-specific event pointer (void* for type erasure)
     * @param unifiedEvent Output unified event structure
     * @return true if event was parsed successfully, false otherwise
     */
    virtual bool ParseEvent(void * platformEvent, BleEvent & unifiedEvent) = 0;

    /**
     * @brief Map platform-specific error code to CHIP_ERROR
     * @param platformError Platform-specific error code
     * @return CHIP_ERROR equivalent
     */
    virtual CHIP_ERROR MapPlatformError(int32_t platformError) = 0;

    /**
     * @brief Check if an event can be handled by this platform
     * @param event Event ID/type
     * @return true if event can be handled, false otherwise
     */
    virtual bool CanHandleEvent(uint32_t event) = 0;

    /**
     * @brief Check if a characteristic is a CHIPoBLE characteristic
     * @param characteristic Characteristic handle
     * @return true if it's a CHIPoBLE characteristic, false otherwise
     */
    virtual bool IsChipoBleCharacteristic(uint16_t characteristic) const = 0;

    /**
     * @brief Check if a characteristic handle is the TX CCCD handle
     * @param characteristic Characteristic handle
     * @return true if it's the TX CCCD, false otherwise
     */
    virtual bool IsTxCccdHandle(uint16_t characteristic) const = 0;

    /**
     * @brief Check if a connection is a CHIPoBLE connection
     * @param connection Connection handle
     * @param advertiser Advertising handle that created the connection
     * @param chipoBleAdvertiser CHIPoBLE advertising handle
     * @return true if it's a CHIPoBLE connection, false otherwise
     */
    virtual bool IsChipoBleConnection(uint8_t connection, uint8_t advertiser, uint8_t chipoBleAdvertiser) const = 0;

    /**
     * @brief Get connection state for a connection
     * @param connection Connection handle
     * @param allocate If true, allocate a new state if not found
     * @return Pointer to connection state, or nullptr if not found/allocated
     */
    virtual BleConnectionState * GetConnectionState(uint8_t connection, bool allocate) = 0;

    /**
     * @brief Add a connection to the platform's connection tracking
     * @param connection Connection handle
     * @param bonding Bonding handle
     * @param address Remote device address
     */
    virtual void AddConnection(uint8_t connection, uint8_t bonding, const uint8_t * address) = 0;

    /**
     * @brief Remove a connection from the platform's connection tracking
     * @param connection Connection handle
     */
    virtual void RemoveConnection(uint8_t connection) = 0;

    /**
     * @brief Send a GATT read response
     * @param connection Connection handle
     * @param characteristic Characteristic handle
     * @param data Data to send
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR SendReadResponse(uint8_t connection, uint16_t characteristic, ByteSpan data) = 0;

    /**
     * @brief Send a GATT write response
     * @param connection Connection handle
     * @param characteristic Characteristic handle
     * @param status Status code (0 = success)
     * @return CHIP_NO_ERROR on success, error code otherwise
     */
    virtual CHIP_ERROR SendWriteResponse(uint8_t connection, uint16_t characteristic, uint8_t status) = 0;

    /**
     * @brief Handle a non-CHIPoBLE connection (platform-specific logic)
     * @param connection Connection handle
     * @param advertiser Advertising handle
     * @param bonding Bonding handle
     * @param address Remote device address
     * @param chipoBleAdvertiser CHIPoBLE advertising handle
     * @return true if connection was handled, false otherwise
     */
    virtual bool HandleNonChipoBleConnection(uint8_t connection, uint8_t advertiser, uint8_t bonding, const uint8_t * address,
                                             uint8_t chipoBleAdvertiser) = 0;

    /**
     * @brief Handle write event for CHIPoBLE connection (platform-specific logic)
     * @param platformEvent Platform-specific event
     * @param connection Connection handle
     * @param characteristic Characteristic handle
     * @return WriteType indicating how to handle the write (RX, TX_CCCD, or OTHER)
     */
    enum class WriteType
    {
        RX_CHARACTERISTIC, // Handle as RX characteristic write
        TX_CCCD,           // Handle as TX CCCD write
        OTHER_CHIPOBLE     // Other CHIPoBLE characteristic
    };
    virtual WriteType HandleChipoBleWrite(void * platformEvent, uint8_t connection, uint16_t characteristic) = 0;

    /**
     * @brief Handle non-CHIPoBLE write (platform-specific logic, e.g., side channel)
     * @param platformEvent Platform-specific event
     * @param connection Connection handle
     * @param characteristic Characteristic handle
     * @return true if write was handled, false otherwise
     */
    virtual bool HandleNonChipoBleWrite(void * platformEvent, uint8_t connection, uint16_t characteristic) = 0;

    /**
     * @brief Handle non-CHIPoBLE read (platform-specific logic, e.g., side channel)
     * @param platformEvent Platform-specific event
     * @param connection Connection handle
     * @param characteristic Characteristic handle
     * @return true if read was handled, false otherwise
     */
    virtual bool HandleNonChipoBleRead(void * platformEvent, uint8_t connection, uint16_t characteristic) = 0;

    /**
     * @brief Handle non-CHIPoBLE MTU update (platform-specific logic, e.g., side channel)
     * @param platformEvent Platform-specific event
     * @param connection Connection handle
     * @return true if MTU update was handled, false otherwise
     */
    virtual bool HandleNonChipoBleMtuUpdate(void * platformEvent, uint8_t connection) = 0;

    /**
     * @brief Map platform-specific disconnect reason code to CHIP BLE error reason
     * @param platformReason Platform-specific reason code
     * @return CHIP BLE error reason code
     */
    virtual CHIP_ERROR MapDisconnectReason(uint16_t platformReason) = 0;

    /**
     * @brief Handle non-CHIPoBLE disconnect (platform-specific logic, e.g., side channel)
     * @param platformEvent Platform-specific event
     * @param connection Connection handle
     * @return true if disconnect was handled, false otherwise
     */
    virtual bool HandleNonChipoBleDisconnect(void * platformEvent, uint8_t connection) = 0;

    /**
     * @brief Result structure for TX CCCD write handling
     */
    struct TxCccdWriteResult
    {
        bool handled;             // Whether the CCCD write was handled
        bool isIndicationEnabled; // Whether indication is enabled (only valid if handled is true)
        uint8_t connection;       // Connection handle (only valid if handled is true)
    };

    /**
     * @brief Handle TX CCCD write (platform-specific logic)
     * @param platformEvent Platform-specific event
     * @param unifiedEvent Unified event structure
     * @return Result structure indicating if handled and indication state
     */
    virtual TxCccdWriteResult HandleTxCccdWrite(void * platformEvent, const BleEvent & unifiedEvent) = 0;

    /**
     * @brief Handle non-CHIPoBLE CCCD write (platform-specific logic, e.g., side channel)
     * @param platformEvent Platform-specific event
     * @param unifiedEvent Unified event structure
     * @return true if CCCD write was handled, false otherwise
     */
    virtual bool HandleNonChipoBleCccdWrite(void * platformEvent, const BleEvent & unifiedEvent) = 0;
};

/**
 * @brief Factory function to get the platform-specific BLE instance
 * @return Pointer to platform-specific BlePlatformInterface instance
 */
BlePlatformInterface * GetBlePlatformInstance();

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
