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
#include "FreeRTOS.h"
#include "timers.h"
#include <lib/core/Optional.h>
#include <lib/support/BitFlags.h>
#include <platform/internal/BLEManager.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

// Forward declaration avoided to prevent namespace conflicts with DeviceLayer::Silabs
// Using void pointer - cast to BlePlatformInterface* in .cpp where full definition is available

#if !(SLI_SI91X_ENABLE_BLE)
// EFR32-specific side channel support
class BLEChannel;
struct BLEConState;
#endif // !(SLI_SI91X_ENABLE_BLE)

using namespace chip::Ble;

/**
 * Concrete implementation of the BLEManager singleton object for the EFR32 platforms.
 */
class BLEManagerImpl final : public BLEManager, private BleLayer, private BlePlatformDelegate, private BleApplicationDelegate
{

public:
    void HandleBootEvent(void);
    void HandleConnectEvent(void * platformEvent);
    void HandleConnectParams(void * platformEvent);
    void HandleConnectionCloseEvent(void * platformEvent);
    void HandleWriteEvent(void * platformEvent);
    void UpdateMtu(void * platformEvent);
    void HandleTxConfirmationEvent(BLE_CONNECTION_OBJECT conId);
    void HandleTXCharCCCDWrite(void * platformEvent);
    void HandleSoftTimerEvent(void * platformEvent);
    bool CanHandleEvent(uint32_t event);
    void ParseEvent(void * platformEvent);
    CHIP_ERROR StartAdvertising(void);
    CHIP_ERROR StopAdvertising(void);

#if defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
    void HandleReadEvent(void * platformEvent);

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

    // Helper method for platform to handle side channel connections
    bool HandleSideChannelConnection(uint8_t connection, uint8_t bonding)
    {
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
        if (mBleSideChannel != nullptr)
        {
            ChipLogProgress(DeviceLayer, "Connect Event for SideChannel on handle : %d", connection);
            mBleSideChannel->AddConnection(connection, bonding);
            return true;
        }
#endif
        return false;
    }

