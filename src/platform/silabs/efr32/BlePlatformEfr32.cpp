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

// Include CHIPDeviceLayerInternal.h first to establish platform layer context
#include <platform/internal/CHIPDeviceLayerInternal.h>
// Then include BLE headers - BLEManagerImpl.h must come before BlePlatformEfr32.h
// to ensure BLEManagerImpl type is complete for CRTP template instantiation
#include <platform/silabs/BLEManagerImpl.h>
#include <platform/silabs/efr32/BlePlatformEfr32.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <crypto/RandUtils.h>
#include <cstring>

extern "C" {
#include "sl_bluetooth.h"
}
#include "sl_bt_api.h"
#include "sl_bt_stack_config.h"
#include "sl_bt_stack_init.h"
#include "gatt_db.h"
#include <ble/Ble.h>

using namespace ::chip;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceLayer::Internal;
using namespace ::chip::DeviceLayer::Internal::Silabs;

namespace {

inline constexpr uint8_t kInvalidAdvertisingHandle = 0xff;

bool isMATTERoBLECharacteristic(uint16_t characteristic)
{
    return (gattdb_CHIPoBLEChar_Rx == characteristic || gattdb_CHIPoBLEChar_Tx == characteristic ||
            gattdb_CHIPoBLEChar_C3 == characteristic);
}

} // namespace

CHIP_ERROR BlePlatformEfr32::Init()
{
    // Check that an address was not already configured at boot.
    // This covers the init-shutdown-init case to comply with the BLE address change at boot only requirement
    bd_addr temp = { 0 };
    if (!mRandomAddrConfigured && memcmp(&mRandomizedAddr, &temp, sizeof(bd_addr)) == 0)
    {
        // Since a random address is not configured, configure one
        uint64_t random = Crypto::GetRandU64();
        // Copy random value to address. We don't care of the ordering since it's a random value.
        memcpy(&mRandomizedAddr, &random, sizeof(mRandomizedAddr));

        // Set two MSBs to 11 to properly the address - BLE Static Device Address requirement
        mRandomizedAddr.addr[5] |= 0xC0;
        mRandomAddrConfigured = true;
    }

    memset(mConnections, 0, sizeof(mConnections));
    return CHIP_NO_ERROR;
}

void BlePlatformEfr32::SetManager(BLEManagerImpl * manager)
{
    mManager = manager;
}

CHIP_ERROR BlePlatformEfr32::ConfigureAdvertising(const BleAdvertisingConfig & config)
{
    sl_status_t ret;
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (mAdvertisingSetHandle == kInvalidAdvertisingHandle)
    {
        ret = sl_bt_advertiser_create_set(&mAdvertisingSetHandle);
        VerifyOrExit(ret == SL_STATUS_OK, {
            err = MapPlatformError(ret);
            ChipLogError(DeviceLayer, "sl_bt_advertiser_create_set() failed: %" CHIP_ERROR_FORMAT, err.Format());
        });

        ret = sl_bt_advertiser_set_random_address(mAdvertisingSetHandle, sl_bt_gap_static_address, mRandomizedAddr,
                                                   &mRandomizedAddr);
        VerifyOrExit(ret == SL_STATUS_OK, {
            err = MapPlatformError(ret);
            ChipLogError(DeviceLayer, "sl_bt_advertiser_set_random_address() failed: %" CHIP_ERROR_FORMAT, err.Format());
        });
        ChipLogDetail(DeviceLayer, "BLE Static Device Address %02X:%02X:%02X:%02X:%02X:%02X", mRandomizedAddr.addr[5],
                      mRandomizedAddr.addr[4], mRandomizedAddr.addr[3], mRandomizedAddr.addr[2], mRandomizedAddr.addr[1],
                      mRandomizedAddr.addr[0]);
    }

    if (config.advData.size() > 0)
    {
        ret = sl_bt_legacy_advertiser_set_data(mAdvertisingSetHandle, sl_bt_advertiser_advertising_data_packet,
                                               config.advData.size(), const_cast<uint8_t *>(config.advData.data()));
        VerifyOrExit(ret == SL_STATUS_OK, {
            err = MapPlatformError(ret);
            ChipLogError(DeviceLayer, "sl_bt_legacy_advertiser_set_data() - Advertising Data failed: %" CHIP_ERROR_FORMAT,
                         err.Format());
        });
    }

    if (config.responseData.size() > 0)
    {
        ret = sl_bt_legacy_advertiser_set_data(mAdvertisingSetHandle, sl_bt_advertiser_scan_response_packet,
                                               config.responseData.size(), const_cast<uint8_t *>(config.responseData.data()));
        VerifyOrExit(ret == SL_STATUS_OK, {
            err = MapPlatformError(ret);
            ChipLogError(DeviceLayer, "sl_bt_legacy_advertiser_set_data() - Scan Response failed: %" CHIP_ERROR_FORMAT,
                         err.Format());
        });
    }

exit:
    return err;
}

