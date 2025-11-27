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
 *          Merged from sl_si91x_ble_init.cpp
 */

#include <ble/Ble.h>
#include <crypto/RandUtils.h>
#include <cstring>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/silabs/SiWx/ble/BlePlatformSiWx.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {
class BLEManagerImpl;
BLEManagerImpl & BLEMgrImpl();
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip

extern "C" {
#include <rsi_ble_apis.h>
#include <rsi_utils.h>
}

extern "C" void ChipBlePlatform_NotifyStackReady();
extern "C" void ChipBlePlatform_HandleEvent(void * platformEvent, int eventType);

using namespace ::chip;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceLayer::Internal;
using namespace ::chip::DeviceLayer::Internal::Silabs;

namespace {

#define BLE_MIN_CONNECTION_INTERVAL_MS 24
#define BLE_MAX_CONNECTION_INTERVAL_MS 40
#define BLE_SLAVE_LATENCY_MS 0
#define BLE_TIMEOUT_MS 400

// BLE thread configuration
constexpr uint32_t kBleTaskSize = 2560;
uint8_t bleStack[kBleTaskSize];
osThreadId_t sBleTaskControlBlock;
constexpr osThreadAttr_t kBleTaskAttr = { .name       = "rsi_ble",
                                          .attr_bits  = osThreadDetached,
                                          .cb_mem     = &sBleTaskControlBlock,
                                          .cb_size    = sizeof(osThreadId_t),
                                          .stack_mem  = bleStack,
                                          .stack_size = kBleTaskSize,
                                          .priority   = osPriorityHigh };

// Global instance pointer for callbacks
BlePlatformSiWx917 * gPlatformInstance = nullptr;

void rsi_ble_add_matter_service(void)
{
    constexpr uuid_t custom_service                         = { .size = RSI_BLE_MATTER_CUSTOM_SERVICE_SIZE,
                                                                .val  = { .val16 = RSI_BLE_MATTER_CUSTOM_SERVICE_VALUE_16 } };
    uint8_t data[RSI_BLE_MATTER_CUSTOM_SERVICE_DATA_LENGTH] = { RSI_BLE_MATTER_CUSTOM_SERVICE_DATA };

    constexpr uuid_t custom_characteristic_RX = { .size     = RSI_BLE_CUSTOM_CHARACTERISTIC_RX_SIZE,
                                                  .reserved = { RSI_BLE_CUSTOM_CHARACTERISTIC_RX_RESERVED },
                                                  .val      = { .val128 = {
                                                                    .data1 = { RSI_BLE_CUSTOM_CHARACTERISTIC_RX_VALUE_128_DATA_1 },
                                                                    .data2 = { RSI_BLE_CUSTOM_CHARACTERISTIC_RX_VALUE_128_DATA_2 },
                                                                    .data3 = { RSI_BLE_CUSTOM_CHARACTERISTIC_RX_VALUE_128_DATA_3 },
                                                                    .data4 = { RSI_BLE_CUSTOM_CHARACTERISTIC_RX_VALUE_128_DATA_4 } } } };

    rsi_ble_resp_add_serv_t new_serv_resp = { 0 };
    int32_t add_serv_rc                   = rsi_ble_add_service(custom_service, &new_serv_resp);
    if (add_serv_rc != RSI_SUCCESS)
    {
        ChipLogError(DeviceLayer, "rsi_ble_add_service failed: %ld", add_serv_rc);
        return;
    }
    else
    {
        ChipLogProgress(DeviceLayer, "rsi_ble_add_service succeeded, serv_handler=%p, start_handle=%u", new_serv_resp.serv_handler,
                        new_serv_resp.start_handle);
    }

    // Adding custom characteristic declaration to the custom service
    SilabsBleWrapper::rsi_ble_add_char_serv_att(
        new_serv_resp.serv_handler, new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_RX_ATTRIBUTE_HANDLE_LOCATION,
        RSI_BLE_ATT_PROPERTY_WRITE | RSI_BLE_ATT_PROPERTY_READ, // Set read, write, write without response
        new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_RX_VALUE_HANDLE_LOCATION, custom_characteristic_RX);

    // Adding characteristic value attribute to the service
    if (gPlatformInstance)
    {
        gPlatformInstance->mRsiBleRxValueHndl = new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_RX_VALUE_HANDLE_LOCATION;
    }
    SilabsBleWrapper::rsi_ble_add_char_val_att(
        new_serv_resp.serv_handler, new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_RX_VALUE_HANDLE_LOCATION,
        custom_characteristic_RX,
        RSI_BLE_ATT_PROPERTY_WRITE | RSI_BLE_ATT_PROPERTY_READ, // Set read, write, write without response
        data, sizeof(data), ATT_REC_IN_HOST);

    constexpr uuid_t custom_characteristic_TX = { .size     = RSI_BLE_CUSTOM_CHARACTERISTIC_TX_SIZE,
                                                  .reserved = { RSI_BLE_CUSTOM_CHARACTERISTIC_TX_RESERVED },
                                                  .val      = { .val128 = {
                                                                    .data1 = { RSI_BLE_CUSTOM_CHARACTERISTIC_TX_VALUE_128_DATA_1 },
                                                                    .data2 = { RSI_BLE_CUSTOM_CHARACTERISTIC_TX_VALUE_128_DATA_2 },
                                                                    .data3 = { RSI_BLE_CUSTOM_CHARACTERISTIC_TX_VALUE_128_DATA_3 },
                                                                    .data4 = { RSI_BLE_CUSTOM_CHARACTERISTIC_TX_VALUE_128_DATA_4 } } } };

    // Adding custom characteristic declaration to the custom service
    SilabsBleWrapper::rsi_ble_add_char_serv_att(
        new_serv_resp.serv_handler, new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_TX_ATTRIBUTE_HANDLE_LOCATION,
        RSI_BLE_ATT_PROPERTY_WRITE_NO_RESPONSE | RSI_BLE_ATT_PROPERTY_WRITE | RSI_BLE_ATT_PROPERTY_READ |
            RSI_BLE_ATT_PROPERTY_NOTIFY | RSI_BLE_ATT_PROPERTY_INDICATE, // Set read, write, write without response
        new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_TX_MEASUREMENT_HANDLE_LOCATION, custom_characteristic_TX);

    // Adding characteristic value attribute to the service
    if (gPlatformInstance)
    {
        gPlatformInstance->mRsiBleMeasurementHndl =
            new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_TX_MEASUREMENT_HANDLE_LOCATION;

        // Adding characteristic value attribute to the service
        gPlatformInstance->mRsiBleGattServerClientConfigHndl =
            new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_TX_GATT_SERVER_CLIENT_HANDLE_LOCATION;
    }

    SilabsBleWrapper::rsi_ble_add_char_val_att(
        new_serv_resp.serv_handler, new_serv_resp.start_handle + RSI_BLE_CHARACTERISTIC_TX_MEASUREMENT_HANDLE_LOCATION,
        custom_characteristic_TX,
        RSI_BLE_ATT_PROPERTY_WRITE_NO_RESPONSE | RSI_BLE_ATT_PROPERTY_WRITE | RSI_BLE_ATT_PROPERTY_READ |
            RSI_BLE_ATT_PROPERTY_NOTIFY | RSI_BLE_ATT_PROPERTY_INDICATE, // Set read, write, write without response
        data, sizeof(data), ATT_REC_MAINTAIN_IN_HOST);

#ifdef CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    // C3 characteristic is of 128 bit UUID format structure - where val128 is of uuid128_t which is composed of uint32_t data1,
    // uint16_t data2, uint16_t data3, uint8_t data4[8];
    constexpr uuid_t custom_characteristic_C3 = { .size     = RSI_BLE_CHAR_C3_UUID_SIZE,
                                                  .reserved = { RSI_BLE_CHAR_C3_RESERVED },
                                                  .val      = { .val128 = { .data1 = RSI_BLE_CHAR_C3_UUID_1,
                                                                            .data2 = RSI_BLE_CHAR_C3_UUID_2,
                                                                            .data3 = RSI_BLE_CHAR_C3_UUID_3,
                                                                            .data4 = { RSI_BLE_CHAR_C3_UUID_4 } } } };

    // Adding custom characteristic declaration to the custom service
    SilabsBleWrapper::rsi_ble_add_char_serv_att(
        new_serv_resp.serv_handler, new_serv_resp.start_handle + RSI_BLE_CHAR_C3_ATTR_HANDLE_LOC,
        RSI_BLE_ATT_PROPERTY_READ, // Set read
        new_serv_resp.start_handle + RSI_BLE_CHAR_C3_MEASUREMENT_HANDLE_LOC, custom_characteristic_C3);

    // Adding characteristic value attribute to the service
    SilabsBleWrapper::rsi_ble_add_char_val_att(
        new_serv_resp.serv_handler, new_serv_resp.start_handle + RSI_BLE_CHAR_C3_MEASUREMENT_HANDLE_LOC, custom_characteristic_C3,
        RSI_BLE_ATT_PROPERTY_READ, // Set read
        data, sizeof(data), ATT_REC_MAINTAIN_IN_HOST);
#endif // CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
}

} // namespace

