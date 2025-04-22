#include "BLEChannel.h"
#include <crypto/RandUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

namespace {

// Side Channel UUIDS
const uint8_t kSideServiceUUID[16] = { 0x01, 0x00, 0x00, 0xEE, 0xFF, 0xC0, 0x00, 0x80,
                                       0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uuid_128 kRxUUID = { .data = { 0x01, 0x00, 0x00, 0xEE, 0xFF, 0xC0, 0xAD, 0xDE, 0xEF, 0xBE, 0xAD, 0xDE, 0x00, 0xEE, 0xFF,
                                     0xC0 } };
const uuid_128 kTxUUID = { .data = { 0x02, 0x00, 0x00, 0xEE, 0xFF, 0xC0, 0xAD, 0xDE, 0xEF, 0xBE, 0xAD, 0xDE, 0x00, 0xEE, 0xFF,
                                     0xC0 } };

uint8_t InitialValueRX[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
uint8_t InitialValueTX[16] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

CHIP_ERROR MapBLEError(int bleErr)
{
    switch (bleErr)
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
        return CHIP_ERROR(ChipError::Range::kPlatform, bleErr + CHIP_DEVICE_CONFIG_SILABS_BLE_ERROR_MIN);
    }
}

} // namespace

BLEChannel::BLEChannel()
{
    // Initialize the connection state
    memset(&mConnectionState, 0, sizeof(mConnectionState));
    mFlags.ClearAll();
    mAdvIntervalMin = 0;
    mAdvIntervalMax = 0;
    mAdvDuration    = 0;
    mAdvMaxEvents   = 0;
}

CHIP_ERROR BLEChannel::Init()
{
    uint16_t session;
    sl_status_t ret = sl_bt_gattdb_new_session(&session);
    VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));

    // Add service
    ret = sl_bt_gattdb_add_service(session, sl_bt_gattdb_primary_service,
                                   0, // not advertised
                                   sizeof(kSideServiceUUID), kSideServiceUUID, &mSideServiceHandle);
    VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));

    // Add RX characteristic
    ret = sl_bt_gattdb_add_uuid128_characteristic(session, mSideServiceHandle,
                                                  SL_BT_GATTDB_CHARACTERISTIC_READ | SL_BT_GATTDB_CHARACTERISTIC_WRITE,
                                                  0, // No security
                                                  0, // No flags
                                                  kRxUUID, sl_bt_gattdb_variable_length_value,
                                                  255, // Max length
                                                  sizeof(InitialValueRX), InitialValueRX, &mSideRxCharHandle);
    VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));

    ret = sl_bt_gattdb_add_uuid128_characteristic(session, mSideServiceHandle,
                                                  SL_BT_GATTDB_CHARACTERISTIC_READ | SL_BT_GATTDB_CHARACTERISTIC_WRITE |
                                                      SL_BT_GATTDB_CHARACTERISTIC_WRITE_NO_RESPONSE |
                                                      SL_BT_GATTDB_CHARACTERISTIC_INDICATE,
                                                  0, // No security
                                                  0, // No flags
                                                  kTxUUID, sl_bt_gattdb_variable_length_value,
                                                  255, // Max length
                                                  sizeof(InitialValueTX), InitialValueTX, &mSideTxCharHandle);
    VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));

    ret = sl_bt_gattdb_start_service(session, mSideServiceHandle);
    VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));

    ret = sl_bt_gattdb_start_characteristic(session, mSideRxCharHandle);
    VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));

    ret = sl_bt_gattdb_start_characteristic(session, mSideTxCharHandle);
    VerifyOrReturnError(ret == SL_STATUS_OK, MapBLEError(ret));

    ret = sl_bt_gattdb_commit(session);
    return MapBLEError(ret);
}
/** @brief ConfigureAdvertising
 *   Configure the advertising data and parameters for the BLE channel. This function needs to be called before
 *  starting the advertising process.
 *
 *  @param advData The advertising data to be set.
 * @param responseData The scan response data to be set.
 * @param intervalMin The minimum advertising interval.
 * @param intervalMax The maximum advertising interval.
 * @param duration The duration of the advertising.
 * @param maxEvents The maximum number of advertising events.
 *
 * @return CHIP_NO_ERROR on success, or a translation of the BLEErrorCode into CHIP_ERROR.
 */
CHIP_ERROR BLEChannel::ConfigureAdvertising(ByteSpan advData, ByteSpan responseData, uint32_t intervalMin, uint32_t intervalMax,
                                            uint16_t duration, uint8_t maxEvents)
{
    ChipLogProgress(DeviceLayer, "Configuring BLE Channel");

    sl_status_t ret;

    if (mAdvHandle == 0xff)
    {
        ret = sl_bt_advertiser_create_set(&mAdvHandle);
        VerifyOrReturnLogError(ret == SL_STATUS_OK, MapBLEError(ret));

        // TODO: Check if we need to randomize the address
        // Since a random address is not configured, configure one
        uint64_t random = Crypto::GetRandU64();
        // Copy random value to address. We don't care of the ordering since it's a random value.
        memcpy(&mRandomizedAddr, &random, sizeof(mRandomizedAddr));

        // Set two MSBs to 11 to properly the address - BLE Static Device Address requirement
        mRandomizedAddr.addr[5] |= 0xC0;

        ret = sl_bt_advertiser_set_random_address(mAdvHandle, sl_bt_gap_static_address, mRandomizedAddr, &mRandomizedAddr);
        VerifyOrReturnLogError(ret == SL_STATUS_OK, MapBLEError(ret));

        ChipLogDetail(DeviceLayer, "BLE Static Device Address %02X:%02X:%02X:%02X:%02X:%02X", mRandomizedAddr.addr[5],
                      mRandomizedAddr.addr[4], mRandomizedAddr.addr[3], mRandomizedAddr.addr[2], mRandomizedAddr.addr[1],
                      mRandomizedAddr.addr[0]);
    }

    ret = sl_bt_legacy_advertiser_set_data(mAdvHandle, sl_bt_advertiser_advertising_data_packet, advData.size(), advData.data());
    VerifyOrReturnLogError(ret == SL_STATUS_OK, MapBLEError(ret));

    ret = sl_bt_legacy_advertiser_set_data(mAdvHandle, sl_bt_advertiser_scan_response_packet, responseData.size(),
                                           responseData.data());
    VerifyOrReturnLogError(ret == SL_STATUS_OK, MapBLEError(ret));

    mAdvIntervalMin = intervalMin;
    mAdvIntervalMax = intervalMax;
    mAdvDuration    = duration;
    mAdvMaxEvents   = maxEvents;

    return CHIP_NO_ERROR;
}

/** @brief StartAdvertising
 *  Start the advertising process for the BLE channel using configured parameters. ConfiureAdvertising must be called before this
 * function.
 */