CHIP_ERROR BlePlatformEfr32::StartAdvertising(uint32_t intervalMin, uint32_t intervalMax, bool connectable)
{
    sl_status_t ret;
    CHIP_ERROR err = CHIP_NO_ERROR;
    uint8_t connectableAdv = connectable ? sl_bt_advertiser_connectable_scannable : sl_bt_advertiser_scannable_non_connectable;

    ChipLogProgress(DeviceLayer, "Starting advertising with interval_min=%u, interval_max=%u (units of 625us)",
                    static_cast<unsigned>(intervalMin), static_cast<unsigned>(intervalMax));

    ret = sl_bt_advertiser_set_timing(mAdvertisingSetHandle, intervalMin, intervalMax, 0, 0);
    err = MapPlatformError(ret);
    SuccessOrExit(err);

    sl_bt_advertiser_configure(mAdvertisingSetHandle, 1);

    ret = sl_bt_legacy_advertiser_start(mAdvertisingSetHandle, connectableAdv);
    err = MapPlatformError(ret);

exit:
    return err;
}

CHIP_ERROR BlePlatformEfr32::StopAdvertising()
{
    sl_status_t ret;
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (mAdvertisingSetHandle != kInvalidAdvertisingHandle)
    {
        ret = sl_bt_advertiser_stop(mAdvertisingSetHandle);
        sl_bt_advertiser_clear_random_address(mAdvertisingSetHandle);
        sl_bt_advertiser_delete_set(mAdvertisingSetHandle);
        mAdvertisingSetHandle = kInvalidAdvertisingHandle;
        err                   = MapPlatformError(ret);
    }

    return err;
}

CHIP_ERROR BlePlatformEfr32::SendIndication(uint8_t connection, uint16_t characteristic, ByteSpan data)
{
    sl_status_t ret = sl_bt_gatt_server_send_indication(connection, characteristic, data.size(), const_cast<uint8_t *>(data.data()));
    CHIP_ERROR err = MapPlatformError(ret);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "sl_bt_gatt_server_send_indication failed: 0x%lx (CHIP error: %" CHIP_ERROR_FORMAT ")", 
                     ret, err.Format());
    }
    return err;
}

uint16_t BlePlatformEfr32::GetMTU(uint8_t connection) const
{
    BleConnectionState * conState = const_cast<BlePlatformEfr32 *>(this)->GetConnectionState(connection, false);
    return (conState != nullptr) ? conState->mtu : 0;
}

CHIP_ERROR BlePlatformEfr32::CloseConnection(uint8_t connection)
{
    sl_status_t ret = sl_bt_connection_close(connection);
    return MapPlatformError(ret);
}