// SilabsBleWrapper static method implementations
void SilabsBleWrapper::rsi_ble_on_mtu_event(rsi_ble_event_mtu_t * rsi_ble_mtu)
{
    if (gPlatformInstance)
    {
        SilabsBleWrapper::BleEvent_t bleEvent = { .eventType = BleEventType::RSI_BLE_MTU_EVENT,
                                                  .eventData = { .connectionHandle = 1, .rsi_ble_mtu = *rsi_ble_mtu } };
        gPlatformInstance->BlePostEvent(&bleEvent);
    }
}

void SilabsBleWrapper::rsi_ble_on_gatt_write_event(uint16_t event_id, rsi_ble_event_write_t * rsi_ble_write)
{
    if (gPlatformInstance)
    {
        // Hex dump of write data
        char hexBuf[256];
        int pos   = 0;
        hexBuf[0] = '\0';
        for (uint8_t i = 0; i < rsi_ble_write->length && pos + 4 < (int) sizeof(hexBuf); ++i)
        {
            int n = snprintf(&hexBuf[pos], sizeof(hexBuf) - pos, "%02X", rsi_ble_write->att_value[i]);
            if (n > 0)
                pos += n;
            if (i + 1 < rsi_ble_write->length && pos + 1 < (int) sizeof(hexBuf))
            {
                hexBuf[pos++] = ' ';
                hexBuf[pos]   = '\0';
            }
        }

        SilabsBleWrapper::BleEvent_t bleEvent = {
            .eventType = BleEventType::RSI_BLE_GATT_WRITE_EVENT,
            .eventData = { .connectionHandle = 1, .event_id = event_id, .rsi_ble_write = *rsi_ble_write }
        };
        gPlatformInstance->BlePostEvent(&bleEvent);
    }
}

