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
 *          SiWx917 platform-specific implementation of BlePlatformInterface
 *          Merged from sl_si91x_ble_init.h
 */

#pragma once

#include "ble_config.h"
#include "cmsis_os2.h"
#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <platform/silabs/ble/BlePlatformInterface.h>
#include <stdbool.h>
#include <string.h>

extern "C" {
#include <rsi_ble.h>
#include <rsi_ble_apis.h>
#include <rsi_ble_common_config.h>
#include <rsi_bt_common.h>
#include <rsi_bt_common_apis.h>
#include <rsi_common_apis.h>
}

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

// Constants from sl_si91x_ble_init.h
#define ATT_REC_IN_HOST (0)
#define WFX_QUEUE_SIZE 10

#define RSI_BT_CTRL_REMOTE_USER_TERMINATED (0x4E13)
#define RSI_BT_CTRL_REMOTE_DEVICE_TERMINATED_CONNECTION_DUE_TO_LOW_RESOURCES (0x4E14)
#define RSI_BT_CTRL_REMOTE_POWERING_OFF (0x4E15)
#define RSI_BT_CTRL_TERMINATED_MIC_FAILURE (0x4E3D)
#define RSI_BT_FAILED_TO_ESTABLISH_CONN (0x4E3E)
#define RSI_BT_INVALID_RANGE (0x4E60)

#define RSI_BLE_MATTER_CUSTOM_SERVICE_UUID (0)
#define RSI_BLE_MATTER_CUSTOM_SERVICE_SIZE (2)
#define RSI_BLE_MATTER_CUSTOM_SERVICE_VALUE_16 (0xFFF6)
#define RSI_BLE_MATTER_CUSTOM_SERVICE_DATA (0x00)

#define RSI_BLE_CUSTOM_CHARACTERISTIC_RX_SIZE (16)
#define RSI_BLE_CUSTOM_CHARACTERISTIC_RX_RESERVED 0x00, 0x00, 0x00
#define RSI_BLE_CUSTOM_CHARACTERISTIC_RX_VALUE_128_DATA_1 0x18EE2EF5
#define RSI_BLE_CUSTOM_CHARACTERISTIC_RX_VALUE_128_DATA_2 0x263D
#define RSI_BLE_CUSTOM_CHARACTERISTIC_RX_VALUE_128_DATA_3 0x4559
#define RSI_BLE_CUSTOM_CHARACTERISTIC_RX_VALUE_128_DATA_4 0x9F, 0x95, 0x9C, 0x4F, 0x11, 0x9D, 0x9F, 0x42
#define RSI_BLE_CHARACTERISTIC_RX_ATTRIBUTE_HANDLE_LOCATION (1)
#define RSI_BLE_CHARACTERISTIC_RX_VALUE_HANDLE_LOCATION (2)

#define RSI_BLE_CUSTOM_CHARACTERISTIC_TX_SIZE (16)
#define RSI_BLE_CUSTOM_CHARACTERISTIC_TX_RESERVED 0x00, 0x00, 0x00
#define RSI_BLE_CUSTOM_CHARACTERISTIC_TX_VALUE_128_DATA_1 0x18EE2EF5
#define RSI_BLE_CUSTOM_CHARACTERISTIC_TX_VALUE_128_DATA_2 0x263D
#define RSI_BLE_CUSTOM_CHARACTERISTIC_TX_VALUE_128_DATA_3 0x4559
#define RSI_BLE_CUSTOM_CHARACTERISTIC_TX_VALUE_128_DATA_4 0x9F, 0x95, 0x9C, 0x4F, 0x12, 0x9D, 0x9F, 0x42
#define RSI_BLE_CHARACTERISTIC_TX_ATTRIBUTE_HANDLE_LOCATION (3)
#define RSI_BLE_CHARACTERISTIC_TX_MEASUREMENT_HANDLE_LOCATION (4)
#define RSI_BLE_CHARACTERISTIC_TX_GATT_SERVER_CLIENT_HANDLE_LOCATION (5)

#ifdef CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
#define RSI_BLE_CHAR_C3_UUID_SIZE (16)
#define RSI_BLE_CHAR_C3_RESERVED 0x00, 0x00, 0x00
#define RSI_BLE_CHAR_C3_UUID_1 0x64630238
#define RSI_BLE_CHAR_C3_UUID_2 0x8772
#define RSI_BLE_CHAR_C3_UUID_3 0x45f2
#define RSI_BLE_CHAR_C3_UUID_4 0x7D, 0xB8, 0x8A, 0x74, 0x04, 0x8F, 0x21, 0x83
#define RSI_BLE_CHAR_C3_ATTR_HANDLE_LOC (6)
#define RSI_BLE_CHAR_C3_MEASUREMENT_HANDLE_LOC (7)
#define RSI_BLE_CHAR_C3_GATT_SERVER_CLI_HANDLE_LOC (8)
#endif