bool BlePlatformEfr32::ParseEvent(void * platformEvent, BleEvent & unifiedEvent)
{
    volatile sl_bt_msg_t * evt = static_cast<volatile sl_bt_msg_t *>(platformEvent);
    if (evt == nullptr)
    {
        return false;
    }

    uint32_t eventId = SL_BT_MSG_ID(evt->header);

    switch (eventId)
    {
    case sl_bt_evt_system_boot_id:
        unifiedEvent.type = BleEventType::kSystemBoot;
        return true;

    case sl_bt_evt_connection_opened_id: {
        sl_bt_evt_connection_opened_t * conn_evt = (sl_bt_evt_connection_opened_t *) &(evt->data);
        unifiedEvent.type                         = BleEventType::kConnectionOpened;
        unifiedEvent.data.connectionOpened.connection = conn_evt->connection;
        unifiedEvent.data.connectionOpened.bonding   = conn_evt->bonding;
        unifiedEvent.data.connectionOpened.advertiser = conn_evt->advertiser;
        memcpy(unifiedEvent.data.connectionOpened.address, conn_evt->address.addr, 6);
        return true;
    }

    case sl_bt_evt_connection_closed_id: {
        sl_bt_evt_connection_closed_t * conn_evt = (sl_bt_evt_connection_closed_t *) &(evt->data);
        unifiedEvent.type                        = BleEventType::kConnectionClosed;
        unifiedEvent.data.connectionClosed.connection = conn_evt->connection;
        unifiedEvent.data.connectionClosed.reason     = conn_evt->reason;
        return true;
    }

    case sl_bt_evt_gatt_server_attribute_value_id: {
        sl_bt_evt_gatt_server_attribute_value_t * write_evt = (sl_bt_evt_gatt_server_attribute_value_t *) &(evt->data);
        unifiedEvent.type                                    = BleEventType::kGattWriteRequest;
        unifiedEvent.data.gattWriteRequest.connection       = write_evt->connection;
        unifiedEvent.data.gattWriteRequest.characteristic   = write_evt->attribute;
        unifiedEvent.data.gattWriteRequest.length           = write_evt->value.len;
        unifiedEvent.data.gattWriteRequest.data             = write_evt->value.data;
        return true;
    }

    case sl_bt_evt_gatt_mtu_exchanged_id: {
        sl_bt_evt_gatt_mtu_exchanged_t * mtu_evt = (sl_bt_evt_gatt_mtu_exchanged_t *) &(evt->data);
        unifiedEvent.type                       = BleEventType::kGattMtuExchanged;
        unifiedEvent.data.mtuExchanged.connection = mtu_evt->connection;
        unifiedEvent.data.mtuExchanged.mtu        = mtu_evt->mtu;
        return true;
    }

    case sl_bt_evt_gatt_server_characteristic_status_id: {
        sl_bt_evt_gatt_server_characteristic_status_t * status_evt = (sl_bt_evt_gatt_server_characteristic_status_t *) &(evt->data);
        if (status_evt->status_flags == sl_bt_gatt_server_confirmation)
        {
            unifiedEvent.type                                    = BleEventType::kGattIndicationConfirmation;
            unifiedEvent.data.indicationConfirmation.connection = status_evt->connection;
            unifiedEvent.data.indicationConfirmation.status     = 0; // Success
        }
        else
        {
            unifiedEvent.type                                    = BleEventType::kGattCharacteristicStatus;
            unifiedEvent.data.characteristicStatus.connection   = status_evt->connection;
            unifiedEvent.data.characteristicStatus.characteristic = status_evt->characteristic;
            unifiedEvent.data.characteristicStatus.flags         = status_evt->client_config_flags;
        }
        return true;
    }

    case sl_bt_evt_gatt_server_user_read_request_id: {
        sl_bt_evt_gatt_server_user_read_request_t * read_evt = (sl_bt_evt_gatt_server_user_read_request_t *) &(evt->data);
        unifiedEvent.type                                      = BleEventType::kGattReadRequest;
        unifiedEvent.data.gattReadRequest.connection          = read_evt->connection;
        unifiedEvent.data.gattReadRequest.characteristic     = read_evt->characteristic;
        unifiedEvent.data.gattReadRequest.offset             = 0;
        return true;
    }

    case sl_bt_evt_system_soft_timer_id: {
        sl_bt_evt_system_soft_timer_t * timer_evt = (sl_bt_evt_system_soft_timer_t *) &(evt->data);
        unifiedEvent.type                         = BleEventType::kSystemSoftTimer;
        unifiedEvent.data.softTimer.handle        = timer_evt->handle;
        return true;
    }

    case sl_bt_evt_connection_parameters_id:
        unifiedEvent.type = BleEventType::kConnectionParameters;
        return true;

    default:
        return false;
    }
}

CHIP_ERROR BlePlatformEfr32::MapPlatformError(int32_t platformError)
{
    switch (platformError)
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
        return CHIP_ERROR(ChipError::Range::kPlatform, platformError + CHIP_DEVICE_CONFIG_SILABS_BLE_ERROR_MIN);
    }
}

