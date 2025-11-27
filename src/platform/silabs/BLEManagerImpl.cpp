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
 *          Provides an implementation of the BLEManager singleton object
 *          for the Silicon Labs EFR32 platforms.
 */

/* this file behaves like a config.h, comes first */
#include <platform/internal/CHIPDeviceLayerInternal.h>
#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE

#if defined(SL_COMPONENT_CATALOG_PRESENT)
#include "sl_component_catalog.h"
#endif

#include "sl_component_catalog.h"

#include <platform/internal/BLEManager.h>
#include <platform/silabs/BLEManagerImpl.h>
#include <platform/silabs/ble/BlePlatformInterface.h>

#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
// SiWx917 platform
#include <platform/silabs/SiWx/ble/BlePlatformSiWx.h>
#else
// EFR32 platform
#include "FreeRTOS.h"
#include "rail.h"
#include <platform/silabs/ble/efr32/BlePlatformEfr32.h>
extern "C" {
#include "sl_bluetooth.h"
}
#include "gatt_db.h"
#include "sl_bt_api.h"
#include "sl_bt_stack_config.h"
#include "sl_bt_stack_init.h"
#include <ble/efr32/BLEChannel.h>
#endif
#include "timers.h"
#include <ble/Ble.h>
#include <crypto/RandUtils.h>
#include <cstring>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInstanceInfoProvider.h>
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
#include <sl_bt_rtos_adaptation.h>
#endif

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
#include <setup_payload/AdditionalDataPayloadGenerator.h>
#endif

#include <headers/ProvisionChannel.h>
#include <headers/ProvisionManager.h>

using namespace ::chip;
using namespace ::chip::Ble;

// Helper macro to cast mPlatform void* to BlePlatformInterface*
#define PLATFORM() (static_cast<chip::DeviceLayer::Internal::Silabs::BlePlatformInterface *>(mPlatform))

