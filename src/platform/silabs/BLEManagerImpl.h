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

#pragma once
#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE

#include "AbstractBLEManagerImpl.h"

namespace chip {
namespace DeviceLayer {
namespace Internal {

using namespace chip::Ble;

/**
 * Concrete implementation of the BLEManager singleton object for the Silicon Labs platforms.
 */
class BLEManagerImpl final : public AbstractBLEManagerImpl
{

public:
#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    // Used for posting the event in the BLE queue
    void BlePostEvent(SilabsBleWrapper::BleEvent_t * event);
    void HandleConnectEvent(const SilabsBleWrapper::sl_wfx_msg_t & evt);
    void HandleConnectionCloseEvent(const SilabsBleWrapper::sl_wfx_msg_t & evt);
    void HandleWriteEvent(const SilabsBleWrapper::sl_wfx_msg_t & evt);
    void UpdateMtu(const SilabsBleWrapper::sl_wfx_msg_t & evt);
    void HandleTxConfirmationEvent(BLE_CONNECTION_OBJECT conId);
    void HandleTXCharCCCDWrite(const SilabsBleWrapper::sl_wfx_msg_t & evt);
    void HandleSoftTimerEvent(void);
    int32_t SendBLEAdvertisementCommand(void);
#else
    void HandleConnectEvent(volatile sl_bt_msg_t * evt);
    void HandleConnectParams(volatile sl_bt_msg_t * evt);
    void HandleConnectionCloseEvent(volatile sl_bt_msg_t * evt);
    void HandleWriteEvent(volatile sl_bt_msg_t * evt);
    void UpdateMtu(volatile sl_bt_msg_t * evt);
    void HandleTxConfirmationEvent(BLE_CONNECTION_OBJECT conId);
    void HandleTXCharCCCDWrite(volatile sl_bt_msg_t * evt);
    void HandleSoftTimerEvent(volatile sl_bt_msg_t * evt);
    bool CanHandleEvent(uint32_t event);
    void ParseEvent(volatile sl_bt_msg_t * evt);
#endif // RSI_BLE_ENABLEHandleConnectEvent
    CHIP_ERROR StartAdvertising(void);
    CHIP_ERROR StopAdvertising(void);

#if defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
    void HandleReadEvent(volatile sl_bt_msg_t * evt);

    // Side Channel
    CHIP_ERROR InjectSideChannel(BLEChannel * channel);
    CHIP_ERROR SideChannelConfigureAdvertisingDefaultData(void);
    CHIP_ERROR SideChannelConfigureAdvertising(ByteSpan advData, ByteSpan responseData, uint32_t intervalMin, uint32_t intervalMax,
                                               uint16_t duration, uint8_t maxEvents);
    CHIP_ERROR SideChannelStartAdvertising(void);
    CHIP_ERROR SideChannelStopAdvertising(void);

    // GAP
    CHIP_ERROR SideChannelGeneratAdvertisingData(uint8_t discoverMove, uint8_t connectMode, const Optional<uint16_t> & maxEvents)
    {
        VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
        return mBleSideChannel->GeneratAdvertisingData(discoverMove, connectMode, maxEvents);
    }

    CHIP_ERROR SideChannelOpenConnection(bd_addr address, uint8_t addrType)
    {
        VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
        return mBleSideChannel->OpenConnection(address, addrType);
    }

    CHIP_ERROR SideChannelSetConnectionParams(const Optional<uint8_t> & connectionHandle, uint32_t intervalMin,
                                              uint32_t intervalMax, uint16_t latency, uint16_t timeout)
    {
        VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
        return mBleSideChannel->SetConnectionParams(connectionHandle, intervalMin, intervalMax, latency, timeout);
    }

    CHIP_ERROR SideChannelSetAdvertisingParams(uint32_t intervalMin, uint32_t intervalMax, uint16_t duration,
                                               const Optional<uint16_t> & maxEvents, const Optional<uint8_t> & channelMap)
    {
        VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
        return mBleSideChannel->SetAdvertisingParams(intervalMin, intervalMax, duration, maxEvents, channelMap);
    }