bool BlePlatformEfr32::CanHandleEvent(uint32_t event)
{
    return (event == sl_bt_evt_system_boot_id || event == sl_bt_evt_connection_opened_id ||
            event == sl_bt_evt_connection_parameters_id || event == sl_bt_evt_connection_phy_status_id ||
            event == sl_bt_evt_connection_data_length_id || event == sl_bt_evt_connection_closed_id ||
            event == sl_bt_evt_gatt_server_attribute_value_id || event == sl_bt_evt_gatt_mtu_exchanged_id ||
            event == sl_bt_evt_gatt_server_characteristic_status_id || event == sl_bt_evt_system_soft_timer_id ||
            event == sl_bt_evt_gatt_server_user_read_request_id || event == sl_bt_evt_connection_remote_used_features_id);
}

bool BlePlatformEfr32::IsChipoBleCharacteristic(uint16_t characteristic) const
{
    return isMATTERoBLECharacteristic(characteristic);
}

bool BlePlatformEfr32::IsChipoBleConnection(uint8_t connection, uint8_t advertiser, uint8_t chipoBleAdvertiser) const
{
    return (advertiser == chipoBleAdvertiser);
}

BleConnectionState * BlePlatformEfr32::GetConnectionState(uint8_t connection, bool allocate)
{
    uint8_t freeIndex = kMaxConnections;

    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        if (mConnections[i].allocated)
        {
            if (mConnections[i].connectionHandle == connection)
            {
                return &mConnections[i];
            }
        }
        else if (i < freeIndex)
        {
            freeIndex = i;
        }
    }

    if (allocate && freeIndex < kMaxConnections)
    {
        memset(&mConnections[freeIndex], 0, sizeof(BleConnectionState));
        mConnections[freeIndex].connectionHandle = connection;
        mConnections[freeIndex].allocated        = true;
        return &mConnections[freeIndex];
    }

    return nullptr;
}

void BlePlatformEfr32::AddConnection(uint8_t connection, uint8_t bonding, const uint8_t * address)
{
    BleConnectionState * conState = GetConnectionState(connection, true);
    if (conState != nullptr)
    {
        conState->bondingHandle = bonding;
        if (address != nullptr)
        {
            memcpy(conState->address, address, 6);
        }
    }
}

void BlePlatformEfr32::RemoveConnection(uint8_t connection)
{
    BleConnectionState * conState = GetConnectionState(connection, false);
    if (conState != nullptr)
    {
        memset(conState, 0, sizeof(BleConnectionState));
    }
}

CHIP_ERROR BlePlatformEfr32::SendReadResponse(uint8_t connection, uint16_t characteristic, ByteSpan data)
{
    sl_status_t ret = sl_bt_gatt_server_send_user_read_response(connection, characteristic, 0, data.size(),
                                                                 const_cast<uint8_t *>(data.data()), nullptr);
    return MapPlatformError(ret);
}

CHIP_ERROR BlePlatformEfr32::SendWriteResponse(uint8_t connection, uint16_t characteristic, uint8_t status)
{
    sl_status_t ret = sl_bt_gatt_server_send_user_write_response(connection, characteristic, status);
    return MapPlatformError(ret);
}

bool BlePlatformEfr32::HandleNonChipoBleConnection(uint8_t connection, uint8_t advertiser, uint8_t bonding, const uint8_t * address,
                                                    uint8_t chipoBleAdvertiser)
{
    // EFR32: Handle side channel connection if it's not a CHIPoBLE connection
    if (mManager != nullptr)
    {
        return mManager->HandleSideChannelConnection(connection, bonding);
    }
    return false;
}

BlePlatformInterface::WriteType BlePlatformEfr32::HandleChipoBleWrite(void * platformEvent, uint8_t connection, uint16_t characteristic)
{
    // EFR32-specific: Check for RX characteristic
    if (characteristic == gattdb_CHIPoBLEChar_Rx)
    {
        return BlePlatformInterface::WriteType::RX_CHARACTERISTIC;
    }
    return BlePlatformInterface::WriteType::OTHER_CHIPOBLE;
}