void SilabsBleWrapper::rsi_ble_on_enhance_conn_status_event(rsi_ble_event_enhance_conn_status_t * resp_enh_conn)
{
    if (gPlatformInstance)
    {
        SilabsBleWrapper::BleEvent_t bleEvent = { .eventType = BleEventType::RSI_BLE_CONN_EVENT,
                                                  .eventData = {
                                                      .connectionHandle = 1,
                                                      .bondingHandle    = 255,
                                                  } };
        memcpy(bleEvent.eventData.resp_enh_conn.dev_addr, resp_enh_conn->dev_addr, RSI_DEV_ADDR_LEN);
        gPlatformInstance->BlePostEvent(&bleEvent);
    }
}

void SilabsBleWrapper::rsi_ble_on_disconnect_event(rsi_ble_event_disconnect_t * resp_disconnect, uint16_t reason)
{
    if (gPlatformInstance)
    {
        SilabsBleWrapper::BleEvent_t bleEvent = { .eventType = BleEventType::RSI_BLE_DISCONN_EVENT,
                                                  .eventData = { .reason = reason } };
        gPlatformInstance->BlePostEvent(&bleEvent);
    }
}

void SilabsBleWrapper::rsi_ble_on_event_indication_confirmation(uint16_t resp_status,
                                                                rsi_ble_set_att_resp_t * rsi_ble_event_set_att_rsp)
{
    if (gPlatformInstance)
    {
        SilabsBleWrapper::BleEvent_t bleEvent = { .eventType = BleEventType::RSI_BLE_GATT_INDICATION_CONFIRMATION,
                                                  .eventData = { .resp_status               = resp_status,
                                                                 .rsi_ble_event_set_att_rsp = *rsi_ble_event_set_att_rsp } };
        gPlatformInstance->BlePostEvent(&bleEvent);
    }
}

void SilabsBleWrapper::rsi_ble_on_read_req_event(uint16_t event_id, rsi_ble_read_req_t * rsi_ble_read_req)
{
    if (gPlatformInstance)
    {
        SilabsBleWrapper::BleEvent_t bleEvent = { .eventType = BleEventType::RSI_BLE_EVENT_GATT_RD,
                                                  .eventData = { .event_id = event_id, .rsi_ble_read_req = rsi_ble_read_req } };
        gPlatformInstance->BlePostEvent(&bleEvent);
    }
}

void SilabsBleWrapper::rsi_gatt_add_attribute_to_list(rsi_ble_t * p_val, uint16_t handle, uint16_t data_len, uint8_t * data,
                                                      uuid_t uuid, uint8_t char_prop)
{
    if ((p_val->DATA_ix + data_len) >= BLE_ATT_REC_SIZE)
    { //! Check for max data length for the characteristic value
        return;
    }

    p_val->att_rec_list[p_val->att_rec_list_count].char_uuid     = uuid;
    p_val->att_rec_list[p_val->att_rec_list_count].handle        = handle;
    p_val->att_rec_list[p_val->att_rec_list_count].value_len     = data_len;
    p_val->att_rec_list[p_val->att_rec_list_count].max_value_len = data_len;
    p_val->att_rec_list[p_val->att_rec_list_count].char_val_prop = char_prop;
    memcpy(p_val->DATA + p_val->DATA_ix, data, data_len);
    p_val->att_rec_list[p_val->att_rec_list_count].value = p_val->DATA + p_val->DATA_ix;
    p_val->att_rec_list_count++;
    p_val->DATA_ix += p_val->att_rec_list[p_val->att_rec_list_count].max_value_len;

    return;
}

void SilabsBleWrapper::rsi_ble_add_char_serv_att(void * serv_handler, uint16_t handle, uint8_t val_prop, uint16_t att_val_handle,
                                                 uuid_t att_val_uuid)
{
    rsi_ble_req_add_att_t new_att = { 0 };

    //! preparing the attribute service structure
    new_att.serv_handler       = serv_handler;
    new_att.handle             = handle;
    new_att.att_uuid.size      = 2;
    new_att.att_uuid.val.val16 = RSI_BLE_CHAR_SERV_UUID;
    new_att.property           = RSI_BLE_ATT_PROPERTY_READ;

    //! preparing the characteristic attribute value
    new_att.data_len = att_val_uuid.size + 4;
    new_att.data[0]  = val_prop;
    rsi_uint16_to_2bytes(&new_att.data[2], att_val_handle);
    if (new_att.data_len == 6)
    {
        rsi_uint16_to_2bytes(&new_att.data[4], att_val_uuid.val.val16);
    }
    else if (new_att.data_len == 8)
    {
        rsi_uint32_to_4bytes(&new_att.data[4], att_val_uuid.val.val32);
    }
    else if (new_att.data_len == 20)
    {
        memcpy(&new_att.data[4], &att_val_uuid.val.val128, att_val_uuid.size);
    }
    //! Add attribute to the service
    rsi_ble_add_attribute(&new_att);

    return;
}

void SilabsBleWrapper::rsi_ble_add_char_val_att(void * serv_handler, uint16_t handle, uuid_t att_type_uuid, uint8_t val_prop,
                                                uint8_t * data, uint8_t data_len, uint8_t auth_read)
{
    rsi_ble_req_add_att_t new_att = { 0 };
    rsi_ble_t att_list;
    memset(&new_att, 0, sizeof(rsi_ble_req_add_att_t));
    //! preparing the attributes
    new_att.serv_handler  = serv_handler;
    new_att.handle        = handle;
    new_att.config_bitmap = auth_read;
    memcpy(&new_att.att_uuid, &att_type_uuid, sizeof(uuid_t));
    new_att.property = val_prop;

    if (data != NULL)
    {
        memcpy(new_att.data, data, RSI_MIN(sizeof(new_att.data), data_len));
    }

    //! preparing the attribute value
    new_att.data_len = data_len;

    //! add attribute to the service
    rsi_ble_add_attribute(&new_att);

    if ((auth_read == ATT_REC_MAINTAIN_IN_HOST) || (data_len > 20))
    {
        if (data != NULL)
        {
            rsi_gatt_add_attribute_to_list(&att_list, handle, data_len, data, att_type_uuid, val_prop);
        }
    }

    //! check the attribute property with notification/Indication
    if ((val_prop & RSI_BLE_ATT_PROPERTY_NOTIFY) || (val_prop & RSI_BLE_ATT_PROPERTY_INDICATE))
    {
        //! if notification/indication property supports then we need to add client characteristic service.

        //! preparing the client characteristic attribute & values
        memset(&new_att, 0, sizeof(rsi_ble_req_add_att_t));
        new_att.serv_handler       = serv_handler;
        new_att.handle             = handle + 1;
        new_att.att_uuid.size      = 2;
        new_att.att_uuid.val.val16 = RSI_BLE_CLIENT_CHAR_UUID;
        new_att.property           = RSI_BLE_ATT_PROPERTY_READ | RSI_BLE_ATT_PROPERTY_WRITE;
        new_att.data_len           = 2;

        //! add attribute to the service
        rsi_ble_add_attribute(&new_att);
    }

    return;
}

// BlePlatformSiWx917 implementation
CHIP_ERROR BlePlatformSiWx917::Init()
{
    gPlatformInstance = this;
    memset(mConnections, 0, sizeof(mConnections));
    memset(mDevAddress, 0, sizeof(mDevAddress));
    mRsiBleMeasurementHndl            = 0;
    mRsiBleGattServerClientConfigHndl = 0;
    mAdvertisingSetHandle             = 0xff;

    // Create BLE event queue
    sBleEventQueue = osMessageQueueNew(WFX_QUEUE_SIZE, sizeof(SilabsBleWrapper::BleEvent_t), NULL);
    VerifyOrReturnError(sBleEventQueue != nullptr, CHIP_ERROR_NO_MEMORY);

    return CHIP_NO_ERROR;
}

void BlePlatformSiWx917::SetManager(BLEManagerImpl * manager)
{
    mManager = manager;
}