CHIP_ERROR BLEChannel::StartAdvertising(void)
{
    // TODO: Check for handling max connection per handle vs globally

    // If already advertising, stop it, before changing values
    if (mFlags.Has(Flags::kAdvertising))
    {
        sl_bt_advertiser_stop(mAdvHandle);
    }

    sl_status_t ret;

    ret = sl_bt_advertiser_set_timing(mAdvHandle, mAdvIntervalMin, mAdvIntervalMax, mAdvDuration, mAdvMaxEvents);
    VerifyOrReturnLogError(ret == SL_STATUS_OK, MapBLEError(ret));

    ret = sl_bt_advertiser_configure(mAdvHandle, 1); // TODO : Figure out this magic 1 in the sl_bt_advertiser_flags
    VerifyOrReturnLogError(ret == SL_STATUS_OK, MapBLEError(ret));

    // Start advertising
    ret = sl_bt_legacy_advertiser_start(mAdvHandle, sl_bt_advertiser_connectable_scannable);
    VerifyOrReturnLogError(ret == SL_STATUS_OK, MapBLEError(ret));

    mFlags.Set(Flags::kAdvertising);
    ChipLogProgress(DeviceLayer, "BLE Advertising started successfully");

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEChannel::StopAdvertising(void)
{
    if (mFlags.Has(Flags::kAdvertising))
    {
        sl_status_t ret = SL_STATUS_OK;

        mFlags.Clear(Flags::kAdvertising);
        // TODO: Confirm the fast vs slow advertising concept from a channel perspective vs from CHIPoBLE perspective
        // mFlags.Set(Flags::kFastAdvertisingEnabled, true);

        ret = sl_bt_advertiser_stop(mAdvHandle);
        sl_bt_advertiser_clear_random_address(mAdvHandle);
        sl_bt_advertiser_delete_set(mAdvHandle);
        mAdvHandle = 0xff;
        VerifyOrReturnLogError(CHIP_NO_ERROR == MapBLEError(ret), MapBLEError(ret));
    }

    return CHIP_NO_ERROR;
}

void BLEChannel::AddConnection(uint8_t connectionHandle, uint8_t bondingHandle)
{
    mConnectionState.connectionHandle = connectionHandle;
    mConnectionState.bondingHandle    = bondingHandle;
    mConnectionState.allocated        = 1;
    mConnectionState.subscribed       = 0;
}

bool BLEChannel::RemoveConnection(uint8_t connectionHandle)
{
    VerifyOrReturnValue(mConnectionState.allocated, false);
    VerifyOrReturnValue(mConnectionState.connectionHandle == connectionHandle, false);
    memset(&mConnectionState, 0, sizeof(mConnectionState));
    return true;
}

void BLEChannel::HandleReadRequest(volatile sl_bt_msg_t * evt, ByteSpan data)
{
    uint16_t sent_length; // TODO: Confirm how to leverage this or use a nullptr instead

    // Add logic to handle received data
    sl_bt_evt_gatt_server_user_read_request_t * readReq =
        (sl_bt_evt_gatt_server_user_read_request_t *) &(evt->data.evt_gatt_server_user_read_request);

    // TODO: define uint8_t error logic for app error for BLE response
    ChipLogProgress(DeviceLayer, "Handling Read Request for characteristic: %d", readReq->characteristic);
    sl_status_t ret = sl_bt_gatt_server_send_user_read_response(readReq->connection, readReq->characteristic, 0, data.size(),
                                                                data.data(), &sent_length);

    ChipLogProgress(DeviceLayer, "Sent %d of the %d bytes requested", sent_length, data.size());

    if (ret != SL_STATUS_OK)
    {
        ChipLogDetail(DeviceLayer, "Failed to send read response, err:%ld", ret);
    }
}

void BLEChannel::HandleWriteRequest(volatile sl_bt_msg_t * evt, MutableByteSpan data)
{
    sl_bt_evt_gatt_server_user_write_request_t * writeReq =
        (sl_bt_evt_gatt_server_user_write_request_t *) &(evt->data.evt_gatt_server_user_write_request);
    ChipLogProgress(DeviceLayer, "Handling Write Request for characteristic: %d", writeReq->characteristic);
    // TODO: Review what characteristic we want to offer as default, for now we just copy the data to a buffer

    VerifyOrReturn(data.size() >= writeReq->value.len, ChipLogError(DeviceLayer, "Buffer too small for write request"));
    memcpy(data.data(), writeReq->value.data, writeReq->value.len);

    ChipLogProgress(DeviceLayer, "Received Write Request for characteristic: %d, data size: %d", writeReq->characteristic,
                    writeReq->value.len);
    // Log the data received
    ChipLogByteSpan(DeviceLayer, data);

    // TODO: define uint8_t error logic for app error for BLE response
    sl_status_t ret = sl_bt_gatt_server_send_user_write_response(writeReq->connection, writeReq->characteristic, 0);
    if (ret != SL_STATUS_OK)
    {
        ChipLogDetail(DeviceLayer, "Failed to send write response, err:%ld", ret);
    }
}

CHIP_ERROR BLEChannel::HandleCCCDWriteRequest(volatile sl_bt_msg_t * evt, bool & isNewSubscription)
{
    ChipLogProgress(DeviceLayer, "Handling CCCD Write");

    sl_bt_evt_gatt_server_characteristic_status_t * CCCDWriteReq =
        (sl_bt_evt_gatt_server_characteristic_status_t *) &(evt->data.evt_gatt_server_characteristic_status);

    bool isIndicationEnabled = false;
    isNewSubscription        = false;

    VerifyOrReturnLogError(mConnectionState.allocated, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnLogError(mConnectionState.connectionHandle == CCCDWriteReq->connection, CHIP_ERROR_INCORRECT_STATE);

    isIndicationEnabled = (CCCDWriteReq->client_config_flags == sl_bt_gatt_indication);

    if (isIndicationEnabled)
    {
        if (mConnectionState.subscribed)
        {
            isNewSubscription = false; // Already subscribed
        }
        else
        {
            mConnectionState.subscribed = 1;
            ChipLogProgress(DeviceLayer, "CHIPoBLE Subscribe received for characteristic: %d", CCCDWriteReq->characteristic);
            isNewSubscription = true;
        }
    }
    else
    {
        mConnectionState.subscribed = 0;
        ChipLogProgress(DeviceLayer, "CHIPoBLE Unsubscribe received for characteristic: %d", CCCDWriteReq->characteristic);
        isNewSubscription = false;
    }

    // TODO: Leverage Endpoint to send event or implement a timer + callback here
    ChipLogProgress(DeviceLayer, "Note: This is not implemented yet");

    return CHIP_NO_ERROR;
}

void BLEChannel::UpdateMtu(volatile sl_bt_msg_t * evt)
{
    mConnectionState.mtu = evt->data.evt_gatt_mtu_exchanged.mtu;
    ChipLogProgress(DeviceLayer, "MTU exchanged: %d", evt->data.evt_gatt_mtu_exchanged.mtu);
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
