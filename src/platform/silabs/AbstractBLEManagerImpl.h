#pragma once

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE

#include <lib/support/BitFlags.h>
#include <system/SystemPacketBuffer.h>

#include <platform/internal/BLEManager.h>

#include "FreeRTOS.h"
#include "timers.h"

#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
#include "wfx_sl_ble_init.h"
#else
#include "gatt_db.h"
#include "sl_bgapi.h"
#include "sl_bt_api.h"
#include <BLEChannel.h>
#include <lib/core/Optional.h>
#endif // (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)

namespace chip {
namespace DeviceLayer {
namespace Internal {

using namespace chip::Ble;

struct ChipDeviceEvent;

class AbstractBLEManagerImpl : public BLEManager,
                               private BleLayer,
                               private BlePlatformDelegate,
                               private BleApplicationDelegate
{
public:
    virtual ~AbstractBLEManagerImpl() = default;

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
    void HandleBootEvent(void);

    CHIP_ERROR SubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const Ble::ChipBleUUID * svcId,
                                       const Ble::ChipBleUUID * charId) override;
    CHIP_ERROR UnsubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const Ble::ChipBleUUID * svcId,
                                         const Ble::ChipBleUUID * charId) override;
    CHIP_ERROR SendWriteRequest(BLE_CONNECTION_OBJECT conId, const Ble::ChipBleUUID * svcId,
                                const Ble::ChipBleUUID * charId, System::PacketBufferHandle pBuf) override;

protected:
    AbstractBLEManagerImpl();

    virtual CHIP_ERROR PlatformInit(void)                               = 0;
    virtual void PostDriveBLEState(void)                                = 0;
    CHIP_ERROR MapBLEError(int bleErr);
    bool RemoveConnection(uint8_t connectionHandle);
    void AddConnection(uint8_t connectionHandle, uint8_t bondingHandle);
    BLEConState * GetConnectionState(uint8_t conId, bool allocate = false);
    uint8_t GetTimerHandle(uint8_t connectionHandle, bool allocate);

protected:
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

    enum
    {
        kMaxConnections      = BLE_LAYER_NUM_BLE_ENDPOINTS,
        kMaxDeviceNameLength = 21,
        kUnusedIndex         = 0xFF,
    };

    static constexpr uint8_t kFlagTlvSize       = 3;
    static constexpr uint8_t kUUIDTlvSize       = 4;
    static constexpr uint8_t kDeviceNameTlvSize = (2 + kMaxDeviceNameLength);

#if (SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    struct BLEConState
    {
        uint16_t mtu : 10;
        uint16_t allocated : 1;
        uint16_t subscribed : 1;
        uint16_t unused : 4;
        uint8_t connectionHandle;
        uint8_t bondingHandle;
    };
#endif

    BLEConState mBleConnections[kMaxConnections];
    uint8_t mIndConfId[kMaxConnections];
    CHIPoBLEServiceMode mServiceMode;
    BitFlags<Flags> mFlags;
    char mDeviceName[kMaxDeviceNameLength + 1];
    uint8_t mAdvertisingSetHandle = 0xff;
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    PacketBufferHandle c3AdditionalDataBufferHandle;
#endif

#if !(SLI_SI91X_ENABLE_BLE || RSI_BLE_ENABLE)
    BLEChannel * mBleSideChannel = nullptr;
#endif
};

inline AbstractBLEManagerImpl::AbstractBLEManagerImpl() = default;

inline BleLayer * AbstractBLEManagerImpl::_GetBleLayer()
{
    return this;
}

inline bool AbstractBLEManagerImpl::_IsAdvertisingEnabled(void)
{
    return mFlags.Has(Flags::kAdvertisingEnabled);
}

inline bool AbstractBLEManagerImpl::_IsAdvertising(void)
{
    return mFlags.Has(Flags::kAdvertising);
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip

#endif // CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE

