#pragma once

#include "sl_bt_api.h"
#include "sl_bt_rtos_adaptation.h"
#include "sl_bt_stack_config.h"
#include "sl_bt_stack_init.h"
#include <lib/core/CHIPError.h>
#include <lib/core/Optional.h>
#include <lib/support/BitFlags.h>
#include <lib/support/Span.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

struct BLEConState
{
    uint16_t mtu : 10;
    uint16_t allocated : 1;
    uint16_t subscribed : 1;
    uint16_t unused : 4;
    uint8_t connectionHandle;
    uint8_t bondingHandle;
};

class BLEChannel
{
public:
    BLEChannel();
    virtual ~BLEChannel() = default;

    CHIP_ERROR Init(void);

    // TODO: Revisit this method and see about splitting timing vs advertising data. It is currently implemented to do everything
    // the Matter BLEManager requires but it should be modified to use existing APIs for the advertising data and timing
    CHIP_ERROR ConfigureAdvertising(ByteSpan advData, ByteSpan responseData, uint32_t intervalMin, uint32_t intervalMax,
                                    uint16_t duration, uint8_t maxEvents);

    /** @brief StartAdvertising
     *  Start the advertising process for the BLE channel using configured parameters. ConfigureAdvertising must be called before
     * this function.
     */
    CHIP_ERROR StartAdvertising(void);
    CHIP_ERROR StopAdvertising(void);

    virtual void AddConnection(uint8_t connectionHandle, uint8_t bondingHandle);
    virtual bool RemoveConnection(uint8_t connectionHandle);
    virtual void HandleReadRequest(volatile sl_bt_msg_t * evt, ByteSpan data);
    virtual void HandleWriteRequest(volatile sl_bt_msg_t * evt, MutableByteSpan data);

    /** @brief CCCD Write Handler
     *
     *  This method handles the CCCD write request from the client. It sets the subscribed flag
     *  in the connection state and logs the subscription status. It returns a boolean indicating
     *  whether the request requires a new subscription.
     *
     * @param evt The event containing the CCCD write request.
     * @param isNewSubscription A boolean indicating whether the request requires a new subscription.
     *
     *
     * @return CHIP_ERROR_INCORRECT_STATE if a request is received when the connection is not allocated or a request is received
     *        for a different connection handle. CHIP_NO_ERROR if the request is handled successfully.
     */
    virtual CHIP_ERROR HandleCCCDWriteRequest(volatile sl_bt_msg_t * evt, bool & isNewSubscription);

    virtual void UpdateMtu(volatile sl_bt_msg_t * evt);

    // CLI methods BEGIN
    // GAP
    virtual CHIP_ERROR GeneratAdvertisingData(uint8_t discoverMove, uint8_t connectMode, const Optional<uint16_t> & maxEvents);
    virtual CHIP_ERROR SetAdvertisingParams(uint32_t intervalMin, uint32_t intervalMax, uint16_t duration,
                                            const Optional<uint16_t> & maxEvents, const Optional<uint8_t> & channelMap);
    virtual CHIP_ERROR OpenConnection(bd_addr address, uint8_t addrType);

    /** @brief SetConnectionParams
     *
     *  If a connectionHandle is provided, this connection parameters will be updated, otherwise, the default connection
     *  parameters (in the bt stack) will be updated to the provided values
     *
     * @param connectionHandle Optional connection handle to set the parameters for.
     * @param intervalMin Minimum connection interval, caculate as Time = intervalMin x 1.25 ms = 30ms
     * @param intervalMax Maximum connection interval, caculate as Time = intervalMin x 1.25 ms = 30ms
     * @param latency Peripheral latency: how many connection intervals the peripheral can skip if it has no data to send.
     * @param timeout Supervision timeout: time that the connection is maintained although the devices can't communicate
     *
     * @return CHIP_NO_ERROR if the parameters are set successfully, sl_status_t mapped to from ble stack error to CHIP_ERROR
     * otherwise.
     */
    virtual CHIP_ERROR SetConnectionParams(const Optional<uint8_t> & connectionHandle, uint32_t intervalMin, uint32_t intervalMax,
                                           uint16_t latency, uint16_t timeout);
    virtual CHIP_ERROR CloseConnection(void);

    /** @brief Set Advertising Handle
     *
     *  Our side channel currently only supports one advertising handle. This method checks if the
     *  current mAdvertisingHandle is 0xff (uninitialized) and sets it to the provided handle and
     *  create the advertising set if it is the case.
     *  If the handle is already set, it will stop the ongoing advertising, delete the set and
     *  create a new one with the provided handle.
     *
     *  TODO: Have the BLEManagerImpl set the max advertising handle by deriving it from
     *  SL_BT_CONFIG_USER_ADVERTISERS and the number of channels it has to manage.
     *
     *  @param handle The advertising handle to set as the channel's advertising handle.
     *  @return CHIP_NO_ERROR if the handle is set successfully, otherwise an error code.
     */
    virtual CHIP_ERROR SetAdvHandle(uint8_t handle);

    // GATT (All these methods need some event handling to be done in sl_bt_on_event)
    virtual CHIP_ERROR DiscoverServices();
    virtual CHIP_ERROR DiscoverCharacteristics(uint32_t serviceHandle);
    virtual CHIP_ERROR SetCharacteristicNotification(uint8_t characteristicHandle, uint8_t flags);
    virtual CHIP_ERROR SetCharacteristicValue(uint8_t characteristicHandle, const ByteSpan & value);
    // CLI methods END

    // Getters
    uint8_t GetAdvHandle(void) const { return mAdvHandle; }
    uint8_t GetConnectionHandle(void) const { return mConnectionState.connectionHandle; }
    uint8_t GetBondingHandle(void) const { return mConnectionState.bondingHandle; }
    bd_addr GetRandomizedAddr(void) const { return mRandomizedAddr; }
    BLEConState GetConnectionState() const { return mConnectionState; }

private:
    enum class Flags : uint16_t
    {
        kAdvertising = 0x0001, // Todo : See about flags for connection, subscription, etc.
    };

    BLEConState mConnectionState;
    BitFlags<Flags> mFlags;

    bd_addr mRandomizedAddr = { 0 };

    // Advertising parameters
    // TODO: Default values should be set in a configuration file for the side channel
    uint8_t mAdvHandle           = 0xff;
    uint32_t mAdvIntervalMin     = 0;
    uint32_t mAdvIntervalMax     = 0;
    uint16_t mAdvDuration        = 0;
    uint8_t mAdvMaxEvents        = 0;
    uint8_t mAdvConnectableMode  = 0;
    uint8_t mAdvDiscoverableMode = 0;

    uint16_t mSideServiceHandle = 0;
    uint16_t mSideRxCharHandle  = 0;
    uint16_t mSideTxCharHandle  = 0;
};

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
