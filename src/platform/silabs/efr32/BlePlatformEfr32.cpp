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

#include "BlePlatformEfr32.h"
#include <platform/internal/BLEManager.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>
#include <lib/support/logging/CHIPLogging.h>
#include <cstring>
#include "gatt_db.h"
#include "sl_bt_api.h"

namespace chip {
namespace DeviceLayer {
namespace Internal {
namespace Silabs {

BlePlatformEfr32::BlePlatformEfr32() : mAdvertisingHandle(kInvalidAdvertisingHandle), mManager(nullptr)
{
    // Initialize connections using default constructor
    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        mConnections[i] = BleConnectionState();
    }
}

BlePlatformEfr32 & BlePlatformEfr32::GetInstance()
{
    static BlePlatformEfr32 sInstance;
    return sInstance;
}

CHIP_ERROR BlePlatformEfr32::Init()
{
    // Platform-specific initialization is handled by BLEManagerImpl
    // This interface is initialized after the BLE stack is booted
    return CHIP_NO_ERROR;
}

void BlePlatformEfr32::Shutdown()
{
    // Stop advertising if active
    if (mAdvertisingHandle != kInvalidAdvertisingHandle)
    {
        StopAdvertising();
    }

    // Clear all connections
    // Initialize connections using default constructor
    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        mConnections[i] = BleConnectionState();
    }
    mAdvertisingHandle = kInvalidAdvertisingHandle;
    mManager           = nullptr;
}

CHIP_ERROR BlePlatformEfr32::ConfigureAdvertising(const BleAdvertisingConfig & config)
{
    sl_status_t ret;
    CHIP_ERROR err = CHIP_NO_ERROR;

    // Create advertising set if needed
    if (mAdvertisingHandle == kInvalidAdvertisingHandle)
    {
        ret = sl_bt_advertiser_create_set(&mAdvertisingHandle);
        VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));
    }

    // Set advertising data
    if (config.advData.size() > 0)
    {
        ret = sl_bt_legacy_advertiser_set_data(mAdvertisingHandle, sl_bt_advertiser_advertising_data_packet,
                                               static_cast<uint8_t>(config.advData.size()), const_cast<uint8_t *>(config.advData.data()));
        VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));
    }

    // Set scan response data
    if (config.responseData.size() > 0)
    {
        ret = sl_bt_legacy_advertiser_set_data(mAdvertisingHandle, sl_bt_advertiser_scan_response_packet,
                                               static_cast<uint8_t>(config.responseData.size()),
                                               const_cast<uint8_t *>(config.responseData.data()));
        VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));
    }

    // Set advertising timing
    ret = sl_bt_advertiser_set_timing(mAdvertisingHandle, config.intervalMin, config.intervalMax, 0, 0);
    VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));

    // Configure advertiser
    sl_bt_advertiser_configure(mAdvertisingHandle, 1);

    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformEfr32::StartAdvertising()
{
    sl_status_t ret;
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (mAdvertisingHandle == kInvalidAdvertisingHandle)
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }

    // Determine connectable mode based on number of connections
    uint8_t connectableAdv = (GetNumConnections() < kMaxConnections) ? sl_bt_advertiser_connectable_scannable
                                                                     : sl_bt_advertiser_scannable_non_connectable;

    ret = sl_bt_legacy_advertiser_start(mAdvertisingHandle, connectableAdv);
    err = MapBLEError(ret);

    return err;
}

CHIP_ERROR BlePlatformEfr32::StopAdvertising()
{
    sl_status_t ret = SL_STATUS_OK;

    if (mAdvertisingHandle != kInvalidAdvertisingHandle)
    {
        ret = sl_bt_advertiser_stop(mAdvertisingHandle);
        sl_bt_advertiser_clear_random_address(mAdvertisingHandle);
        sl_bt_advertiser_delete_set(mAdvertisingHandle);
        mAdvertisingHandle = kInvalidAdvertisingHandle;
    }

    return MapBLEError(ret);
}

bool BlePlatformEfr32::IsAdvertising() const
{
    return mAdvertisingHandle != kInvalidAdvertisingHandle;
}

uint8_t BlePlatformEfr32::GetAdvertisingHandle() const
{
    return mAdvertisingHandle;
}

CHIP_ERROR BlePlatformEfr32::AddConnection(uint8_t connectionHandle, uint8_t bondingHandle, const bd_addr & address)
{
    BleConnectionState * connState = GetConnectionState(connectionHandle, true);
    VerifyOrReturnError(connState != nullptr, CHIP_ERROR_NO_MEMORY);

    connState->connectionHandle = connectionHandle;
    connState->bondingHandle     = bondingHandle;
    connState->allocated         = 1;
    connState->address           = address;

    return CHIP_NO_ERROR;
}

bool BlePlatformEfr32::RemoveConnection(uint8_t connectionHandle)
{
    BleConnectionState * connState = GetConnectionState(connectionHandle, false);
    if (connState != nullptr && connState->allocated)
    {
        *connState = BleConnectionState(); // Use constructor to properly initialize
        return true;
    }
    return false;
}