    // Helper method for platform to handle side channel writes
    bool HandleSideChannelWrite(void * platformEvent)
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
#endif
        return false;
    }

    // Helper method for platform to handle side channel reads
    bool HandleSideChannelRead(void * platformEvent, uint8_t connection, uint16_t characteristic)
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
#endif
        return false;
    }

    // Helper method for platform to handle side channel MTU updates
    bool HandleSideChannelMtuUpdate(void * platformEvent, uint8_t connection)
    {
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
        if (mBleSideChannel != nullptr)
        {
            mBleSideChannel->UpdateMtu(static_cast<volatile sl_bt_msg_t *>(platformEvent));
            return true;
        }
#endif
        return false;
    }

    // Helper method for platform to handle side channel disconnects
    bool HandleSideChannelDisconnect(uint8_t connection)
    {
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
        if (mBleSideChannel != nullptr)
        {
            ChipLogProgress(DeviceLayer, "Disconnect Event for the Side Channel on handle : %d", connection);
            mBleSideChannel->RemoveConnection(connection);
            return true;
        }
#endif
        return false;
    }

    // Helper method for platform to handle side channel CCCD writes
    CHIP_ERROR HandleSideChannelCccdWrite(void * platformEvent, bool & isNewSubscription)
    {
#if !(SLI_SI91X_ENABLE_BLE) && defined(SL_BLE_SIDE_CHANNEL_ENABLED) && SL_BLE_SIDE_CHANNEL_ENABLED
        if (mBleSideChannel != nullptr)
        {
            return mBleSideChannel->HandleCCCDWriteRequest(static_cast<volatile sl_bt_msg_t *>(platformEvent), isNewSubscription);
        }
#endif
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    static void HandleC3ReadRequest(void * platformEvent);
#endif

private:
    // Allow the BLEManager interface class to delegate method calls to
    // the implementation methods provided by this class.
    friend BLEManager;

    // ===== Members that implement the BLEManager internal interface.

    CHIP_ERROR _Init(void);
    void _Shutdown() {}
    bool _IsAdvertisingEnabled(void);
    CHIP_ERROR _SetAdvertisingEnabled(bool val);
    bool _IsAdvertising(void);
    CHIP_ERROR _SetAdvertisingMode(BLEAdvertisingMode mode);
    CHIP_ERROR _GetDeviceName(char * buf, size_t bufSize);
    CHIP_ERROR _SetDeviceName(const char * deviceName);
    uint16_t _NumConnections(void);
    void _OnPlatformEvent(const ChipDeviceEvent * event);
    BleLayer * _GetBleLayer(void);

    // ===== Members that implement virtual methods on BlePlatformDelegate.

    CHIP_ERROR SubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const Ble::ChipBleUUID * svcId,
                                       const Ble::ChipBleUUID * charId) override;
    CHIP_ERROR UnsubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const Ble::ChipBleUUID * svcId,
                                         const Ble::ChipBleUUID * charId) override;
    CHIP_ERROR CloseConnection(BLE_CONNECTION_OBJECT conId) override;
    uint16_t GetMTU(BLE_CONNECTION_OBJECT conId) const override;
    CHIP_ERROR SendIndication(BLE_CONNECTION_OBJECT conId, const Ble::ChipBleUUID * svcId, const Ble::ChipBleUUID * charId,
                              System::PacketBufferHandle pBuf) override;
    CHIP_ERROR SendWriteRequest(BLE_CONNECTION_OBJECT conId, const Ble::ChipBleUUID * svcId, const Ble::ChipBleUUID * charId,
                                System::PacketBufferHandle pBuf) override;

    // ===== Members that implement virtual methods on BleApplicationDelegate.

    void NotifyChipConnectionClosed(BLE_CONNECTION_OBJECT conId) override;

    // ===== Members for internal use by the following friends.

    friend BLEManager & BLEMgr(void);
    friend BLEManagerImpl & BLEMgrImpl(void);

    static BLEManagerImpl sInstance;

    // ===== Private members reserved for use by this class only.
    enum class Flags : uint16_t
    {
        kAdvertisingEnabled       = 0x0001,
        kFastAdvertisingEnabled   = 0x0002,
        kAdvertising              = 0x0004,
        kRestartAdvertising       = 0x0008,
        kSiLabsBLEStackInitialize = 0x0010,
        kDeviceNameSet            = 0x0020,
        kExtAdvertisingEnabled    = 0x0040,
    };

    static constexpr uint8_t kMaxConnections      = BLE_LAYER_NUM_BLE_ENDPOINTS;
    static constexpr uint8_t kMaxDeviceNameLength = 21;
    static constexpr uint8_t kUnusedIndex         = 0xFF;

    static constexpr uint8_t kFlagTlvSize       = 3; // 1 byte for length, 1b for type and 1b for the Flag value
    static constexpr uint8_t kUUIDTlvSize       = 4; // 1 byte for length, 1b for type and 2b for the UUID value
    static constexpr uint8_t kDeviceNameTlvSize = (2 + kMaxDeviceNameLength); // 1 byte for length, 1b for type and + device name

    // Unified connection state structure
    struct BLEConState
    {
        uint16_t mtu : 10;
        uint16_t allocated : 1;
        uint16_t subscribed : 1;
        uint16_t unused : 4;
        uint8_t connectionHandle;
        uint8_t bondingHandle;
    };

    BLEConState mBleConnections[kMaxConnections];
    void * mPlatform = nullptr; // BlePlatformInterface* - cast in .cpp to avoid namespace conflicts
    uint8_t mIndConfId[kMaxConnections];
    CHIPoBLEServiceMode mServiceMode;
    BitFlags<Flags> mFlags;
    char mDeviceName[kMaxDeviceNameLength + 1];
    // The advertising set handle allocated from Bluetooth stack.
    uint8_t mAdvertisingSetHandle = 0xff;
#if SLI_SI91X_ENABLE_BLE
    // Track if an indication is pending for SiWx (to avoid cancelling timer on spurious confirmations)
    bool mIndicationInFlight = false;
#endif
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    PacketBufferHandle c3AdditionalDataBufferHandle;
#endif

#if !(SLI_SI91X_ENABLE_BLE)
    BLEChannel * mBleSideChannel = nullptr;
#endif // !(SLI_SI91X_ENABLE_BLE)

    CHIP_ERROR MapBLEError(int bleErr);
    void DriveBLEState(void);
    CHIP_ERROR ConfigureAdvertisingData(void);
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    CHIP_ERROR EncodeAdditionalDataTlv();
#endif

    void HandleRXCharWrite(void * platformEvent);
    bool RemoveConnection(uint8_t connectionHandle);
    void AddConnection(uint8_t connectionHandle, uint8_t bondingHandle);
    void StartBleAdvTimeoutTimer(uint32_t aTimeoutInMs);
    void CancelBleAdvTimeoutTimer(void);
    BLEConState * GetConnectionState(uint8_t conId, bool allocate = false);
    static void DriveBLEState(intptr_t arg);
    static void BleAdvTimeoutHandler(void * arg);
    uint8_t GetTimerHandle(uint8_t connectionHandle, bool allocate);

#if SLI_SI91X_ENABLE_BLE
protected:
    static void OnSendIndicationTimeout(System::Layer * aLayer, void * appState);
#endif
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

inline BleLayer * BLEManagerImpl::_GetBleLayer()
{
    return this;
}

inline bool BLEManagerImpl::_IsAdvertisingEnabled(void)
{
    return mFlags.Has(Flags::kAdvertisingEnabled);
}

inline bool BLEManagerImpl::_IsAdvertising(void)
{
    return mFlags.Has(Flags::kAdvertising);
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip

#endif // CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
