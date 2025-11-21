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
 *          rs911x/SiWx917-specific implementation of BlePlatformInterface
 */

#include "BlePlatformRs911x.h"
#include <lib/support/logging/CHIPLogging.h>
#include <platform/silabs/CHIPDevicePlatformConfig.h>
#include <cstring>
#include <cstdlib>
#include "wfx_sl_ble_init.h"

extern "C" {
#include <rsi_ble.h>
#include <rsi_ble_apis.h>
#include <rsi_utils.h>
}

namespace chip {
namespace DeviceLayer {
namespace Internal {
namespace Silabs {

// External variables from BLEManagerImpl
extern uint8_t dev_address[RSI_DEV_ADDR_LEN];
extern uint16_t rsi_ble_measurement_hndl;
extern uint16_t rsi_ble_gatt_server_client_config_hndl;

BlePlatformRs911x::BlePlatformRs911x() : mAdvertisingHandle(kInvalidAdvertisingHandle), mAdvertising(false), mManager(nullptr)
{
    // Initialize connections using default constructor
    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        mConnections[i] = BleConnectionState();
    }
}

BlePlatformRs911x & BlePlatformRs911x::GetInstance()
{
    static BlePlatformRs911x sInstance;
    return sInstance;
}

CHIP_ERROR BlePlatformRs911x::Init()
{
    // Platform-specific initialization is handled by BLEManagerImpl
    // This interface is initialized after the BLE stack is booted
    return CHIP_NO_ERROR;
}

void BlePlatformRs911x::Shutdown()
{
    // Stop advertising if active
    if (mAdvertising)
    {
        StopAdvertising();
    }

    // Clear all connections
    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        mConnections[i] = BleConnectionState();
    }
    mAdvertisingHandle = kInvalidAdvertisingHandle;
    mAdvertising       = false;
    mManager           = nullptr;
}

CHIP_ERROR BlePlatformRs911x::ConfigureAdvertising(const BleAdvertisingConfig & config)
{
    int32_t ret;

    // Set advertising data
    if (config.advData.size() > 0)
    {
        ret = rsi_ble_set_advertise_data(const_cast<uint8_t *>(config.advData.data()), static_cast<uint8_t>(config.advData.size()));
        VerifyOrReturnError(ret == RSI_SUCCESS, MapRSIError(ret));
    }

    // Set scan response data
    if (config.responseData.size() > 0)
    {
        ret = rsi_ble_set_scan_response_data(const_cast<uint8_t *>(config.responseData.data()),
                                              static_cast<uint8_t>(config.responseData.size()));
        VerifyOrReturnError(ret == RSI_SUCCESS, MapRSIError(ret));
    }

    // Store advertising handle if provided
    if (config.advertisingHandle != kInvalidAdvertisingHandle)
    {
        mAdvertisingHandle = config.advertisingHandle;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformRs911x::StartAdvertising()
{
    // Note: RSI BLE advertising is started via SendBLEAdvertisementCommand in BLEManagerImpl
    // This method is a placeholder for the interface
    mAdvertising = true;
    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformRs911x::StopAdvertising()
{
    int32_t ret = rsi_ble_stop_advertising();
    CHIP_ERROR err = MapRSIError(ret);

    if (err == CHIP_NO_ERROR)
    {
        mAdvertising = false;
        mAdvertisingHandle = kInvalidAdvertisingHandle;
    }

    return err;
}

bool BlePlatformRs911x::IsAdvertising() const
{
    return mAdvertising;
}

uint8_t BlePlatformRs911x::GetAdvertisingHandle() const
{
    return mAdvertisingHandle;
}

CHIP_ERROR BlePlatformRs911x::AddConnection(uint8_t connectionHandle, uint8_t bondingHandle, const bd_addr & address)
{
    BleConnectionState * connState = GetConnectionState(connectionHandle, true);
    VerifyOrReturnError(connState != nullptr, CHIP_ERROR_NO_MEMORY);

    connState->connectionHandle = connectionHandle;
    connState->bondingHandle     = bondingHandle;
    connState->allocated         = 1;
    // Note: RSI BLE uses fixed connection handle, address is stored separately in dev_address

    return CHIP_NO_ERROR;
}

bool BlePlatformRs911x::RemoveConnection(uint8_t connectionHandle)
{
    BleConnectionState * connState = GetConnectionState(connectionHandle, false);
    if (connState != nullptr && connState->allocated)
    {
        *connState = BleConnectionState(); // Use constructor to properly initialize
        return true;
    }
    return false;
}

BleConnectionState * BlePlatformRs911x::GetConnectionState(uint8_t connectionHandle, bool allocate)
{
    uint8_t freeIndex = kMaxConnections;

    // First, try to find existing connection
    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        if (mConnections[i].allocated && mConnections[i].connectionHandle == connectionHandle)
        {
            return &mConnections[i];
        }
        else if (!mConnections[i].allocated && i < freeIndex)
        {
            freeIndex = i;
        }
    }

    // If not found and allocate is true, allocate a new one
    if (allocate && freeIndex < kMaxConnections)
    {
        return &mConnections[freeIndex];
    }

    return nullptr;
}

uint16_t BlePlatformRs911x::GetNumConnections() const
{
    uint16_t count = 0;
    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        if (mConnections[i].allocated)
        {
            count++;
        }
    }
    return count;
}

uint16_t BlePlatformRs911x::GetMTU(uint8_t connectionHandle) const
{
    const BleConnectionState * connState = const_cast<BlePlatformRs911x *>(this)->GetConnectionState(connectionHandle, false);
    return (connState != nullptr) ? connState->mtu : 0;
}

CHIP_ERROR BlePlatformRs911x::SendIndication(uint8_t connectionHandle, uint16_t characteristicHandle, const ByteSpan & data)
{
    // RSI BLE uses dev_address instead of connection handle for indication
    int32_t ret = rsi_ble_indicate_value(Silabs::dev_address, characteristicHandle, static_cast<uint16_t>(data.size()),
                                         const_cast<uint8_t *>(data.data()));
    if (ret != RSI_SUCCESS)
    {
        return MapRSIError(ret);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformRs911x::SendWriteResponse(uint8_t connectionHandle, uint16_t characteristicHandle, uint8_t status)
{
    // RSI BLE doesn't have explicit write response API in the same way
    // Write responses are handled automatically by the stack
    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformRs911x::SendReadResponse(uint8_t connectionHandle, uint16_t characteristicHandle, const ByteSpan & data)
{
    // RSI BLE read responses are handled via rsi_ble_set_att_value or similar
    // This is platform-specific and may need to be implemented differently
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR BlePlatformRs911x::SetConnectionParams(uint8_t connectionHandle, const BleConnectionParams & params)
{
    // RSI BLE uses rsi_ble_conn_params_update with device address
    int32_t ret = rsi_ble_conn_params_update(Silabs::dev_address, params.intervalMin, params.intervalMax, params.latency, params.timeout);
    return MapRSIError(ret);
}

CHIP_ERROR BlePlatformRs911x::SetDefaultConnectionParams(const BleConnectionParams & params)
{
    // RSI BLE doesn't have explicit default connection params API
    // Connection params are set per connection via SetConnectionParams
    return CHIP_NO_ERROR;
}

bool BlePlatformRs911x::CanHandleEvent(uint32_t eventType)
{
    // RSI BLE uses different event types via SilabsBleWrapper::BleEventType enum
    // For now, return true for all events as they're handled via the wrapper
    return true;
}

bool BlePlatformRs911x::ParseEvent(void * platformEvent, BleEvent & unifiedEvent)
{
    SilabsBleWrapper::BleEvent_t * rsiEvent = static_cast<SilabsBleWrapper::BleEvent_t *>(platformEvent);
    if (rsiEvent == nullptr)
    {
        return false;
    }

    unifiedEvent.type = BleEventType::kUnknown;

    switch (rsiEvent->eventType)
    {
    case SilabsBleWrapper::BleEventType::RSI_BLE_CONN_EVENT: {
        unifiedEvent.type = BleEventType::kConnectionOpened;
        unifiedEvent.data.connectionOpened.connection  = kDefaultConnectionHandle; // RSI uses fixed handle
        unifiedEvent.data.connectionOpened.bonding     = 0; // Not used in RSI
        unifiedEvent.data.connectionOpened.advertiser   = 0; // Not used in RSI
        // Address is in dev_address, not in event
        unifiedEvent.data.connectionOpened.address = bd_addr{};
        unifiedEvent.data.connectionOpened.addressType = 0;
    }
    break;

    case SilabsBleWrapper::BleEventType::RSI_BLE_DISCONN_EVENT: {
        unifiedEvent.type = BleEventType::kConnectionClosed;
        unifiedEvent.data.connectionClosed.connection = kDefaultConnectionHandle;
        unifiedEvent.data.connectionClosed.reason      = rsiEvent->eventData.reason;
    }
    break;

    case SilabsBleWrapper::BleEventType::RSI_BLE_GATT_WRITE_EVENT: {
        unifiedEvent.type = BleEventType::kGattWriteRequest;
        const auto & writeData = rsiEvent->eventData.rsi_ble_write;
        unifiedEvent.data.gattWriteRequest.connection     = kDefaultConnectionHandle;
        unifiedEvent.data.gattWriteRequest.characteristic = writeData.handle[0];
        unifiedEvent.data.gattWriteRequest.attValue       = const_cast<uint8_t *>(writeData.att_value);
        unifiedEvent.data.gattWriteRequest.attValueLen    = writeData.length;
    }
    break;

    case SilabsBleWrapper::BleEventType::RSI_BLE_EVENT_GATT_RD: {
        unifiedEvent.type = BleEventType::kGattReadRequest;
        unifiedEvent.data.gattReadRequest.connection     = kDefaultConnectionHandle;
        unifiedEvent.data.gattReadRequest.characteristic = rsiEvent->eventData.rsi_ble_read_req->handle;
    }
    break;

    case SilabsBleWrapper::BleEventType::RSI_BLE_MTU_EVENT: {
        unifiedEvent.type = BleEventType::kGattMtuExchanged;
        const auto & mtuData = rsiEvent->eventData.rsi_ble_mtu;
        unifiedEvent.data.mtuExchanged.connection = kDefaultConnectionHandle; // RSI uses fixed handle
        unifiedEvent.data.mtuExchanged.mtu         = mtuData.mtu_size;
    }
    break;

    default:
        return false;
    }

    return true;
}

bool BlePlatformRs911x::HandleEvent(const BleEvent & event, BLEChannel * sideChannel)
{
    // This method is a placeholder - actual event handling is done in BLEManagerImpl
    return false;
}

bool BlePlatformRs911x::IsChipoBleConnection(uint8_t connectionHandle, uint8_t advertiserHandle, uint8_t chipobleAdvertisingHandle)
{
    // RSI BLE uses fixed connection handle, so all connections are CHIPoBLE if they exist
    return (connectionHandle == kDefaultConnectionHandle);
}

bool BlePlatformRs911x::IsChipoBleCharacteristic(uint16_t characteristicHandle)
{
    // RSI BLE uses rsi_ble_measurement_hndl and rsi_ble_gatt_server_client_config_hndl
    // Check if the handle matches CHIPoBLE characteristics
    return (characteristicHandle == rsi_ble_measurement_hndl ||
            characteristicHandle == rsi_ble_gatt_server_client_config_hndl);
}

CHIP_ERROR BlePlatformRs911x::MapPlatformError(int platformError)
{
    return MapRSIError(platformError);
}

CHIP_ERROR BlePlatformRs911x::MapRSIError(int32_t status)
{
    if (status == RSI_SUCCESS)
    {
        return CHIP_NO_ERROR;
    }

    // Map RSI error codes to CHIP errors
    // RSI BLE uses int32_t status codes
    // Map common error patterns to CHIP errors
    if (status == RSI_FAILURE)
    {
        return CHIP_ERROR_INTERNAL;
    }

    // For other error codes, use generic platform error mapping
    uint32_t absStatus = (status < 0) ? static_cast<uint32_t>(-status) : static_cast<uint32_t>(status);
    return CHIP_ERROR(ChipError::Range::kPlatform, absStatus + CHIP_DEVICE_CONFIG_SILABS_BLE_ERROR_MIN);
}

void BlePlatformRs911x::SetManager(void * manager)
{
    mManager = manager;
}

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