BleConnectionState * BlePlatformEfr32::GetConnectionState(uint8_t connectionHandle, bool allocate)
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

uint16_t BlePlatformEfr32::GetNumConnections() const
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

uint16_t BlePlatformEfr32::GetMTU(uint8_t connectionHandle) const
{
    const BleConnectionState * connState = const_cast<BlePlatformEfr32 *>(this)->GetConnectionState(connectionHandle, false);
    return (connState != nullptr) ? connState->mtu : 0;
}

CHIP_ERROR BlePlatformEfr32::SendIndication(uint8_t connectionHandle, uint16_t characteristicHandle, const ByteSpan & data)
{
    sl_status_t ret = sl_bt_gatt_server_send_indication(connectionHandle, characteristicHandle, static_cast<uint16_t>(data.size()),
                                                        const_cast<uint8_t *>(data.data()));
    return MapBLEError(ret);
}

CHIP_ERROR BlePlatformEfr32::SendWriteResponse(uint8_t connectionHandle, uint16_t characteristicHandle, uint8_t status)
{
    sl_status_t ret = sl_bt_gatt_server_send_user_write_response(connectionHandle, characteristicHandle, status);
    return MapBLEError(ret);
}

CHIP_ERROR BlePlatformEfr32::SendReadResponse(uint8_t connectionHandle, uint16_t characteristicHandle, const ByteSpan & data)
{
    sl_status_t ret = sl_bt_gatt_server_send_user_read_response(connectionHandle, characteristicHandle, 0x01, 0, 0, nullptr);
    return MapBLEError(ret);
}

CHIP_ERROR BlePlatformEfr32::SetConnectionParams(uint8_t connectionHandle, const BleConnectionParams & params)
{
    sl_status_t ret;
    if (connectionHandle == 0xFF)
    {
        // Set default connection parameters
        ret = sl_bt_connection_set_default_parameters(params.intervalMin, params.intervalMax, params.latency, params.timeout,
                                                      params.minCeLength, params.maxCeLength);
    }
    else
    {
        // Set parameters for specific connection
        ret = sl_bt_connection_set_parameters(connectionHandle, params.intervalMin, params.intervalMax, params.latency,
                                               params.timeout, params.minCeLength, params.maxCeLength);
    }
    return MapBLEError(ret);
}

CHIP_ERROR BlePlatformEfr32::SetDefaultConnectionParams(const BleConnectionParams & params)
{
    sl_status_t ret = sl_bt_connection_set_default_parameters(params.intervalMin, params.intervalMax, params.latency, params.timeout,
                                                               params.minCeLength, params.maxCeLength);
    return MapBLEError(ret);
}

bool BlePlatformEfr32::CanHandleEvent(uint32_t eventType)
{
    return (eventType == sl_bt_evt_system_boot_id || eventType == sl_bt_evt_connection_opened_id ||
            eventType == sl_bt_evt_connection_parameters_id || eventType == sl_bt_evt_connection_phy_status_id ||
            eventType == sl_bt_evt_connection_data_length_id || eventType == sl_bt_evt_connection_closed_id ||
            eventType == sl_bt_evt_gatt_server_attribute_value_id || eventType == sl_bt_evt_gatt_mtu_exchanged_id ||
            eventType == sl_bt_evt_gatt_server_characteristic_status_id || eventType == sl_bt_evt_system_soft_timer_id ||
            eventType == sl_bt_evt_gatt_server_user_read_request_id || eventType == sl_bt_evt_connection_remote_used_features_id);
}