namespace chip {
namespace DeviceLayer {
namespace Internal {

namespace {

#define CHIP_ADV_DATA_TYPE_FLAGS 0x01
#define CHIP_ADV_DATA_TYPE_UUID 0x03
#define CHIP_ADV_DATA_TYPE_NAME 0x09
#define CHIP_ADV_DATA_TYPE_SERVICE_DATA 0x16

#define CHIP_ADV_DATA_FLAGS 0x06

#define CHIP_ADV_DATA 0
#define CHIP_ADV_SCAN_RESPONSE_DATA 1
#define CHIP_ADV_SHORT_UUID_LEN 2

#define MAX_RESPONSE_DATA_LEN 31
#define MAX_ADV_DATA_LEN 31

// Timer Frequency used.
#define TIMER_CLK_FREQ ((uint32_t) 32768)
// Convert msec to timer ticks.
#define TIMER_MS_2_TIMERTICK(ms) ((TIMER_CLK_FREQ * ms) / 1000)
#define TIMER_S_2_TIMERTICK(s) (TIMER_CLK_FREQ * s)

#define BLE_MAX_BUFFER_SIZE (3076)
#define BLE_MAX_ADVERTISERS (1)
#define BLE_CONFIG_MAX_PERIODIC_ADVERTISING_SYNC (0)
#define BLE_CONFIG_MAX_SOFTWARE_TIMERS (4)
#define BLE_CONFIG_MIN_TX_POWER (-30)
#define BLE_CONFIG_MAX_TX_POWER (80)
#define BLE_CONFIG_RF_PATH_GAIN_TX (0)
#define BLE_CONFIG_RF_PATH_GAIN_RX (0)

// Default Connection  parameters
#define BLE_CONFIG_MIN_INTERVAL (16) // Time = Value x 1.25 ms = 20ms
#define BLE_CONFIG_MAX_INTERVAL (80) // Time = Value x 1.25 ms = 100ms
#define BLE_CONFIG_LATENCY (0)
#define BLE_CONFIG_TIMEOUT (100)          // Time = Value x 10 ms = 1s
#define BLE_CONFIG_MIN_CE_LENGTH (0)      // Leave to min value
#define BLE_CONFIG_MAX_CE_LENGTH (0xFFFF) // Leave to max value

#define BLE_CONFIG_MIN_INTERVAL_SC (32)   // Time = Value * 0.625 ms = 20ms
#define BLE_CONFIG_MAX_INTERVAL_SC (8000) // Time = Value * 0.625 ms = 5s

osTimerId_t sbleAdvTimeoutTimer; // SW timer

#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
constexpr uint32_t kBleTaskSize = 2560;
uint8_t bleStack[kBleTaskSize];
osThread_t sBleTaskControlBlock;
constexpr osThreadAttr_t kBleTaskAttr = { .name       = "rsi_ble",
                                          .attr_bits  = osThreadDetached,
                                          .cb_mem     = &sBleTaskControlBlock,
                                          .cb_size    = osThreadCbSize,
                                          .stack_mem  = bleStack,
                                          .stack_size = kBleTaskSize,
                                          .priority   = osPriorityHigh };
#endif

const uint8_t UUID_CHIPoBLEService[]      = { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                              0x00, 0x10, 0x00, 0x00, 0xF6, 0xFF, 0x00, 0x00 };
const uint8_t ShortUUID_CHIPoBLEService[] = { 0xF6, 0xFF };

} // namespace

BLEManagerImpl BLEManagerImpl::sInstance;

CHIP_ERROR BLEManagerImpl::_Init()
{
    CHIP_ERROR err;

    // Initialize the CHIP BleLayer.
    err = BleLayer::Init(this, this, &DeviceLayer::SystemLayer());
    SuccessOrExit(err);

    // Initialize platform interface using factory function
    mPlatform = Silabs::GetBlePlatformInstance();
    VerifyOrReturnError(mPlatform != nullptr, CHIP_ERROR_INCORRECT_STATE);
    ReturnErrorOnFailure(PLATFORM()->Init());
    PLATFORM()->SetManager(this);

#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    // Create BLE thread for event handling after platform initialization.
    if (osThreadNew(Silabs::BlePlatformSiWx917::sl_ble_event_handling_task, &Silabs::BlePlatformSiWx917::GetInstance(),
                    &kBleTaskAttr) == nullptr)
    {
        err = CHIP_ERROR_INCORRECT_STATE;
        SuccessOrExit(err);
    }
#endif

#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    // EFR32: This line hasn't changed but since the BLEConState definition has moved to
    // BLEChannel.h, the compiler seems to think that the memset is not valid
    // since the size of the struct is not known here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmemset-elt-size"
#endif
    memset(mBleConnections, 0, sizeof(mBleConnections));
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
#pragma GCC diagnostic pop
#endif
    memset(mIndConfId, kUnusedIndex, sizeof(mIndConfId));
    mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_Enabled;

    // SW timer for BLE timeouts and interval change.
    sbleAdvTimeoutTimer = osTimerNew(BleAdvTimeoutHandler, osTimerOnce, NULL, NULL);

    mFlags.ClearAll().Set(Flags::kAdvertisingEnabled, CHIP_DEVICE_CONFIG_CHIPOBLE_ENABLE_ADVERTISING_AUTOSTART);
    mFlags.Set(Flags::kFastAdvertisingEnabled, true);

    PlatformMgr().ScheduleWork(DriveBLEState, 0);

exit:
    return err;
}

uint16_t BLEManagerImpl::_NumConnections(void)
{
    uint16_t numCons = 0;
    for (uint16_t i = 0; i < kMaxConnections; i++)
    {
        if (mBleConnections[i].allocated)
        {
            numCons++;
        }
    }

    return numCons;
}

CHIP_ERROR BLEManagerImpl::_SetAdvertisingEnabled(bool val)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    VerifyOrExit(mServiceMode != ConnectivityManager::kCHIPoBLEServiceMode_NotSupported, err = CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    if (mFlags.Has(Flags::kAdvertisingEnabled) != val)
    {
        mFlags.Set(Flags::kAdvertisingEnabled, val);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }

exit:
    return err;
}

CHIP_ERROR BLEManagerImpl::_SetAdvertisingMode(BLEAdvertisingMode mode)
{
    switch (mode)
    {
    case BLEAdvertisingMode::kFastAdvertising:
        mFlags.Set(Flags::kFastAdvertisingEnabled, true);
        break;
    case BLEAdvertisingMode::kSlowAdvertising:
        mFlags.Set(Flags::kFastAdvertisingEnabled, false);
        break;
    default:
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    mFlags.Set(Flags::kRestartAdvertising);
    PlatformMgr().ScheduleWork(DriveBLEState, 0);
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::_GetDeviceName(char * buf, size_t bufSize)
{
    if (strlen(mDeviceName) >= bufSize)
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    strcpy(buf, mDeviceName);
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::_SetDeviceName(const char * deviceName)
{
    if (mServiceMode == ConnectivityManager::kCHIPoBLEServiceMode_NotSupported)
    {
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }
    if (deviceName != NULL && deviceName[0] != 0)
    {
        if (strlen(deviceName) >= kMaxDeviceNameLength)
        {
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        strcpy(mDeviceName, deviceName);
        mFlags.Set(Flags::kDeviceNameSet);
        mFlags.Set(Flags::kRestartAdvertising);
        ChipLogProgress(DeviceLayer, "Setting device name to : \"%s\"", mDeviceName);
    }
    else
    {
        mDeviceName[0] = 0;
    }
    PlatformMgr().ScheduleWork(DriveBLEState, 0);
    return CHIP_NO_ERROR;
}

void BLEManagerImpl::_OnPlatformEvent(const ChipDeviceEvent * event)
{
    switch (event->Type)
    {
    case DeviceEventType::kCHIPoBLESubscribe: {
        ChipDeviceEvent connEstEvent;

        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLESubscribe");
        HandleSubscribeReceived(event->CHIPoBLESubscribe.ConId, &CHIP_BLE_SVC_ID, &Ble::CHIP_BLE_CHAR_2_UUID);
        connEstEvent.Type = DeviceEventType::kCHIPoBLEConnectionEstablished;
        PlatformMgr().PostEventOrDie(&connEstEvent);
    }
    break;

    case DeviceEventType::kCHIPoBLEUnsubscribe: {
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLEUnsubscribe");
        HandleUnsubscribeReceived(event->CHIPoBLEUnsubscribe.ConId, &CHIP_BLE_SVC_ID, &Ble::CHIP_BLE_CHAR_2_UUID);
    }
    break;

    case DeviceEventType::kCHIPoBLEWriteReceived: {
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLEWriteReceived");
        HandleWriteReceived(event->CHIPoBLEWriteReceived.ConId, &CHIP_BLE_SVC_ID, &Ble::CHIP_BLE_CHAR_1_UUID,
                            PacketBufferHandle::Adopt(event->CHIPoBLEWriteReceived.Data));
    }
    break;

    case DeviceEventType::kCHIPoBLEConnectionError: {
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLEConnectionError");
        HandleConnectionError(event->CHIPoBLEConnectionError.ConId, event->CHIPoBLEConnectionError.Reason);
    }
    break;

    case DeviceEventType::kCHIPoBLEIndicateConfirm: {
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLEIndicateConfirm");
        HandleIndicationConfirmation(event->CHIPoBLEIndicateConfirm.ConId, &CHIP_BLE_SVC_ID, &Ble::CHIP_BLE_CHAR_2_UUID);
    }
    break;

    default:
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent default:  event->Type = %d", event->Type);
        break;
    }
}

CHIP_ERROR BLEManagerImpl::SubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId,
                                                   const ChipBleUUID * charId)
{
    ChipLogProgress(DeviceLayer, "BLEManagerImpl::SubscribeCharacteristic() not supported");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR BLEManagerImpl::UnsubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId,
                                                     const ChipBleUUID * charId)
{
    ChipLogProgress(DeviceLayer, "BLEManagerImpl::UnsubscribeCharacteristic() not supported");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR BLEManagerImpl::CloseConnection(BLE_CONNECTION_OBJECT conId)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    ChipLogProgress(DeviceLayer, "Closing BLE GATT connection (con %u)", conId);

    VerifyOrReturnError(mPlatform != nullptr, CHIP_ERROR_INCORRECT_STATE);
    err = PLATFORM()->CloseConnection(static_cast<uint8_t>(conId));

    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "BLE connection close failed: %" CHIP_ERROR_FORMAT, err.Format());
    }

    return err;
}

uint16_t BLEManagerImpl::GetMTU(BLE_CONNECTION_OBJECT conId) const
{
    VerifyOrReturnValue(mPlatform != nullptr, 0);
    return PLATFORM()->GetMTU(static_cast<uint8_t>(conId));
}

CHIP_ERROR BLEManagerImpl::SendIndication(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId, const ChipBleUUID * charId,
                                          PacketBufferHandle data)
{
    CHIP_ERROR err         = CHIP_NO_ERROR;
    BLEConState * conState = GetConnectionState(conId);
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    uint16_t cId = (UUIDsMatch(&Ble::CHIP_BLE_CHAR_1_UUID, charId) ? gattdb_CHIPoBLEChar_Rx : gattdb_CHIPoBLEChar_Tx);
#else
    // SiWx917: Platform implementation uses mRsiBleMeasurementHndl internally, characteristic parameter is ignored
    uint16_t cId = 0;
#endif
    uint8_t timerHandle = GetTimerHandle(conId, true);
    ByteSpan dataSpan(data->Start(), data->DataLength()); // Declare early to avoid jump-to-label error

    VerifyOrExit(((conState != NULL) && (conState->subscribed != 0)), err = CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrExit(timerHandle != kMaxConnections, err = CHIP_ERROR_NO_MEMORY);

    // start timer for light indication confirmation. Long delay for spake2 indication
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    sl_bt_system_set_lazy_soft_timer(TIMER_S_2_TIMERTICK(6), 0, timerHandle, true);
#else
    DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(6000), // 6 seconds
                                          OnSendIndicationTimeout, this);
#endif

    err = PLATFORM()->SendIndication(static_cast<uint8_t>(conId), cId, dataSpan);

exit:
    return err;
}

CHIP_ERROR BLEManagerImpl::SendWriteRequest(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId, const ChipBleUUID * charId,
                                            PacketBufferHandle pBuf)
{
    ChipLogProgress(DeviceLayer, "BLEManagerImpl::SendWriteRequest() not supported");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

void BLEManagerImpl::NotifyChipConnectionClosed(BLE_CONNECTION_OBJECT conId)
{
    CloseConnection(conId);
}

CHIP_ERROR BLEManagerImpl::MapBLEError(int bleErr)
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

void BLEManagerImpl::DriveBLEState(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    // Check if BLE stack is initialized
    VerifyOrExit(mFlags.Has(Flags::kSiLabsBLEStackInitialize), /* */);

    // Start advertising if needed...
    if (mServiceMode == ConnectivityManager::kCHIPoBLEServiceMode_Enabled && mFlags.Has(Flags::kAdvertisingEnabled) &&
        NumConnections() < kMaxConnections)
    {
        // Start/re-start advertising if not already started, or if there is a pending change
        // to the advertising configuration.
        if (!mFlags.Has(Flags::kAdvertising) || mFlags.Has(Flags::kRestartAdvertising))
        {
            err = StartAdvertising();
            SuccessOrExit(err);
        }
    }

    // Otherwise, stop advertising if it is enabled.
    else if (mFlags.Has(Flags::kAdvertising))
    {
        err = StopAdvertising();
        SuccessOrExit(err);
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Disabling CHIPoBLE service due to error: %" CHIP_ERROR_FORMAT, err.Format());
        mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_Disabled;
    }
}

CHIP_ERROR BLEManagerImpl::ConfigureAdvertisingData(void)
{
    ChipBLEDeviceIdentificationInfo mDeviceIdInfo;
    CHIP_ERROR err;
    uint8_t responseData[MAX_RESPONSE_DATA_LEN];
    uint8_t advData[MAX_ADV_DATA_LEN];
    uint32_t advLen             = 0;
    uint32_t respLen            = 0;
    uint32_t mDeviceNameLength  = 0;
    uint8_t mDeviceIdInfoLength = 0;
    Silabs::BleAdvertisingConfig advConfig;

    VerifyOrExit((kMaxDeviceNameLength + 1) < UINT8_MAX, err = CHIP_ERROR_INVALID_ARGUMENT);

    memset(responseData, 0, MAX_RESPONSE_DATA_LEN);
    memset(advData, 0, MAX_ADV_DATA_LEN);

    err = ConfigurationMgr().GetBLEDeviceIdentificationInfo(mDeviceIdInfo);
    SuccessOrExit(err);

    if (!mFlags.Has(Flags::kDeviceNameSet))
    {
        uint16_t discriminator;
        SuccessOrExit(err = GetCommissionableDataProvider()->GetSetupDiscriminator(discriminator));

        snprintf(mDeviceName, sizeof(mDeviceName), "%s%04u", CHIP_DEVICE_CONFIG_BLE_DEVICE_NAME_PREFIX, discriminator);

        mDeviceName[kMaxDeviceNameLength] = 0;
        mDeviceNameLength                 = strlen(mDeviceName);

        VerifyOrExit(mDeviceNameLength < kMaxDeviceNameLength, err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    mDeviceNameLength = strlen(mDeviceName); // Device Name length + length field
    VerifyOrExit(mDeviceNameLength < kMaxDeviceNameLength, err = CHIP_ERROR_INVALID_ARGUMENT);
    static_assert((kUUIDTlvSize + kDeviceNameTlvSize) <= MAX_RESPONSE_DATA_LEN, "Scan Response buffer is too small");

    mDeviceIdInfoLength = sizeof(mDeviceIdInfo); // Servicedatalen + length+ UUID (Short)
    static_assert(sizeof(mDeviceIdInfo) + CHIP_ADV_SHORT_UUID_LEN + 1 <= UINT8_MAX, "Our length won't fit in a uint8_t");
    static_assert(2 + CHIP_ADV_SHORT_UUID_LEN + sizeof(mDeviceIdInfo) + 1 <= MAX_ADV_DATA_LEN, "Our buffer is not big enough");

    advLen            = 0;
    advData[advLen++] = 0x02;                                                                    // length
    advData[advLen++] = CHIP_ADV_DATA_TYPE_FLAGS;                                                // AD type : flags
    advData[advLen++] = CHIP_ADV_DATA_FLAGS;                                                     // AD value
    advData[advLen++] = static_cast<uint8_t>(mDeviceIdInfoLength + CHIP_ADV_SHORT_UUID_LEN + 1); // AD length
    advData[advLen++] = CHIP_ADV_DATA_TYPE_SERVICE_DATA;                                         // AD type : Service Data
    advData[advLen++] = ShortUUID_CHIPoBLEService[0];                                            // AD value
    advData[advLen++] = ShortUUID_CHIPoBLEService[1];

#if CHIP_DEVICE_CONFIG_EXT_ADVERTISING
    // Check for extended advertisement interval and redact VID/PID if past the initial period.
    if (mFlags.Has(Flags::kExtAdvertisingEnabled))
    {
        mDeviceIdInfo.SetVendorId(0);
        mDeviceIdInfo.SetProductId(0);
        mDeviceIdInfo.SetExtendedAnnouncementFlag(true);
    }
#endif

    memcpy(&advData[advLen], (void *) &mDeviceIdInfo, mDeviceIdInfoLength); // AD value
    advLen += mDeviceIdInfoLength;

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    ReturnErrorOnFailure(EncodeAdditionalDataTlv());
#endif

    // Build scan response data
    respLen                 = 0;
    responseData[respLen++] = CHIP_ADV_SHORT_UUID_LEN + 1;  // AD length
    responseData[respLen++] = CHIP_ADV_DATA_TYPE_UUID;      // AD type : uuid
    responseData[respLen++] = ShortUUID_CHIPoBLEService[0]; // AD value
    responseData[respLen++] = ShortUUID_CHIPoBLEService[1];

    responseData[respLen++] = static_cast<uint8_t>(mDeviceNameLength + 1); // length
    responseData[respLen++] = CHIP_ADV_DATA_TYPE_NAME;                     // AD type : name
    memcpy(&responseData[respLen], mDeviceName, mDeviceNameLength);        // AD value
    respLen += mDeviceNameLength;

    // Use platform interface to configure advertising
    advConfig.advData           = ByteSpan(advData, advLen);
    advConfig.responseData      = ByteSpan(responseData, respLen);
    advConfig.advertisingHandle = mAdvertisingSetHandle;

#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    // EFR32: Set random address if needed (handled by platform implementation)
    if (mAdvertisingSetHandle == 0xff) // Invalid advertising handle
    {
        advConfig.advData      = ByteSpan(advData, advLen);
        advConfig.responseData = ByteSpan(responseData, respLen);
    }
#endif

    err = PLATFORM()->ConfigureAdvertising(advConfig);
    SuccessOrExit(err);

    // Get advertising handle from platform
    mAdvertisingSetHandle = PLATFORM()->GetAdvertisingHandle();

exit:
    return err;
}

CHIP_ERROR BLEManagerImpl::StartAdvertising(void)
{
    CHIP_ERROR err          = CHIP_NO_ERROR;
    uint32_t interval_min   = 0;
    uint32_t interval_max   = 0;
    bool postAdvChangeEvent = false;
    bool connectable        = (NumConnections() < kMaxConnections);

    VerifyOrReturnError(mPlatform != nullptr, CHIP_ERROR_INCORRECT_STATE);

    // If already advertising, stop it before changing values.
    if (mFlags.Has(Flags::kAdvertising))
    {
        PLATFORM()->StopAdvertising();

        mFlags.Clear(Flags::kAdvertising).Clear(Flags::kRestartAdvertising);
        mFlags.Set(Flags::kFastAdvertisingEnabled, true);
        mAdvertisingSetHandle = 0xff; // invalidate handle so platform reassigns
        CancelBleAdvTimeoutTimer();

        ChipDeviceEvent advChange;
        advChange.Type                             = DeviceEventType::kCHIPoBLEAdvertisingChange;
        advChange.CHIPoBLEAdvertisingChange.Result = kActivity_Stopped;
        PlatformMgr().PostEventOrDie(&advChange);
    }
    else
    {
        ChipLogDetail(DeviceLayer, "Start BLE advertisement");
        postAdvChangeEvent = true;
    }

    err = ConfigureAdvertisingData();
    SuccessOrExit(err);

    mFlags.Clear(Flags::kRestartAdvertising);

    if (mFlags.Has(Flags::kFastAdvertisingEnabled))
    {
        interval_min = CHIP_DEVICE_CONFIG_BLE_FAST_ADVERTISING_INTERVAL_MIN;
        interval_max = CHIP_DEVICE_CONFIG_BLE_FAST_ADVERTISING_INTERVAL_MAX;
    }
    else
    {
#if CHIP_DEVICE_CONFIG_EXT_ADVERTISING
        if (!mFlags.Has(Flags::kExtAdvertisingEnabled))
        {
            interval_min = CHIP_DEVICE_CONFIG_BLE_SLOW_ADVERTISING_INTERVAL_MIN;
            interval_max = CHIP_DEVICE_CONFIG_BLE_SLOW_ADVERTISING_INTERVAL_MAX;
        }
        else
        {
            interval_min = CHIP_DEVICE_CONFIG_BLE_EXT_ADVERTISING_INTERVAL_MIN;
            interval_max = CHIP_DEVICE_CONFIG_BLE_EXT_ADVERTISING_INTERVAL_MAX;
        }
#else
        interval_min = CHIP_DEVICE_CONFIG_BLE_SLOW_ADVERTISING_INTERVAL_MIN;
        interval_max = CHIP_DEVICE_CONFIG_BLE_SLOW_ADVERTISING_INTERVAL_MAX;
#endif
    }

    err = PLATFORM()->StartAdvertising(interval_min, interval_max, connectable);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "StartAdvertising returned error: %" CHIP_ERROR_FORMAT, err.Format());
    }
    SuccessOrExit(err);

    // Get advertising handle from platform
    mAdvertisingSetHandle = PLATFORM()->GetAdvertisingHandle();

    if (mFlags.Has(Flags::kFastAdvertisingEnabled))
    {
        StartBleAdvTimeoutTimer(CHIP_DEVICE_CONFIG_BLE_ADVERTISING_INTERVAL_CHANGE_TIME);
    }
    mFlags.Set(Flags::kAdvertising);

    if (postAdvChangeEvent)
    {
        // Post CHIPoBLEAdvertisingChange event.
        ChipDeviceEvent advChange;
        advChange.Type                             = DeviceEventType::kCHIPoBLEAdvertisingChange;
        advChange.CHIPoBLEAdvertisingChange.Result = kActivity_Started;

        ReturnErrorOnFailure(PlatformMgr().PostEvent(&advChange));
    }

exit:
    return err;
}

CHIP_ERROR BLEManagerImpl::StopAdvertising(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (mFlags.Has(Flags::kAdvertising))
    {
        VerifyOrReturnError(mPlatform != nullptr, CHIP_ERROR_INCORRECT_STATE);
        PLATFORM()->StopAdvertising();

        mFlags.Clear(Flags::kAdvertising).Clear(Flags::kRestartAdvertising);
        mFlags.Set(Flags::kFastAdvertisingEnabled, true);
        mAdvertisingSetHandle = 0xff; // invalidate
        CancelBleAdvTimeoutTimer();

        ChipDeviceEvent advChange;
        advChange.Type                             = DeviceEventType::kCHIPoBLEAdvertisingChange;
        advChange.CHIPoBLEAdvertisingChange.Result = kActivity_Stopped;
        err                                        = PlatformMgr().PostEvent(&advChange);
    }

    return err;
}
#if defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED

CHIP_ERROR BLEManagerImpl::SideChannelConfigureAdvertisingDefaultData(void)
{
    VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);

    uint8_t advData[MAX_ADV_DATA_LEN];
    uint32_t index = 0;

    // Flags
    advData[index++] = 2;                        // Length
    advData[index++] = CHIP_ADV_DATA_TYPE_FLAGS; // Flags AD Type
    advData[index++] = 0x06;                     // LE General Discoverable Mode, BR/EDR not supported

    // Service UUID
    advData[index++] = 3;                       // Length
    advData[index++] = CHIP_ADV_DATA_TYPE_UUID; // 16-bit UUID
    advData[index++] = 0x34;                    // UUID 0x1234 (little endian)
    advData[index++] = 0x12;
    ByteSpan advDataSpan(advData, index);

    uint8_t responseData[MAX_RESPONSE_DATA_LEN];
    index = 0;

    const char * sideChannelName = "Si-Channel";
    size_t sideChannelNameLen    = strlen(sideChannelName);

    responseData[index++] = static_cast<uint8_t>(sideChannelNameLen + 1);
    responseData[index++] = 0x09; // Complete Local Name
    memcpy(&responseData[index], sideChannelName, sideChannelNameLen);
    index += sideChannelNameLen;
    ByteSpan responseDataSpan(responseData, index);

    AdvConfigStruct config = { advDataSpan,
                               responseDataSpan,
                               BLE_CONFIG_MIN_INTERVAL_SC,
                               BLE_CONFIG_MAX_INTERVAL_SC,
                               sl_bt_advertiser_connectable_scannable,
                               0,
                               0 };
    return mBleSideChannel->ConfigureAdvertising(config);
}

CHIP_ERROR BLEManagerImpl::InjectSideChannel(BLEChannel * channel)
{
    VerifyOrReturnError(nullptr != channel, CHIP_ERROR_INVALID_ARGUMENT);
    mBleSideChannel = channel;
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::SideChannelConfigureAdvertising(ByteSpan advData, ByteSpan responseData, uint32_t intervalMin,
                                                           uint32_t intervalMax, uint16_t duration, uint8_t maxEvents)
{
    VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
    AdvConfigStruct config = { advData,  responseData, intervalMin, intervalMax, sl_bt_advertiser_connectable_scannable,
                               duration, maxEvents };
    return mBleSideChannel->ConfigureAdvertising(config);
}

CHIP_ERROR BLEManagerImpl::SideChannelStartAdvertising(void)
{
    VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
    return mBleSideChannel->StartAdvertising();
}

CHIP_ERROR BLEManagerImpl::SideChannelStopAdvertising(void)
{
    VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
    return mBleSideChannel->StopAdvertising();
}

void BLEManagerImpl::HandleReadEvent(void * platformEvent)
{
    VerifyOrReturn(mPlatform != nullptr);
    Silabs::BleEvent unifiedEvent;
    VerifyOrReturn(PLATFORM()->ParseEvent(platformEvent, unifiedEvent));

    if (unifiedEvent.type == Silabs::BleEventType::kGattReadRequest)
    {
        const auto & readData = unifiedEvent.data.gattReadRequest;

        // Check if this is a CHIPoBLE connection or characteristic
        if (GetConnectionState(readData.connection) || PLATFORM()->IsChipoBleCharacteristic(readData.characteristic))
        {
            // Sends invalid handle if the user attempts to read a value other than C3 on CHIPoBLE
            // or if the user attempts to read a CHIPoBLE characteristic on the side channel
            ByteSpan emptyData(nullptr, 0);
            PLATFORM()->SendReadResponse(readData.connection, readData.characteristic, emptyData);
            return;
        }
        else
        {
            // Handle non-CHIPoBLE read (platform-specific logic, e.g., side channel)
            PLATFORM()->HandleNonChipoBleRead(platformEvent, readData.connection, readData.characteristic);
        }
    }
}
#endif // defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED

// Helper methods for side channel
bool BLEManagerImpl::HandleSideChannelConnection(uint8_t connection, uint8_t bonding)
{
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
    if (mBleSideChannel != nullptr)
    {
        ChipLogProgress(DeviceLayer, "Connect Event for SideChannel on handle : %d", connection);
        mBleSideChannel->AddConnection(connection, bonding);
        return true;
    }
#else
    (void) connection;
    (void) bonding;
#endif
    return false;
}

bool BLEManagerImpl::HandleSideChannelWrite(void * platformEvent)
{
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
    if (mBleSideChannel != nullptr)
    {
        uint8_t dataBuff[255] = { 0 };
        MutableByteSpan dataSpan(dataBuff);
        mBleSideChannel->HandleWriteRequest(static_cast<volatile sl_bt_msg_t *>(platformEvent), dataSpan);

        // Buffered (&Deleted) the following data:
        ChipLogProgress(DeviceLayer, "Buffered (&Deleted) the following data:");
        ChipLogByteSpan(DeviceLayer, dataSpan);
        return true;
    }
#else
    (void) platformEvent;
#endif
    return false;
}

bool BLEManagerImpl::HandleSideChannelRead(void * platformEvent, uint8_t connection, uint16_t characteristic)
{
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
    if (mBleSideChannel != nullptr)
    {
        // Side channel read request
        ChipLogProgress(DeviceLayer, "Char Read Req, char : %d", characteristic);

        char dataBuff[] = "You are reading the Si-Channel TX characteristic";
        ByteSpan dataSpan((const uint8_t *) dataBuff, sizeof(dataBuff));
        mBleSideChannel->HandleReadRequest(static_cast<volatile sl_bt_msg_t *>(platformEvent), dataSpan);
        return true;
    }
#else
    (void) platformEvent;
    (void) connection;
    (void) characteristic;
#endif
    return false;
}

bool BLEManagerImpl::HandleSideChannelMtuUpdate(void * platformEvent, uint8_t connection)
{
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
    if (mBleSideChannel != nullptr)
    {
        mBleSideChannel->UpdateMtu(static_cast<volatile sl_bt_msg_t *>(platformEvent));
        return true;
    }
#else
    (void) platformEvent;
    (void) connection;
#endif
    return false;
}

bool BLEManagerImpl::HandleSideChannelDisconnect(uint8_t connection)
{
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
    if (mBleSideChannel != nullptr)
    {
        ChipLogProgress(DeviceLayer, "Disconnect Event for the Side Channel on handle : %d", connection);
        mBleSideChannel->RemoveConnection(connection);
        return true;
    }
#else
    (void) connection;
#endif
    return false;
}

CHIP_ERROR BLEManagerImpl::HandleSideChannelCccdWrite(void * platformEvent, bool & isNewSubscription)
{
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
    if (mBleSideChannel != nullptr)
    {
        return mBleSideChannel->HandleCCCDWriteRequest(static_cast<volatile sl_bt_msg_t *>(platformEvent), isNewSubscription);
    }
#else
    (void) platformEvent;
    (void) isNewSubscription;
#endif
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

void BLEManagerImpl::UpdateMtu(void * platformEvent)
{
    VerifyOrReturn(mPlatform != nullptr);
    Silabs::BleEvent unifiedEvent;
    VerifyOrReturn(PLATFORM()->ParseEvent(platformEvent, unifiedEvent));
    if (unifiedEvent.type == Silabs::BleEventType::kGattMtuExchanged)
    {
        const auto & mtuData       = unifiedEvent.data.mtuExchanged;
        BLEConState * bleConnState = GetConnectionState(mtuData.connection);
        if (bleConnState != NULL)
        {
            // Update platform interface connection state
            Silabs::BleConnectionState * platformConnState = PLATFORM()->GetConnectionState(mtuData.connection, false);
            if (platformConnState != nullptr)
            {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
                platformConnState->mtu = mtuData.mtu;
#pragma GCC diagnostic pop
            }

            // bleConnState->MTU is a 10-bit field inside a uint16_t.  We're
            // assigning to it from a uint16_t, and compilers warn about
            // possibly not fitting.  There's no way to suppress that warning
            // via explicit cast; we have to disable the warning around the
            // assignment.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
            bleConnState->mtu = mtuData.mtu;
#pragma GCC diagnostic pop
        }
        else
        {
            // Handle non-CHIPoBLE MTU update (platform-specific logic, e.g., side channel)
            PLATFORM()->HandleNonChipoBleMtuUpdate(platformEvent, mtuData.connection);
        }
    }
}

void BLEManagerImpl::HandleBootEvent(void)
{
    mFlags.Set(Flags::kSiLabsBLEStackInitialize);
    PlatformMgr().ScheduleWork(DriveBLEState, 0);
}

extern "C" void ChipBlePlatform_NotifyStackReady()
{
    BLEMgrImpl().HandleBootEvent();
}

#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
extern "C" void ChipBlePlatform_HandleEvent(void * platformEvent, int eventType)
{
    // Map SiWx platform event types to BLEManagerImpl handlers.
    // This function is SiWx917-specific.
    using namespace chip::DeviceLayer::Internal::Silabs;

    switch (static_cast<SilabsBleWrapper::BleEventType>(eventType))
    {
    case SilabsBleWrapper::BleEventType::RSI_BLE_CONN_EVENT:
        ChipLogProgress(DeviceLayer, "ChipBlePlatform_HandleEvent: dispatching CONNECT event");
        BLEMgrImpl().HandleConnectEvent(platformEvent);
        break;

    case SilabsBleWrapper::BleEventType::RSI_BLE_DISCONN_EVENT:
        ChipLogProgress(DeviceLayer, "ChipBlePlatform_HandleEvent: dispatching DISCONNECT event");
        BLEMgrImpl().HandleConnectionCloseEvent(platformEvent);
        break;

    case SilabsBleWrapper::BleEventType::RSI_BLE_GATT_WRITE_EVENT:
        ChipLogProgress(DeviceLayer, "ChipBlePlatform_HandleEvent: dispatching GATT_WRITE event");
        BLEMgrImpl().HandleWriteEvent(platformEvent);
        break;

    case SilabsBleWrapper::BleEventType::RSI_BLE_MTU_EVENT:
        ChipLogProgress(DeviceLayer, "ChipBlePlatform_HandleEvent: dispatching MTU event");
        BLEMgrImpl().UpdateMtu(platformEvent);
        break;

    case SilabsBleWrapper::BleEventType::RSI_BLE_GATT_INDICATION_CONFIRMATION:
        ChipLogProgress(DeviceLayer, "ChipBlePlatform_HandleEvent: dispatching INDICATION_CONFIRM event");
        // SiWx: Route to the SiWx-specific HandleTxConfirmationEvent
        BLEMgrImpl().HandleTxConfirmationEvent(1); // SiWx uses connection handle 1
        break;

        /* RSI_BLE_EVENT_GATT_RD handling is conditional; ignore here */

    default:
        ChipLogProgress(DeviceLayer, "ChipBlePlatform_HandleEvent: unhandled eventType=%d", eventType);
        break;
    }
}
#endif // (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)

void BLEManagerImpl::HandleConnectEvent(void * platformEvent)
{
    VerifyOrReturn(mPlatform != nullptr);
    Silabs::BleEvent unifiedEvent;
    VerifyOrReturn(PLATFORM()->ParseEvent(platformEvent, unifiedEvent));
    if (unifiedEvent.type == Silabs::BleEventType::kConnectionOpened)
    {
        const auto & connData = unifiedEvent.data.connectionOpened;

        // Use platform interface to check if this is a CHIPoBLE connection
        if (PLATFORM()->IsChipoBleConnection(connData.connection, connData.advertiser, mAdvertisingSetHandle))
        {
            ChipLogProgress(DeviceLayer, "Connect Event for CHIPoBLE on handle : %d", connData.connection);

            // Add to platform interface connection state
            PLATFORM()->AddConnection(connData.connection, connData.bonding, connData.address);

            // Also add to BLEManagerImpl's connection state for CHIPoBLE-specific tracking
            AddConnection(connData.connection, connData.bonding);
            PlatformMgr().ScheduleWork(DriveBLEState, 0);
        }
        else
        {
            // Handle non-CHIPoBLE connection (platform-specific logic)
            PLATFORM()->HandleNonChipoBleConnection(connData.connection, connData.advertiser, connData.bonding, connData.address,
                                                    mAdvertisingSetHandle);
        }
    }
}

void BLEManagerImpl::HandleConnectParams(void * platformEvent)
{
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    // EFR32-specific: Connection parameters handling
    volatile sl_bt_msg_t * evt = static_cast<volatile sl_bt_msg_t *>(platformEvent);
    if (evt == nullptr)
    {
        return;
    }

    sl_bt_evt_connection_parameters_t * con_param_evt = (sl_bt_evt_connection_parameters_t *) &(evt->data);

    ChipLogProgress(DeviceLayer, "Connection Parameters Event for handle : %d", con_param_evt->connection);
    ChipLogProgress(DeviceLayer, "Interval: %d, Latency: %d, Timeout: %d", con_param_evt->interval, con_param_evt->latency,
                    con_param_evt->timeout);

    uint16_t desiredTimeout = con_param_evt->timeout < BLE_CONFIG_TIMEOUT ? BLE_CONFIG_TIMEOUT : con_param_evt->timeout;

    // For better stability, renegotiate the connection parameters if the received ones from the central are outside
    // of our defined constraints
    if (desiredTimeout != con_param_evt->timeout || con_param_evt->interval < BLE_CONFIG_MIN_INTERVAL ||
        con_param_evt->interval > BLE_CONFIG_MAX_INTERVAL)
    {
        ChipLogProgress(DeviceLayer, "Renegotiate BLE connection parameters to minInterval:%d, maxInterval:%d, timeout:%d",
                        BLE_CONFIG_MIN_INTERVAL, BLE_CONFIG_MAX_INTERVAL, desiredTimeout);
        sl_bt_connection_set_parameters(con_param_evt->connection, BLE_CONFIG_MIN_INTERVAL, BLE_CONFIG_MAX_INTERVAL,
                                        BLE_CONFIG_LATENCY, desiredTimeout, BLE_CONFIG_MIN_CE_LENGTH, BLE_CONFIG_MAX_CE_LENGTH);
    }

    if (nullptr != GetConnectionState(con_param_evt->connection, false))
    {
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }
#endif // !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
}

void BLEManagerImpl::HandleConnectionCloseEvent(void * platformEvent)
{
    VerifyOrReturn(mPlatform != nullptr);
    Silabs::BleEvent unifiedEvent;
    VerifyOrReturn(PLATFORM()->ParseEvent(platformEvent, unifiedEvent));
    if (unifiedEvent.type == Silabs::BleEventType::kConnectionClosed)
    {
        const auto & connData = unifiedEvent.data.connectionClosed;

        // Check if this is a CHIPoBLE connection
        if (nullptr != GetConnectionState(connData.connection, false))
        {
            ChipLogProgress(DeviceLayer, "Disconnect Event for CHIPoBLE on handle : %d", connData.connection);

            // Remove from platform interface
            PLATFORM()->RemoveConnection(connData.connection);

            if (RemoveConnection(connData.connection))
            {
                ChipDeviceEvent event;
                event.Type                          = DeviceEventType::kCHIPoBLEConnectionError;
                event.CHIPoBLEConnectionError.ConId = connData.connection;

                // Map platform-specific reason codes using platform interface
                event.CHIPoBLEConnectionError.Reason = PLATFORM()->MapDisconnectReason(connData.reason);

                ChipLogProgress(DeviceLayer, "BLE GATT connection closed (con %u, reason %u)", connData.connection,
                                connData.reason);

                PlatformMgr().PostEventOrDie(&event);

                // Arrange to re-enable connectable advertising in case it was disabled due to the
                // maximum connection limit being reached.
                mFlags.Set(Flags::kRestartAdvertising);
                mFlags.Set(Flags::kFastAdvertisingEnabled);
            }

            PlatformMgr().ScheduleWork(DriveBLEState, 0);
        }
        else
        {
            // Handle non-CHIPoBLE disconnect (platform-specific logic, e.g., side channel)
            PLATFORM()->HandleNonChipoBleDisconnect(platformEvent, connData.connection);
        }
    }
}

void BLEManagerImpl::HandleWriteEvent(void * platformEvent)
{
    VerifyOrReturn(mPlatform != nullptr);
    Silabs::BleEvent unifiedEvent;
    VerifyOrReturn(PLATFORM()->ParseEvent(platformEvent, unifiedEvent));
    VerifyOrReturn(unifiedEvent.type == Silabs::BleEventType::kGattWriteRequest);
    const auto & writeData = unifiedEvent.data.gattWriteRequest;

    // Check if this is a CHIPoBLE connection
    if (GetConnectionState(writeData.connection))
    {
        uint16_t attribute = writeData.characteristic;
        bool do_provision  = chip::DeviceLayer::Silabs::Provision::Manager::GetInstance().IsProvisionRequired();
        ChipLogProgress(DeviceLayer, "Char Write Req, char : %d", attribute);

        // Use platform interface to determine write type
        Silabs::BlePlatformInterface::WriteType writeType =
            PLATFORM()->HandleChipoBleWrite(platformEvent, writeData.connection, attribute);

        if (writeType == Silabs::BlePlatformInterface::WriteType::TX_CCCD)
        {
            // SiWx: TX CCCD writes need to be routed to HandleTXCharCCCDWrite
            HandleTXCharCCCDWrite(platformEvent);
        }
        else if (writeType == Silabs::BlePlatformInterface::WriteType::RX_CHARACTERISTIC ||
                 writeType == Silabs::BlePlatformInterface::WriteType::OTHER_CHIPOBLE)
        {
            if (do_provision)
            {
                chip::DeviceLayer::Silabs::Provision::Channel::Update(attribute);
                chip::DeviceLayer::Silabs::Provision::Manager::GetInstance().Step();
            }
            else
            {
                HandleRXCharWrite(platformEvent);
            }
        }
    }
    else if (PLATFORM()->IsChipoBleCharacteristic(writeData.characteristic))
    {
        // Prevent to write CHIPoBLE Characteristics from other connections
        // This will fail if the characteristic has the WRITE_NO_RESPONSE property
        PLATFORM()->SendWriteResponse(writeData.connection, writeData.characteristic, 0x01);
        return;
    }
    else
    {
        // Handle non-CHIPoBLE write (platform-specific logic, e.g., side channel)
        PLATFORM()->HandleNonChipoBleWrite(platformEvent, writeData.connection, writeData.characteristic);
    }
}

void BLEManagerImpl::HandleTXCharCCCDWrite(void * platformEvent)
{
    VerifyOrReturn(mPlatform != nullptr);
    Silabs::BleEvent unifiedEvent;
    if (!PLATFORM()->ParseEvent(platformEvent, unifiedEvent))
    {
        return;
    }

    CHIP_ERROR err = CHIP_NO_ERROR;

    // Use platform interface to handle TX CCCD write
    Silabs::BlePlatformInterface::TxCccdWriteResult result = PLATFORM()->HandleTxCccdWrite(platformEvent, unifiedEvent);

    if (result.handled)
    {
        BLEConState * bleConnState = GetConnectionState(result.connection);
        VerifyOrReturn(bleConnState != NULL);
        ChipDeviceEvent event;

        ChipLogProgress(DeviceLayer, "CHIPoBLE %s received", result.isIndicationEnabled ? "subscribe" : "unsubscribe");

        // If indications are not already enabled for the connection...
        if (result.isIndicationEnabled && !bleConnState->subscribed)
        {
            bleConnState->subscribed      = true;
            event.Type                    = DeviceEventType::kCHIPoBLESubscribe;
            event.CHIPoBLESubscribe.ConId = result.connection;
            err                           = PlatformMgr().PostEvent(&event);
        }
        else
        {
            bleConnState->subscribed        = false;
            event.Type                      = DeviceEventType::kCHIPoBLEUnsubscribe;
            event.CHIPoBLEUnsubscribe.ConId = result.connection;
            err                             = PlatformMgr().PostEvent(&event);
        }
    }
    else
    {
        // Handle non-CHIPoBLE CCCD write (platform-specific logic, e.g., side channel)
        PLATFORM()->HandleNonChipoBleCccdWrite(platformEvent, unifiedEvent);
    }

    LogErrorOnFailure(err);
}

void BLEManagerImpl::HandleRXCharWrite(void * platformEvent)
{
    VerifyOrReturn(mPlatform != nullptr);
    Silabs::BleEvent unifiedEvent;
    VerifyOrReturn(PLATFORM()->ParseEvent(platformEvent, unifiedEvent));

    CHIP_ERROR err = CHIP_NO_ERROR;

    if (unifiedEvent.type == Silabs::BleEventType::kGattWriteRequest)
    {
        const auto & writeData = unifiedEvent.data.gattWriteRequest;

        // Copy the data to a packet buffer.
        System::PacketBufferHandle buf = System::PacketBufferHandle::NewWithData(writeData.data, writeData.length, 0, 0);
        VerifyOrExit(!buf.IsNull(), err = CHIP_ERROR_NO_MEMORY);

        ChipLogDetail(DeviceLayer, "Write request/command received for CHIPoBLE RX characteristic (con %u, len %u)",
                      writeData.connection, buf->DataLength());

        // Post an event to the CHIP queue to deliver the data into the CHIP stack.
        {
            ChipDeviceEvent event;
            event.Type                        = DeviceEventType::kCHIPoBLEWriteReceived;
            event.CHIPoBLEWriteReceived.ConId = writeData.connection;
            event.CHIPoBLEWriteReceived.Data  = std::move(buf).UnsafeRelease();
            err                               = PlatformMgr().PostEvent(&event);
        }
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HandleRXCharWrite() failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void BLEManagerImpl::HandleTxConfirmationEvent(BLE_CONNECTION_OBJECT conId)
{
    ChipDeviceEvent event;
    uint8_t timerHandle = sInstance.GetTimerHandle(conId, false);

    ChipLogProgress(DeviceLayer, "Tx Confirmation received");

    // stop indication confirmation timer
    if (timerHandle < kMaxConnections)
    {
        ChipLogProgress(DeviceLayer, " stop soft timer");
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
        sl_bt_system_set_lazy_soft_timer(0, 0, timerHandle, false);
#endif
    }

    event.Type                          = DeviceEventType::kCHIPoBLEIndicateConfirm;
    event.CHIPoBLEIndicateConfirm.ConId = conId;
    PlatformMgr().PostEventOrDie(&event);
}

void BLEManagerImpl::HandleSoftTimerEvent(void * platformEvent)
{
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    // EFR32-specific: Soft timer handling
    volatile sl_bt_msg_t * evt = static_cast<volatile sl_bt_msg_t *>(platformEvent);
    VerifyOrReturn(evt != nullptr);

    // BLE Manager starts soft timers with timer handles less than kMaxConnections
    // If we receive a callback for unknown timer handle ignore this.
    if (evt->data.evt_system_soft_timer.handle < kMaxConnections)
    {
        ChipLogProgress(DeviceLayer, "BLEManagerImpl::HandleSoftTimerEvent CHIPOBLE_PROTOCOL_ABORT");
        ChipDeviceEvent event;
        event.Type                                                   = DeviceEventType::kCHIPoBLEConnectionError;
        event.CHIPoBLEConnectionError.ConId                          = mIndConfId[evt->data.evt_system_soft_timer.handle];
        sInstance.mIndConfId[evt->data.evt_system_soft_timer.handle] = kUnusedIndex;
        event.CHIPoBLEConnectionError.Reason                         = BLE_ERROR_CHIPOBLE_PROTOCOL_ABORT;
        PlatformMgr().PostEventOrDie(&event);
    }
#endif // !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
}

bool BLEManagerImpl::RemoveConnection(uint8_t connectionHandle)
{
    BLEConState * bleConnState = GetConnectionState(connectionHandle, true);
    bool status                = false;

    if (bleConnState != NULL)
    {
        memset(bleConnState, 0, sizeof(BLEConState));
        status = true;
    }

    return status;
}

void BLEManagerImpl::AddConnection(uint8_t connectionHandle, uint8_t bondingHandle)
{
    BLEConState * bleConnState = GetConnectionState(connectionHandle, true);

    if (bleConnState != NULL)
    {
        memset(bleConnState, 0, sizeof(BLEConState));
        bleConnState->allocated        = 1;
        bleConnState->connectionHandle = connectionHandle;
        bleConnState->bondingHandle    = bondingHandle;
    }
}

BLEManagerImpl::BLEConState * BLEManagerImpl::GetConnectionState(uint8_t connectionHandle, bool allocate)
{
    uint8_t freeIndex = kMaxConnections;

    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        if (mBleConnections[i].allocated == 1)
        {
            if (mBleConnections[i].connectionHandle == connectionHandle)
            {
                return &mBleConnections[i];
            }
        }

        else if (i < freeIndex)
        {
            freeIndex = i;
        }
    }

    if (allocate)
    {
        if (freeIndex < kMaxConnections)
        {
            memset(&mBleConnections[freeIndex], 0, sizeof(BLEConState));
            mBleConnections[freeIndex].connectionHandle = connectionHandle;
            mBleConnections[freeIndex].allocated        = 1;
            return &mBleConnections[freeIndex];
        }

        ChipLogError(DeviceLayer, "Failed to allocate BLEConState");
    }

    return NULL;
}

#if SLI_SI91X_ENABLE_BLE
void BLEManagerImpl::OnSendIndicationTimeout(System::Layer * aLayer, void * appState)
{
    BLEManagerImpl * bleMgr = static_cast<BLEManagerImpl *>(appState);
    VerifyOrReturn(bleMgr != nullptr);

    // Find the connection that has a pending indication
    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        if (bleMgr->mBleConnections[i].allocated && bleMgr->mBleConnections[i].subscribed)
        {
            ChipDeviceEvent event;
            event.Type                           = DeviceEventType::kCHIPoBLEConnectionError;
            event.CHIPoBLEConnectionError.ConId  = bleMgr->mBleConnections[i].connectionHandle;
            event.CHIPoBLEConnectionError.Reason = BLE_ERROR_CHIPOBLE_PROTOCOL_ABORT;
            PlatformMgr().PostEventOrDie(&event);
            break;
        }
    }
}
#endif // SLI_SI91X_ENABLE_BLE

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
CHIP_ERROR BLEManagerImpl::EncodeAdditionalDataTlv()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    BitFlags<AdditionalDataFields> additionalDataFields;
    AdditionalDataPayloadGeneratorParams additionalDataPayloadParams;

#if CHIP_ENABLE_ROTATING_DEVICE_ID && defined(CHIP_DEVICE_CONFIG_ROTATING_DEVICE_ID_UNIQUE_ID)
    uint8_t rotatingDeviceIdUniqueId[ConfigurationManager::kRotatingDeviceIDUniqueIDLength] = {};
    MutableByteSpan rotatingDeviceIdUniqueIdSpan(rotatingDeviceIdUniqueId);

    err = DeviceLayer::GetDeviceInstanceInfoProvider()->GetRotatingDeviceIdUniqueId(rotatingDeviceIdUniqueIdSpan);

    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to GetRotatingDeviceIdUniqueId"));

    err = ConfigurationMgr().GetLifetimeCounter(additionalDataPayloadParams.rotatingDeviceIdLifetimeCounter);

    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to GetLifetimeCounter"));

    additionalDataPayloadParams.rotatingDeviceIdUniqueId = rotatingDeviceIdUniqueIdSpan;
    additionalDataFields.Set(AdditionalDataFields::RotatingDeviceId);
#endif /* CHIP_ENABLE_ROTATING_DEVICE_ID && defined(CHIP_DEVICE_CONFIG_ROTATING_DEVICE_ID_UNIQUE_ID) */

    err = AdditionalDataPayloadGenerator().generateAdditionalDataPayload(
        additionalDataPayloadParams, sInstance.c3AdditionalDataBufferHandle, additionalDataFields);

    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to generate TLV encoded Additional Data: %" CHIP_ERROR_FORMAT, err.Format());
    }
    return err;
}

void BLEManagerImpl::HandleC3ReadRequest(void * platformEvent)
{
    VerifyOrReturn(mPlatform != nullptr);
    Silabs::BleEvent unifiedEvent;
    VerifyOrReturn(PLATFORM()->ParseEvent(platformEvent, unifiedEvent));
    if (unifiedEvent.type == Silabs::BleEventType::kGattReadRequest)
    {
        const auto & readData = unifiedEvent.data.gattReadRequest;

#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
        // EFR32-specific: Check for C3 characteristic
        if (readData.characteristic == gattdb_CHIPoBLEChar_C3)
#else
        if (PLATFORM()->IsChipoBleCharacteristic(readData.characteristic))
#endif
        {
            ChipLogDetail(DeviceLayer, "Read request received for CHIPoBLEChar_C3");
            ByteSpan dataSpan(sInstance.c3AdditionalDataBufferHandle->Start(),
                              sInstance.c3AdditionalDataBufferHandle->DataLength());
            CHIP_ERROR err = PLATFORM()->SendReadResponse(readData.connection, readData.characteristic, dataSpan);
            if (err != CHIP_NO_ERROR)
            {
                ChipLogDetail(DeviceLayer, "Failed to send read response, err:%" CHIP_ERROR_FORMAT, err.Format());
            }
        }
    }
}
#endif // CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING

uint8_t BLEManagerImpl::GetTimerHandle(uint8_t connectionHandle, bool allocate)
{
    uint8_t freeIndex = kMaxConnections;

    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        if (mIndConfId[i] == connectionHandle)
        {
            return i;
        }
        else if (allocate)
        {
            if (i < freeIndex)
            {
                freeIndex = i;
            }
        }
    }

    if (freeIndex < kMaxConnections)
    {
        mIndConfId[freeIndex] = connectionHandle;
    }
    else
    {
        ChipLogError(DeviceLayer, "Failed to Save Conn Handle for indication");
    }

    return freeIndex;
}

void BLEManagerImpl::BleAdvTimeoutHandler(void * arg)
{
    if (BLEMgrImpl().mFlags.Has(Flags::kFastAdvertisingEnabled))
    {
        ChipLogDetail(DeviceLayer, "bleAdv Timeout : Start slow advertisement");
        BLEMgrImpl().mFlags.Set(Flags::kAdvertising);
        BLEMgr().SetAdvertisingMode(BLEAdvertisingMode::kSlowAdvertising);
#if CHIP_DEVICE_CONFIG_EXT_ADVERTISING
        BLEMgrImpl().mFlags.Clear(Flags::kExtAdvertisingEnabled);
        BLEMgrImpl().StartBleAdvTimeoutTimer(CHIP_DEVICE_CONFIG_BLE_EXT_ADVERTISING_INTERVAL_CHANGE_TIME_MS);
#endif
    }
#if CHIP_DEVICE_CONFIG_EXT_ADVERTISING
    else
    {
        ChipLogDetail(DeviceLayer, "bleAdv Timeout : Start extended advertisement");
        BLEMgrImpl().mFlags.Set(Flags::kAdvertising);
        BLEMgrImpl().mFlags.Set(Flags::kExtAdvertisingEnabled);
        BLEMgr().SetAdvertisingMode(BLEAdvertisingMode::kSlowAdvertising);
    }
#endif
}

void BLEManagerImpl::CancelBleAdvTimeoutTimer(void)
{
    if (osTimerStop(sbleAdvTimeoutTimer) != osOK)
    {
        ChipLogError(DeviceLayer, "Failed to stop BledAdv timeout timer");
    }
}

void BLEManagerImpl::StartBleAdvTimeoutTimer(uint32_t aTimeoutInMs)
{
    if (osTimerStart(sbleAdvTimeoutTimer, pdMS_TO_TICKS(aTimeoutInMs)) != osOK)
    {
        ChipLogError(DeviceLayer, "Failed to start BledAdv timeout timer");
    }
}

void BLEManagerImpl::DriveBLEState(intptr_t arg)
{
    sInstance.DriveBLEState();
}

bool BLEManagerImpl::CanHandleEvent(uint32_t event)
{
    bool canHandle = false;

    VerifyOrReturnValue(mPlatform != nullptr, false);
    canHandle = PLATFORM()->CanHandleEvent(event);

    VerifyOrReturnValue(canHandle == false, true);

#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    if (mBleSideChannel != nullptr)
    {
        // The side channel and the CHIPoBLE service support the same events, but we give the possibility for implementation of the
        // side channel to support more.
        canHandle = canHandle || mBleSideChannel->CanHandleEvent(event);
    }
#endif

    return canHandle;
}

void BLEManagerImpl::ParseEvent(void * platformEvent)
{
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    // EFR32-specific: Parse events from sl_bt_on_event
    volatile sl_bt_msg_t * evt = static_cast<volatile sl_bt_msg_t *>(platformEvent);
    if (evt == nullptr)
    {
        return;
    }

    // As this is running in a separate thread, and we determined this is a matter related event,
    // we need to block CHIP from operating, until the events are handled.
    chip::DeviceLayer::PlatformMgr().LockChipStack();

    switch (SL_BT_MSG_ID(evt->header))
    {
    case sl_bt_evt_system_boot_id: {
        ChipLogProgress(DeviceLayer, "Bluetooth stack booted: v%d.%d.%d-b%d", evt->data.evt_system_boot.major,
                        evt->data.evt_system_boot.minor, evt->data.evt_system_boot.patch, evt->data.evt_system_boot.build);
        HandleBootEvent();

        RAIL_Version_t railVer;
        RAIL_GetVersion(&railVer, true);
        ChipLogProgress(DeviceLayer, "RAIL version:, v%d.%d.%d-b%d", railVer.major, railVer.minor, railVer.rev, railVer.build);
        sl_bt_connection_set_default_parameters(BLE_CONFIG_MIN_INTERVAL, BLE_CONFIG_MAX_INTERVAL, BLE_CONFIG_LATENCY,
                                                BLE_CONFIG_TIMEOUT, BLE_CONFIG_MIN_CE_LENGTH, BLE_CONFIG_MAX_CE_LENGTH);
    }
    break;

    case sl_bt_evt_connection_opened_id: {
        HandleConnectEvent(platformEvent);
    }
    break;
    case sl_bt_evt_connection_parameters_id: {
        ChipLogProgress(DeviceLayer, "Connection parameter ID received - i:%d, l:%d, t:%d, sm:%d",
                        evt->data.evt_connection_parameters.interval, evt->data.evt_connection_parameters.latency,
                        evt->data.evt_connection_parameters.timeout, evt->data.evt_connection_parameters.security_mode);
        HandleConnectParams(platformEvent);
    }
    break;
    case sl_bt_evt_connection_phy_status_id: {
        ChipLogProgress(DeviceLayer, "Connection phy status ID received - phy:%d", evt->data.evt_connection_phy_status.phy);
    }
    break;
    case sl_bt_evt_connection_data_length_id: {
        ChipLogProgress(DeviceLayer, "Connection data length ID received - txL:%d, txT:%d, rxL:%d, rxL:%d",
                        evt->data.evt_connection_data_length.tx_data_len, evt->data.evt_connection_data_length.tx_time_us,
                        evt->data.evt_connection_data_length.rx_data_len, evt->data.evt_connection_data_length.rx_time_us);
    }
    break;
    case sl_bt_evt_connection_closed_id: {
        HandleConnectionCloseEvent(platformEvent);
    }
    break;

    /* This event indicates that a remote GATT client is attempting to write a value of an
     * attribute in to the local GATT database, where the attribute was defined in the GATT
     * XML firmware configuration file to have type="user".  */
    case sl_bt_evt_gatt_server_attribute_value_id: {
        HandleWriteEvent(platformEvent);
    }
    break;

    case sl_bt_evt_gatt_mtu_exchanged_id: {
        UpdateMtu(platformEvent);
    }
    break;

    // confirmation of indication received from remote GATT client
    case sl_bt_evt_gatt_server_characteristic_status_id: {
        sl_bt_gatt_server_characteristic_status_flag_t StatusFlags;

        StatusFlags = (sl_bt_gatt_server_characteristic_status_flag_t) evt->data.evt_gatt_server_characteristic_status.status_flags;

        ChipLogProgress(DeviceLayer, "Characteristic status event: char=%u, flags=0x%02x, client_config=0x%02x",
                        evt->data.evt_gatt_server_characteristic_status.characteristic, StatusFlags,
                        evt->data.evt_gatt_server_characteristic_status.client_config_flags);

        if (sl_bt_gatt_server_confirmation == StatusFlags)
        {
            HandleTxConfirmationEvent(evt->data.evt_gatt_server_characteristic_status.connection);
        }
        else
        {
            HandleTXCharCCCDWrite(platformEvent);
        }
    }
    break;

    /* Software Timer event */
    case sl_bt_evt_system_soft_timer_id: {
        HandleSoftTimerEvent(platformEvent);
    }
    break;

    case sl_bt_evt_gatt_server_user_read_request_id: {
        ChipLogProgress(DeviceLayer, "GATT server user_read_request");
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
        if (evt->data.evt_gatt_server_user_read_request.characteristic == gattdb_CHIPoBLEChar_C3)
        {
            HandleC3ReadRequest(platformEvent);
        }
#else
#if defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
        HandleReadEvent(platformEvent);
#endif // defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
#endif // CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    }
    break;

    case sl_bt_evt_connection_remote_used_features_id: {
        // ChipLogProgress(DeviceLayer, "link layer features supported by the remote device");
    }
    break;

    default: {
        ChipLogProgress(DeviceLayer, "evt_UNKNOWN id = %08" PRIx32, SL_BT_MSG_ID(evt->header));
        break;
    }
    }

    // Unlock the stack
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
#endif // !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
}

namespace Silabs {

BlePlatformInterface * GetBlePlatformInstance()
{
#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    // SiWx917 platform
    return &Silabs::BlePlatformSiWx917::GetInstance();
#else
    // EFR32 platform
    return &BlePlatformEfr32::GetInstance();
#endif
}

} // namespace Silabs
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
#ifdef SL_CATALOG_MATTER_BLE_DMP_TEST_PRESENT
extern "C" void zigbee_bt_on_event(volatile sl_bt_msg_t * evt);
#endif // SL_CATALOG_MATTER_BLE_DMP_TEST_PRESENT

// TODO: Move this to matter_bl_event.cpp and update gn and slc build files
#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
extern "C" void sl_bt_on_event(sl_bt_msg_t * evt)
{
    if (chip::DeviceLayer::Internal::BLEMgrImpl().CanHandleEvent(SL_BT_MSG_ID(evt->header)))
    {
        chip::DeviceLayer::Internal::BLEMgrImpl().ParseEvent(evt);
    }
#ifdef SL_CATALOG_MATTER_BLE_DMP_TEST_PRESENT
    zigbee_bt_on_event(evt);
#endif // SL_CATALOG_MATTER_BLE_DMP_TEST_PRESENT
}
#endif // !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)

#endif // CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