    CHIP_ERROR SideChannelSetAdvertisingHandle(uint8_t handle) { return mBleSideChannel->SetAdvHandle(handle); }
    CHIP_ERROR SideChannelCloseConnection(void) { return mBleSideChannel->CloseConnection(); }

    // GATT (All these methods need some event handling to be done in sl_bt_on_event)
    CHIP_ERROR SideChannelDiscoverServices(void) { return mBleSideChannel->DiscoverServices(); }
    CHIP_ERROR SideChannelDiscoverCharacteristics(uint32_t serviceHandle)
    {
        VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
        return mBleSideChannel->DiscoverCharacteristics(serviceHandle);
    }
    CHIP_ERROR SideChannelSetCharacteristicNotification(uint8_t characteristicHandle, uint8_t flags)
    {
        VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
        return mBleSideChannel->SetCharacteristicNotification(characteristicHandle, flags);
    }
    CHIP_ERROR SideChannelSetCharacteristicValue(uint8_t characteristicHandle, const ByteSpan & value)
    {
        VerifyOrReturnError(mBleSideChannel != nullptr, CHIP_ERROR_INCORRECT_STATE);
        return mBleSideChannel->SetCharacteristicValue(characteristicHandle, value);
    }
    bd_addr SideChannelGetAddr(void) { return mBleSideChannel->GetRandomizedAddr(); }
    BLEConState SideChannelGetConnectionState(void) { return mBleSideChannel->GetConnectionState(); }
    uint8_t SideChannelGetAdvHandle(void) { return mBleSideChannel->GetAdvHandle(); }
    uint8_t SideChannelGetConnHandle(void) { return mBleSideChannel->GetConnectionHandle(); }
#endif // defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    static void HandleC3ReadRequest(const SilabsBleWrapper::sl_wfx_msg_t & rsi_ble_read_req);
#else
    static void HandleC3ReadRequest(volatile sl_bt_msg_t * evt);
#endif
#endif

private:
    friend BLEManager;
    friend BLEManager & BLEMgr(void);
    friend BLEManagerImpl & BLEMgrImpl(void);

#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    osMessageQueueId_t sBleEventQueue = NULL;
    static void sl_ble_event_handling_task(void * args);
    void sl_ble_init();
    void ProcessEvent(SilabsBleWrapper::BleEvent_t inEvent);
#endif

    CHIP_ERROR PlatformInit(void) override;
    void PostDriveBLEState(void) override;
    CHIP_ERROR CloseConnection(BLE_CONNECTION_OBJECT conId) override;
    uint16_t GetMTU(BLE_CONNECTION_OBJECT conId) const override;
    CHIP_ERROR SendIndication(BLE_CONNECTION_OBJECT conId, const Ble::ChipBleUUID * svcId, const Ble::ChipBleUUID * charId,
                              System::PacketBufferHandle pBuf) override;
    void NotifyChipConnectionClosed(BLE_CONNECTION_OBJECT conId) override;

    void DriveBLEState(void);
    static void DriveBLEState(intptr_t arg);
    static void BleAdvTimeoutHandler(void * arg);
    CHIP_ERROR ConfigureAdvertisingData(void);
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    CHIP_ERROR EncodeAdditionalDataTlv();
#endif

#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    void StartBleAdvTimeoutTimer(uint32_t aTimeoutInMs);
    void CancelBleAdvTimeoutTimer(void);
    static void OnSendIndicationTimeout(System::Layer * aLayer, void * appState);
#else
    void StartBleAdvTimeoutTimer(uint32_t aTimeoutInMs);
    void CancelBleAdvTimeoutTimer(void);
#endif

    static BLEManagerImpl sInstance;
};

/**
 * Returns a reference to the public interface of the BLEManager singleton object.
 *
 * Internal components should use this to access features of the BLEManager object
 * that are common to all platforms.
 */
inline BLEManager & BLEMgr(void)
{
    return BLEManagerImpl::sInstance;
}

/**
 * Returns the platform-specific implementation of the BLEManager singleton object.
 *
 * Internal components can use this to gain access to features of the BLEManager
 * that are specific to the EFR32 platforms.
 */
inline BLEManagerImpl & BLEMgrImpl(void)
{
    return BLEManagerImpl::sInstance;
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip

#endif // CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