bool BlePlatformEfr32::ParseEvent(void * platformEvent, BleEvent & unifiedEvent)
{
    volatile sl_bt_msg_t * evt = static_cast<volatile sl_bt_msg_t *>(platformEvent);
    if (evt == nullptr)
    {
        return false;
    }

    uint32_t eventId = SL_BT_MSG_ID(evt->header);
    unifiedEvent.type = BleEventType::kUnknown;

    switch (eventId)
    {
    case sl_bt_evt_system_boot_id:
        unifiedEvent.type = BleEventType::kSystemBoot;
        break;

    case sl_bt_evt_connection_opened_id: {
        unifiedEvent.type = BleEventType::kConnectionOpened;
        sl_bt_evt_connection_opened_t * conn_evt = (sl_bt_evt_connection_opened_t *) &(evt->data);
        unifiedEvent.data.connectionOpened.connection  = conn_evt->connection;
        unifiedEvent.data.connectionOpened.bonding     = conn_evt->bonding;
        unifiedEvent.data.connectionOpened.advertiser   = conn_evt->advertiser;
        unifiedEvent.data.connectionOpened.address      = conn_evt->address;
        unifiedEvent.data.connectionOpened.addressType   = conn_evt->address_type;
    }
    break;

    case sl_bt_evt_connection_closed_id: {
        unifiedEvent.type = BleEventType::kConnectionClosed;
        sl_bt_evt_connection_closed_t * conn_evt = (sl_bt_evt_connection_closed_t *) &(evt->data);
        unifiedEvent.data.connectionClosed.connection = conn_evt->connection;
        unifiedEvent.data.connectionClosed.reason      = conn_evt->reason;
    }
    break;

    case sl_bt_evt_gatt_server_attribute_value_id: {
        unifiedEvent.type = BleEventType::kGattWriteRequest;
        unifiedEvent.data.gattWriteRequest.connection     = evt->data.evt_gatt_server_user_write_request.connection;
        unifiedEvent.data.gattWriteRequest.characteristic = evt->data.evt_gatt_server_user_write_request.characteristic;
        unifiedEvent.data.gattWriteRequest.attValue       = const_cast<uint8_t *>(evt->data.evt_gatt_server_user_write_request.value.data);
        unifiedEvent.data.gattWriteRequest.attValueLen    = evt->data.evt_gatt_server_user_write_request.value.len;
    }
    break;

    case sl_bt_evt_gatt_server_user_read_request_id: {
        unifiedEvent.type = BleEventType::kGattReadRequest;
        unifiedEvent.data.gattReadRequest.connection     = evt->data.evt_gatt_server_user_read_request.connection;
        unifiedEvent.data.gattReadRequest.characteristic = evt->data.evt_gatt_server_user_read_request.characteristic;
    }
    break;

    case sl_bt_evt_gatt_mtu_exchanged_id: {
        unifiedEvent.type = BleEventType::kGattMtuExchanged;
        unifiedEvent.data.mtuExchanged.connection = evt->data.evt_gatt_mtu_exchanged.connection;
        unifiedEvent.data.mtuExchanged.mtu         = evt->data.evt_gatt_mtu_exchanged.mtu;
    }
    break;

    case sl_bt_evt_gatt_server_characteristic_status_id:
        unifiedEvent.type = BleEventType::kGattCharacteristicStatus;
        unifiedEvent.data.characteristicStatus.connection     = evt->data.evt_gatt_server_characteristic_status.connection;
        unifiedEvent.data.characteristicStatus.characteristic = evt->data.evt_gatt_server_characteristic_status.characteristic;
        unifiedEvent.data.characteristicStatus.statusFlags    = evt->data.evt_gatt_server_characteristic_status.status_flags;
        break;

    case sl_bt_evt_system_soft_timer_id:
        unifiedEvent.type = BleEventType::kSoftTimer;
        break;

    case sl_bt_evt_connection_parameters_id:
        unifiedEvent.type = BleEventType::kConnectionParameters;
        break;

    default:
        return false;
    }

    return true;
}

bool BlePlatformEfr32::HandleEvent(const BleEvent & event, BLEChannel * sideChannel)
{
    // This method is a placeholder - actual event handling is done in BLEManagerImpl
    // which has access to the full context. This interface provides the abstraction
    // but the routing logic remains in BLEManagerImpl.
    return false;
}

bool BlePlatformEfr32::IsChipoBleConnection(uint8_t connectionHandle, uint8_t advertiserHandle, uint8_t chipobleAdvertisingHandle)
{
    return (advertiserHandle == chipobleAdvertisingHandle);
}

bool BlePlatformEfr32::IsChipoBleCharacteristic(uint16_t characteristicHandle)
{
    return IsChipoBleCharacteristicInternal(characteristicHandle);
}

bool BlePlatformEfr32::IsChipoBleCharacteristicInternal(uint16_t characteristic)
{
    return (gattdb_CHIPoBLEChar_Rx == characteristic || gattdb_CHIPoBLEChar_Tx == characteristic ||
            gattdb_CHIPoBLEChar_C3 == characteristic);
}

CHIP_ERROR BlePlatformEfr32::MapPlatformError(int platformError)
{
    return MapBLEError(static_cast<sl_status_t>(platformError));
}

CHIP_ERROR BlePlatformEfr32::MapBLEError(sl_status_t status)
{
    switch (status)
    {
    case SL_STATUS_OK:
        return CHIP_NO_ERROR;
    case SL_STATUS_BT_ATT_INVALID_ATT_LENGTH:
        return CHIP_ERROR_INVALID_STRING_LENGTH;
    case SL_STATUS_INVALID_PARAMETER:
        return CHIP_ERROR_INVALID_ARGUMENT;
    case SL_STATUS_INVALID_STATE:
        return CHIP_ERROR_INCORRECT_STATE;
    case SL_STATUS_NOT_SUPPORTED:
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    default:
        return CHIP_ERROR(ChipError::Range::kPlatform, status + CHIP_DEVICE_CONFIG_SILABS_BLE_ERROR_MIN);
    }
}

void BlePlatformEfr32::SetManager(void * manager)
{
    mManager = manager;
}

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
