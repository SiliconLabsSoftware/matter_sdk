#pragma once

#include "sl_bt_api.h"
#include "sl_bt_rtos_adaptation.h"
#include "sl_bt_stack_config.h"
#include "sl_bt_stack_init.h"
#include <lib/core/CHIPError.h>
#include <lib/support/BitFlags.h>
#include <lib/support/Span.h>

namespace chip {
namespace DeviceLayer {
namespace Internal {

class BLEChannel
{
public:
    // Dynamic Gatt database seems appropriate here
    BLEChannel();
    virtual ~BLEChannel() = default;

    CHIP_ERROR Init(void);

    CHIP_ERROR ConfigureAdvertising(ByteSpan advData, ByteSpan responseData, uint32_t intervalMin, uint32_t intervalMax,
                                    uint16_t duration, uint8_t maxEvents);
    CHIP_ERROR StartAdvertising(void);
    CHIP_ERROR StopAdvertising(void);
    virtual void AddConnection(uint8_t connectionHandle, uint8_t bondingHandle);
    virtual bool RemoveConnection(uint8_t connectionHandle);
    virtual void HandleReadRequest(volatile sl_bt_msg_t * evt, ByteSpan data);
    virtual void HandleWriteRequest(volatile sl_bt_msg_t * evt, MutableByteSpan data);

    /** @brief CCCD Write Handler
     *
     *  This function handles the CCCD write request from the client. It sets the subscribed flag
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

    // Getters and setters for handles
    uint8_t GetAdvHandle() const { return mAdvHandle; }
    void SetAdvHandle(uint8_t handle) { mAdvHandle = handle; }

private:
    struct BLEConState
    {
        uint16_t mtu : 10;
        uint16_t allocated : 1;
        uint16_t subscribed : 1;
        uint16_t unused : 4;
        uint8_t connectionHandle;
        uint8_t bondingHandle;
    };

    enum class Flags : uint16_t
    {
        kAdvertising = 0x0001, // Todo : See about flags for connection, subscription, etc.
    };

    BLEConState mConnectionState;
    BitFlags<Flags> mFlags;

    bd_addr mRandomizedAddr = { 0 };

    // Advertising parameters
    // TODO: Default values should be set in a configuration file for the side channel
    uint8_t mAdvHandle       = 0xff;
    uint32_t mAdvIntervalMin = 0;
    uint32_t mAdvIntervalMax = 0;
    uint16_t mAdvDuration    = 0;
    uint8_t mAdvMaxEvents    = 0;

    uint16_t mSideServiceHandle = 0;
    uint16_t mSideRxCharHandle  = 0;
    uint16_t mSideTxCharHandle  = 0;
};

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