CHIP_ERROR BlePlatformSiWx917::ConfigureAdvertising(const BleAdvertisingConfig & config)
{
    int32_t result = 0;

    if (config.advData.size() > 0)
    {
        char advHexBuf[128];
        int advPos   = 0;
        advHexBuf[0] = '\0';
        for (size_t i = 0; i < config.advData.size() && advPos + 4 < (int) sizeof(advHexBuf); ++i)
        {
            int n = snprintf(&advHexBuf[advPos], sizeof(advHexBuf) - advPos, "%02X", config.advData[i]);
            if (n > 0)
                advPos += n;
            if (i + 1 < config.advData.size() && advPos + 1 < (int) sizeof(advHexBuf))
            {
                advHexBuf[advPos++] = ' ';
                advHexBuf[advPos]   = '\0';
            }
        }

        result = rsi_ble_set_advertise_data(const_cast<uint8_t *>(config.advData.data()), config.advData.size());
        if (result != RSI_SUCCESS)
        {
            return MapPlatformError(result);
        }
    }

    if (config.responseData.size() > 0)
    {
        char rspHexBuf[128];
        int rspPos   = 0;
        rspHexBuf[0] = '\0';
        for (size_t i = 0; i < config.responseData.size() && rspPos + 4 < (int) sizeof(rspHexBuf); ++i)
        {
            int n = snprintf(&rspHexBuf[rspPos], sizeof(rspHexBuf) - rspPos, "%02X", config.responseData[i]);
            if (n > 0)
                rspPos += n;
            if (i + 1 < config.responseData.size() && rspPos + 1 < (int) sizeof(rspHexBuf))
            {
                rspHexBuf[rspPos++] = ' ';
                rspHexBuf[rspPos]   = '\0';
            }
        }

        result = rsi_ble_set_scan_response_data(const_cast<uint8_t *>(config.responseData.data()), config.responseData.size());
        if (result != RSI_SUCCESS)
        {
            return MapPlatformError(result);
        }
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformSiWx917::StartAdvertising(uint32_t intervalMin, uint32_t intervalMax, bool connectable)
{
    rsi_ble_req_adv_t ble_adv = { 0 };

    ble_adv.status           = RSI_BLE_START_ADV;
    ble_adv.adv_type         = connectable ? UNDIR_CONN : UNDIR_NON_CONN;
    ble_adv.filter_type      = RSI_BLE_ADV_FILTER_TYPE;
    ble_adv.direct_addr_type = RSI_BLE_ADV_DIR_ADDR_TYPE;
    rsi_ascii_dev_address_to_6bytes_rev(ble_adv.direct_addr, (int8_t *) RSI_BLE_ADV_DIR_ADDR);
    ble_adv.adv_int_min     = intervalMin;
    ble_adv.adv_int_max     = intervalMax;
    ble_adv.own_addr_type   = LE_RANDOM_ADDRESS;
    ble_adv.adv_channel_map = RSI_BLE_ADV_CHANNEL_MAP;

    int32_t result = rsi_ble_start_advertising_with_values(&ble_adv);
    if (result != RSI_SUCCESS)
    {
        ChipLogError(DeviceLayer, "rsi_ble_start_advertising_with_values() failed: %ld", result);
        return MapPlatformError(result);
    }

    // SiWx does not expose an advertising handle; use 0 as the implicit handle
    // so the higher-level BLEManager can correlate connections to CHIPoBLE advertising.
    mAdvertisingSetHandle = 0;

    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformSiWx917::StopAdvertising()
{
    int32_t result = rsi_ble_stop_advertising();
    if (result != RSI_SUCCESS)
    {
        return MapPlatformError(result);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformSiWx917::SendIndication(uint8_t connection, uint16_t characteristic, ByteSpan data)
{
    int32_t status = rsi_ble_indicate_value(mDevAddress, mRsiBleMeasurementHndl, data.size(), const_cast<uint8_t *>(data.data()));
    if (status != RSI_SUCCESS)
    {
        ChipLogError(DeviceLayer, "indication failed with error code %lx ", status);
        return MapPlatformError(status);
    }
    return CHIP_NO_ERROR;
}

uint16_t BlePlatformSiWx917::GetMTU(uint8_t connection) const
{
    BleConnectionState * conState = const_cast<BlePlatformSiWx917 *>(this)->GetConnectionState(connection, false);
    return (conState != nullptr) ? conState->mtu : 0;
}

CHIP_ERROR BlePlatformSiWx917::CloseConnection(uint8_t connection)
{
    // SiWx doesn't have a direct close connection API, connection is managed by the stack
    return CHIP_NO_ERROR;
}

bool BlePlatformSiWx917::ParseEvent(void * platformEvent, BleEvent & unifiedEvent)
{
    SilabsBleWrapper::BleEvent_t * siwxEvent = static_cast<SilabsBleWrapper::BleEvent_t *>(platformEvent);
    if (siwxEvent == nullptr)
    {
        return false;
    }

    switch (siwxEvent->eventType)
    {
    case SilabsBleWrapper::BleEventType::RSI_BLE_CONN_EVENT: {
        unifiedEvent.type                             = BleEventType::kConnectionOpened;
        unifiedEvent.data.connectionOpened.connection = siwxEvent->eventData.connectionHandle;
        unifiedEvent.data.connectionOpened.bonding    = siwxEvent->eventData.bondingHandle;
        unifiedEvent.data.connectionOpened.advertiser = 0; // SiWx doesn't use advertiser handle
        memcpy(unifiedEvent.data.connectionOpened.address, siwxEvent->eventData.resp_enh_conn.dev_addr, 6);
        return true;
    }

    case SilabsBleWrapper::BleEventType::RSI_BLE_DISCONN_EVENT: {
        unifiedEvent.type                             = BleEventType::kConnectionClosed;
        unifiedEvent.data.connectionClosed.connection = 1; // SiWx uses connection handle 1
        unifiedEvent.data.connectionClosed.reason     = siwxEvent->eventData.reason;
        return true;
    }

    case SilabsBleWrapper::BleEventType::RSI_BLE_GATT_WRITE_EVENT: {
        unifiedEvent.type                                 = BleEventType::kGattWriteRequest;
        unifiedEvent.data.gattWriteRequest.connection     = 1; // SiWx uses connection handle 1
        unifiedEvent.data.gattWriteRequest.characteristic = siwxEvent->eventData.rsi_ble_write.handle[0];
        unifiedEvent.data.gattWriteRequest.length         = siwxEvent->eventData.rsi_ble_write.length;
        unifiedEvent.data.gattWriteRequest.data           = siwxEvent->eventData.rsi_ble_write.att_value;
        return true;
    }

    case SilabsBleWrapper::BleEventType::RSI_BLE_MTU_EVENT: {
        unifiedEvent.type                         = BleEventType::kGattMtuExchanged;
        unifiedEvent.data.mtuExchanged.connection = 1; // SiWx uses connection handle 1
        unifiedEvent.data.mtuExchanged.mtu        = siwxEvent->eventData.rsi_ble_mtu.mtu_size;
        return true;
    }

    case SilabsBleWrapper::BleEventType::RSI_BLE_GATT_INDICATION_CONFIRMATION: {
        unifiedEvent.type                                   = BleEventType::kGattIndicationConfirmation;
        unifiedEvent.data.indicationConfirmation.connection = 1; // SiWx uses connection handle 1
        unifiedEvent.data.indicationConfirmation.status     = siwxEvent->eventData.resp_status;
        return true;
    }

    case SilabsBleWrapper::BleEventType::RSI_BLE_EVENT_GATT_RD: {
        if (siwxEvent->eventData.rsi_ble_read_req != nullptr)
        {
            unifiedEvent.type                                = BleEventType::kGattReadRequest;
            unifiedEvent.data.gattReadRequest.connection     = 1; // SiWx uses connection handle 1
            unifiedEvent.data.gattReadRequest.characteristic = siwxEvent->eventData.rsi_ble_read_req->handle;
            unifiedEvent.data.gattReadRequest.offset         = siwxEvent->eventData.rsi_ble_read_req->offset;
            return true;
        }
        return false;
    }

    default:
        return false;
    }
}

CHIP_ERROR BlePlatformSiWx917::MapPlatformError(int32_t platformError)
{
    switch (platformError)
    {
    case SL_STATUS_BT_ATT_INVALID_ATT_LENGTH:
        return CHIP_ERROR_INVALID_STRING_LENGTH;
    case SL_STATUS_INVALID_PARAMETER:
        return CHIP_ERROR_INVALID_ARGUMENT;
    case SL_STATUS_INVALID_STATE:
        return CHIP_ERROR_INCORRECT_STATE;
    case SL_STATUS_NOT_SUPPORTED:
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    default:
        return CHIP_ERROR(ChipError::Range::kPlatform, static_cast<uint32_t>(platformError));
    }
}

bool BlePlatformSiWx917::CanHandleEvent(uint32_t event)
{
    // SiWx uses a different event system, all events are handled via the queue
    return true;
}

bool BlePlatformSiWx917::IsChipoBleCharacteristic(uint16_t characteristic) const
{
    // Check if characteristic matches RX value, TX value, or TX CCCD handles
    return (characteristic == mRsiBleRxValueHndl || characteristic == mRsiBleMeasurementHndl ||
            characteristic == mRsiBleGattServerClientConfigHndl);
}

bool BlePlatformSiWx917::IsTxCccdHandle(uint16_t characteristic) const
{
    // Check if characteristic is specifically the TX CCCD handle
    return (characteristic == mRsiBleGattServerClientConfigHndl);
}

bool BlePlatformSiWx917::IsChipoBleConnection(uint8_t connection, uint8_t advertiser, uint8_t chipoBleAdvertiser) const
{
    // SiWx only supports one connection, so if we have a connection state, it's CHIPoBLE
    BleConnectionState * connState = const_cast<BlePlatformSiWx917 *>(this)->GetConnectionState(connection, false);
    // If the platform has a connection record, it's a CHIPoBLE connection.
    if (connState != nullptr)
    {
        return true;
    }

    // If the advertiser handle matches the CHIPoBLE advertiser handle, treat as CHIPoBLE.
    if (advertiser == chipoBleAdvertiser)
    {
        return true;
    }

    // If the BLEManager hasn't received an advertising handle (0xff), SiWx uses a single implicit
    // advertiser. Treat incoming connections as CHIPoBLE when the manager's advertiser handle is 0xff
    // and the platform reports advertiser==0 (the implicit advertiser).
    if (chipoBleAdvertiser == 0xff && advertiser == 0)
    {
        return true;
    }

    return false;
}

BleConnectionState * BlePlatformSiWx917::GetConnectionState(uint8_t connection, bool allocate)
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

void BlePlatformSiWx917::AddConnection(uint8_t connection, uint8_t bonding, const uint8_t * address)
{
    BleConnectionState * conState = GetConnectionState(connection, true);
    if (conState != nullptr)
    {
        conState->bondingHandle = bonding;
        if (address != nullptr)
        {
            memcpy(conState->address, address, 6);
            memcpy(mDevAddress, address, RSI_DEV_ADDR_LEN);
        }
    }
}

void BlePlatformSiWx917::RemoveConnection(uint8_t connection)
{
    BleConnectionState * conState = GetConnectionState(connection, false);
    if (conState != nullptr)
    {
        memset(conState, 0, sizeof(BleConnectionState));
    }
}

CHIP_ERROR BlePlatformSiWx917::SendReadResponse(uint8_t connection, uint16_t characteristic, ByteSpan data)
{
    // This will be handled by the BLEManagerImpl when processing read events
    return CHIP_NO_ERROR;
}

CHIP_ERROR BlePlatformSiWx917::SendWriteResponse(uint8_t connection, uint16_t characteristic, uint8_t status)
{
    // SiWx handles write responses automatically
    return CHIP_NO_ERROR;
}

bool BlePlatformSiWx917::HandleNonChipoBleConnection(uint8_t connection, uint8_t advertiser, uint8_t bonding,
                                                     const uint8_t * address, uint8_t chipoBleAdvertiser)
{
    // SiWx: Log why the connection was not identified as CHIPoBLE for diagnostics
    ChipLogProgress(DeviceLayer, "Connect Event on handle %d was not CHIPoBLE (advertiser=%u, advHandle=%u)", connection,
                    advertiser, chipoBleAdvertiser);
    return false;
}

BlePlatformInterface::WriteType BlePlatformSiWx917::HandleChipoBleWrite(void * platformEvent, uint8_t connection,
                                                                        uint16_t characteristic)
{
    // SiWx: Check if it's a TX CCCD write first
    if (IsTxCccdHandle(characteristic))
    {
        return BlePlatformInterface::WriteType::TX_CCCD;
    }
    // Check if it's a CHIPoBLE characteristic (RX or TX)
    else if (IsChipoBleCharacteristic(characteristic))
    {
        return BlePlatformInterface::WriteType::RX_CHARACTERISTIC;
    }
    return BlePlatformInterface::WriteType::OTHER_CHIPOBLE;
}

bool BlePlatformSiWx917::HandleNonChipoBleWrite(void * platformEvent, uint8_t connection, uint16_t characteristic)
{
    // SiWx doesn't support side channel
    return false;
}

bool BlePlatformSiWx917::HandleNonChipoBleRead(void * platformEvent, uint8_t connection, uint16_t characteristic)
{
    // SiWx doesn't support side channel
    return false;
}

bool BlePlatformSiWx917::HandleNonChipoBleMtuUpdate(void * platformEvent, uint8_t connection)
{
    // SiWx doesn't support side channel
    return false;
}

CHIP_ERROR BlePlatformSiWx917::MapDisconnectReason(uint16_t platformReason)
{
    // SiWx reason codes
    switch (platformReason)
    {
    case RSI_BT_CTRL_REMOTE_USER_TERMINATED:
    case RSI_BT_CTRL_REMOTE_DEVICE_TERMINATED_CONNECTION_DUE_TO_LOW_RESOURCES:
    case RSI_BT_CTRL_REMOTE_POWERING_OFF:
        return BLE_ERROR_REMOTE_DEVICE_DISCONNECTED;

    default:
        return BLE_ERROR_CHIPOBLE_PROTOCOL_ABORT;
    }
}

bool BlePlatformSiWx917::HandleNonChipoBleDisconnect(void * platformEvent, uint8_t connection)
{
    // SiWx doesn't support side channel
    return false;
}

BlePlatformInterface::TxCccdWriteResult BlePlatformSiWx917::HandleTxCccdWrite(void * platformEvent, const BleEvent & unifiedEvent)
{
    TxCccdWriteResult result = { false, false, 0 };

    // SiWx platform: CCCD writes come as kGattWriteRequest events
    if (unifiedEvent.type == BleEventType::kGattWriteRequest)
    {
        const auto & writeData = unifiedEvent.data.gattWriteRequest;

        if (IsTxCccdHandle(writeData.characteristic))
        {
            result.handled = true;
            // CCCD value is 2 bytes: 0x0001 = notifications, 0x0002 = indications, 0x0000 = disabled
            result.isIndicationEnabled = (writeData.length >= 2 && (writeData.data[0] != 0 || writeData.data[1] != 0));
            result.connection          = writeData.connection;
        }
    }

    return result;
}

bool BlePlatformSiWx917::HandleNonChipoBleCccdWrite(void * platformEvent, const BleEvent & unifiedEvent)
{
    // SiWx doesn't support side channel
    return false;
}

void BlePlatformSiWx917::BlePostEvent(SilabsBleWrapper::BleEvent_t * event)
{
    if (sBleEventQueue != nullptr)
    {
        sl_status_t status = osMessageQueuePut(sBleEventQueue, event, 0, 0);
        if (status != osOK)
        {
            ChipLogError(DeviceLayer, "BlePostEvent: failed to post event: 0x%lx", status);
        }
    }
}

void BlePlatformSiWx917::ProcessEvent(SilabsBleWrapper::BleEvent_t inEvent)
{
    if (mManager == nullptr)
    {
        return;
    }

    BleEvent unifiedEvent;
    if (ParseEvent(&inEvent, unifiedEvent))
    {
        // Process the unified event - forward to BLEManagerImpl via C wrapper
        ChipBlePlatform_HandleEvent(&inEvent, static_cast<int>(inEvent.eventType));
    }
}

void BlePlatformSiWx917::sl_ble_init()
{
    uint8_t randomAddrBLE[RSI_BLE_ADDR_LENGTH] = { 0 };
    uint64_t randomAddr                        = chip::Crypto::GetRandU64();
    memcpy(randomAddrBLE, &randomAddr, RSI_BLE_ADDR_LENGTH);
    // Set the two least significant bits as the first 2 bits of the address has to be '11' to ensure the address is a random
    // non-resolvable private address
    randomAddrBLE[(RSI_BLE_ADDR_LENGTH - 1)] |= 0xC0;

    // registering the GAP callback functions
    rsi_ble_gap_register_callbacks(NULL, NULL, SilabsBleWrapper::rsi_ble_on_disconnect_event, NULL, NULL, NULL,
                                   SilabsBleWrapper::rsi_ble_on_enhance_conn_status_event, NULL, NULL, NULL);

    // registering the GATT call back functions
    rsi_ble_gatt_register_callbacks(NULL, NULL, NULL, NULL, NULL, NULL, NULL, SilabsBleWrapper::rsi_ble_on_gatt_write_event, NULL,
                                    NULL, SilabsBleWrapper::rsi_ble_on_read_req_event, SilabsBleWrapper::rsi_ble_on_mtu_event, NULL,
                                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                    SilabsBleWrapper::rsi_ble_on_event_indication_confirmation, NULL);

    //  Exchange of GATT info with BLE stack
    rsi_ble_add_matter_service();
    rsi_ble_set_random_address_with_value(randomAddrBLE);
    // Notify BLE manager that the SiWx BLE stack has booted and is ready.
    ChipBlePlatform_NotifyStackReady();
}

void BlePlatformSiWx917::sl_ble_event_handling_task(void * args)
{
    BlePlatformSiWx917 * platform = static_cast<BlePlatformSiWx917 *>(args);
    if (platform == nullptr)
    {
        return;
    }

    // This function initialize BLE and start BLE advertisement.
    platform->sl_ble_init();

    // Application event map
    sl_status_t status;
    SilabsBleWrapper::BleEvent_t bleEvent;

    while (1)
    {
        status = osMessageQueueGet(platform->sBleEventQueue, &bleEvent, NULL, osWaitForever);
        if (status == osOK)
        {
            platform->ProcessEvent(bleEvent);
        }
        else
        {
            ChipLogError(DeviceLayer, "sl_ble_event_handling_task: get event failed: 0x%lx", static_cast<uint32_t>(status));
        }
    }
}