// SilabsBleWrapper class (merged from sl_si91x_ble_init.h)
class SilabsBleWrapper
{
public:
    enum class BleEventType : uint8_t
    {
        RSI_BLE_CONN_EVENT,
        RSI_BLE_DISCONN_EVENT,
        RSI_BLE_GATT_WRITE_EVENT,
        RSI_BLE_MTU_EVENT,
        RSI_BLE_GATT_INDICATION_CONFIRMATION,
        RSI_BLE_RESP_ATT_VALUE,
        RSI_BLE_EVENT_GATT_RD
    };

    struct sl_wfx_msg_t
    {
        uint8_t connectionHandle;
        uint8_t bondingHandle;
        uint16_t reason;
        uint16_t event_id;
        uint16_t resp_status;
        rsi_ble_event_mtu_t rsi_ble_mtu;
        rsi_ble_event_write_t rsi_ble_write;
        rsi_ble_event_enhance_conn_status_t resp_enh_conn;
        rsi_ble_event_disconnect_t * resp_disconnect;
        rsi_ble_read_req_t * rsi_ble_read_req;
        rsi_ble_set_att_resp_t rsi_ble_event_set_att_rsp;
        uint16_t subscribed;
    };

    struct BleEvent_t
    {
        BleEventType eventType;
        sl_wfx_msg_t eventData;
    };

    // BLE callback functions
    static void rsi_ble_on_connect_event(rsi_ble_event_conn_status_t * resp_conn);
    static void rsi_ble_on_disconnect_event(rsi_ble_event_disconnect_t * resp_disconnect, uint16_t reason);
    static void rsi_ble_on_enhance_conn_status_event(rsi_ble_event_enhance_conn_status_t * resp_enh_conn);
    static void rsi_ble_on_gatt_write_event(uint16_t event_id, rsi_ble_event_write_t * rsi_ble_write);
    static void rsi_ble_on_mtu_event(rsi_ble_event_mtu_t * rsi_ble_mtu);
    static void rsi_ble_on_event_indication_confirmation(uint16_t resp_status, rsi_ble_set_att_resp_t * rsi_ble_event_set_att_rsp);
    static void rsi_ble_on_read_req_event(uint16_t event_id, rsi_ble_read_req_t * rsi_ble_read_req);
    static void rsi_gatt_add_attribute_to_list(rsi_ble_t * p_val, uint16_t handle, uint16_t data_len, uint8_t * data, uuid_t uuid,
                                               uint8_t char_prop);
    static void rsi_ble_add_char_serv_att(void * serv_handler, uint16_t handle, uint8_t val_prop, uint16_t att_val_handle,
                                          uuid_t att_val_uuid);
    static void rsi_ble_add_char_val_att(void * serv_handler, uint16_t handle, uuid_t att_type_uuid, uint8_t val_prop,
                                         uint8_t * data, uint8_t data_len, uint8_t auth_read);
};

/**
 * @brief SiWx917 platform implementation of BlePlatformInterface
 */
class BlePlatformSiWx917 : public BlePlatformInterface
{
public:
    static BlePlatformSiWx917 & GetInstance()
    {
        static BlePlatformSiWx917 sInstance;
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
    bool IsTxCccdHandle(uint16_t characteristic) const override;
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

    // SiWx-specific methods
    void BlePostEvent(SilabsBleWrapper::BleEvent_t * event);
    void ProcessEvent(SilabsBleWrapper::BleEvent_t inEvent);
    void sl_ble_init();
    static void sl_ble_event_handling_task(void * args);

    // Public for initialization by rsi_ble_add_matter_service
    uint16_t mRsiBleRxValueHndl                = 0;
    uint16_t mRsiBleMeasurementHndl            = 0;
    uint16_t mRsiBleGattServerClientConfigHndl = 0;

private:
    BlePlatformSiWx917()  = default;
    ~BlePlatformSiWx917() = default;

    BLEManagerImpl * mManager                = nullptr;
    uint8_t mAdvertisingSetHandle            = 0xff;
    static constexpr uint8_t kMaxConnections = 8;
    BleConnectionState mConnections[kMaxConnections];
    osMessageQueueId_t sBleEventQueue = nullptr;
    osThreadId_t sBleThread           = nullptr;
    uint8_t mDevAddress[RSI_DEV_ADDR_LEN];
};

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