bool BlePlatformEfr32::HandleNonChipoBleWrite(void * platformEvent, uint8_t connection, uint16_t characteristic)
{
    // EFR32: Handle side channel write if it's not a CHIPoBLE write
    if (mManager != nullptr)
    {
        return mManager->HandleSideChannelWrite(platformEvent);
    }
    return false;
}

bool BlePlatformEfr32::HandleNonChipoBleRead(void * platformEvent, uint8_t connection, uint16_t characteristic)
{
    // EFR32: Handle side channel read if it's not a CHIPoBLE read
    if (mManager != nullptr)
    {
        return mManager->HandleSideChannelRead(platformEvent, connection, characteristic);
    }
    return false;
}

bool BlePlatformEfr32::HandleNonChipoBleMtuUpdate(void * platformEvent, uint8_t connection)
{
    // EFR32: Handle side channel MTU update if it's not a CHIPoBLE connection
    if (mManager != nullptr)
    {
        return mManager->HandleSideChannelMtuUpdate(platformEvent, connection);
    }
    return false;
}

CHIP_ERROR BlePlatformEfr32::MapDisconnectReason(uint16_t platformReason)
{
    // EFR32 reason codes
    switch (platformReason)
    {
    case SL_STATUS_BT_CTRL_REMOTE_USER_TERMINATED:
    case SL_STATUS_BT_CTRL_REMOTE_DEVICE_TERMINATED_CONNECTION_DUE_TO_LOW_RESOURCES:
    case SL_STATUS_BT_CTRL_REMOTE_POWERING_OFF:
        return BLE_ERROR_REMOTE_DEVICE_DISCONNECTED;

    case SL_STATUS_BT_CTRL_CONNECTION_TERMINATED_BY_LOCAL_HOST:
        return BLE_ERROR_APP_CLOSED_CONNECTION;

    default:
        return BLE_ERROR_CHIPOBLE_PROTOCOL_ABORT;
    }
}

bool BlePlatformEfr32::HandleNonChipoBleDisconnect(void * platformEvent, uint8_t connection)
{
    // EFR32: Handle side channel disconnect if it's not a CHIPoBLE connection
    if (mManager != nullptr)
    {
        return mManager->HandleSideChannelDisconnect(connection);
    }
    return false;
}

BlePlatformInterface::TxCccdWriteResult BlePlatformEfr32::HandleTxCccdWrite(void * platformEvent, const BleEvent & unifiedEvent)
{
    TxCccdWriteResult result = { false, false, 0 };

    // EFR32 platform: CCCD writes come as kGattCharacteristicStatus events
    if (unifiedEvent.type == BleEventType::kGattCharacteristicStatus)
    {
        const auto & statusData = unifiedEvent.data.characteristicStatus;

        // EFR32-specific: Check for TX characteristic
        // statusData.flags contains client_config_flags: 0x00=disabled, 0x01=notifications, 0x02=indications
        if (statusData.characteristic == gattdb_CHIPoBLEChar_Tx)
        {
            result.handled            = true;
            result.isIndicationEnabled = (statusData.flags == 0x02); // Check for indications (0x02)
            result.connection         = statusData.connection;
        }
    }

    return result;
}

bool BlePlatformEfr32::HandleNonChipoBleCccdWrite(void * platformEvent, const BleEvent & unifiedEvent)
{
    // EFR32: Handle side channel CCCD write if it's not a CHIPoBLE CCCD write
    if (unifiedEvent.type == BleEventType::kGattCharacteristicStatus)
    {
        const auto & statusData = unifiedEvent.data.characteristicStatus;

        // If it's a CHIPoBLE characteristic but not a CHIPoBLE connection, silent fail
        if (IsChipoBleCharacteristic(statusData.characteristic))
        {
            return false; // Silent fail indication if the characteristic is from CHIPoBLE and the connection on Side Channel
        }

        // Handle side channel CCCD write
        if (mManager != nullptr)
        {
            bool isNewSubscription = false;
            CHIP_ERROR err         = mManager->HandleSideChannelCccdWrite(platformEvent, isNewSubscription);
            return (err == CHIP_NO_ERROR);
        }
    }

    return false;
}
