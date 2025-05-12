/***************************************************************************
 * @file BLEShellCommands.cpp
 * @brief Instrumenting to call BLE shell commands for the Silicon Labs platform.
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "BLECLICommands.h"
#include <lib/shell/Command.h>
#include <lib/shell/Engine.h>
#include <lib/support/CodeUtils.h>
#include <platform/ConfigurationManager.h>
#include <platform/internal/BLEManager.h>

using namespace chip;
using DeviceLayer::Internal::BLEConState;
using DeviceLayer::Internal::BLEMgrImpl;
using Shell::streamer_get;
using Shell::streamer_printf;

namespace {
void printConnections(const BLEConState * conState)
{
    if (conState->allocated)
    {
        streamer_printf(streamer_get(), "Connection handle: %d\n", conState->connectionHandle);
        streamer_printf(streamer_get(), "Bonding handle: %d\n", conState->bondingHandle);
        streamer_printf(streamer_get(), "MTU: %d\n", conState->mtu);
        streamer_printf(streamer_get(), "Subscribed: %s\n", conState->subscribed ? "true" : "false");
    }
    else
    {
        streamer_printf(streamer_get(), ("No active connections.\n"));
    }
}

// Copy bytes from source to destination, reversing the order.
void reverse_mem_copy(uint8_t * dest, const uint8_t * src, uint16_t length)
{
    uint8_t i;
    uint8_t j = (length - 1);

    for (i = 0; i < length; i++)
    {
        dest[i] = src[j];
        j--;
    }
}

CHIP_ERROR SetAdvertisingHandle(uint8_t advHandle)
{
    CHIP_ERROR err = BLEMgrImpl().SideChannelSetAdvertisingHandle(advHandle);
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Set advertising handle: %s\n", ErrorStr(err));
    }
    return err;
}

} // namespace

void HandleHello(sl_cli_command_arg_t * arguments)
{
    sl_status_t status = sl_bt_system_hello();
    streamer_printf(streamer_get(), "BLE hello: %s", (status == SL_STATUS_OK) ? "success" : "error");
}

void HandleGetAddress(sl_cli_command_arg_t * arguments)
{
    bd_addr ble_address = BLEMgrImpl().SideChannelGetAddr();
    streamer_printf(streamer_get(), "BLE address: [%02X:%02X:%02X:%02X:%02X:%02X]\n", ble_address.addr[5], ble_address.addr[4],
                    ble_address.addr[3], ble_address.addr[2], ble_address.addr[1], ble_address.addr[0]);
}

void HandlePrintConnections(sl_cli_command_arg_t * arguments)
{
    (void) arguments;
    BLEConState connState = BLEMgrImpl().SideChannelGetConnectionState();
    printConnections(&connState);
}

void HandleGapStopAdvertising(sl_cli_command_arg_t * arguments)
{
    uint8_t advHandle = sl_cli_get_argument_uint8(arguments, 0);

    ReturnOnFailure(SetAdvertisingHandle(advHandle));

    CHIP_ERROR err = BLEMgrImpl().SideChannelStopAdvertising();
    if (BLEMgrImpl().SideChannelStopAdvertising() != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to stop BLE side channel advertising: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Stopped BLE side channel advertising\n");
    }
}

void HandleGapSetMode(sl_cli_command_arg_t * arguments)
{
    uint8_t advHandle        = sl_cli_get_argument_uint8(arguments, 0);
    uint8_t discoverableMode = sl_cli_get_argument_uint8(arguments, 1);
    uint8_t connectableMode  = sl_cli_get_argument_uint8(arguments, 2);

    ReturnOnFailure(SetAdvertisingHandle(advHandle));

    CHIP_ERROR err = BLEMgrImpl().SideChannelGenerateAdvertisingData(discoverableMode, connectableMode, Optional<uint16_t>());
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to generate BLE advertising data: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Generated BLE advertising data\n");
    }
}

void HandleGapSetBt5Mode(sl_cli_command_arg_t * arguments)
{
    uint8_t advHandle        = sl_cli_get_argument_uint8(arguments, 0);
    uint8_t discoverableMode = sl_cli_get_argument_uint8(arguments, 1);
    uint8_t connectableMode  = sl_cli_get_argument_uint8(arguments, 2);
    uint16_t maxEvents       = sl_cli_get_argument_uint16(arguments, 3);

    ReturnOnFailure(SetAdvertisingHandle(advHandle));

    CHIP_ERROR err = BLEMgrImpl().SideChannelGenerateAdvertisingData(discoverableMode, connectableMode, MakeOptional(maxEvents));
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to generate BLE advertising data: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Generated BLE advertising data\n");
    }
}

void HandleGapConnOpen(sl_cli_command_arg_t * arguments)
{
    uint8_t addressType = sl_cli_get_argument_uint8(arguments, 1);
    bd_addr address;
    size_t _address_len;
    uint8_t * _address = (uint8_t *) &address;
    _address           = sl_cli_get_argument_hex(arguments, 0, &_address_len);
    reverse_mem_copy((uint8_t *) &address, _address, sizeof(address));

    CHIP_ERROR err = BLEMgrImpl().SideChannelOpenConnection(address, addressType);
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to open BLE connection: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Opened BLE connection\n");
    }
}

void HandleGapSetAdvParams(sl_cli_command_arg_t * arguments)
{
    uint8_t advHandle    = sl_cli_get_argument_uint8(arguments, 0);
    uint16_t minInterval = sl_cli_get_argument_uint16(arguments, 1);
    uint16_t maxInterval = sl_cli_get_argument_uint16(arguments, 2);
    uint8_t channelMap   = sl_cli_get_argument_uint8(arguments, 3);

    ReturnOnFailure(SetAdvertisingHandle(advHandle));

    CHIP_ERROR err = BLEMgrImpl().SideChannelSetAdvertisingParams(minInterval, maxInterval, 0, Optional<uint16_t>(),
                                                                  Optional<uint8_t>(channelMap));

    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to set BLE advertising parameters: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Set BLE advertising parameters\n");
    }
}

void HandleGapSetConnParams(sl_cli_command_arg_t * arguments)
{
    uint16_t minInterval        = sl_cli_get_argument_uint16(arguments, 0);
    uint16_t maxInterval        = sl_cli_get_argument_uint16(arguments, 1);
    uint16_t slaveLatency       = sl_cli_get_argument_uint16(arguments, 2);
    uint16_t supervisionTimeout = sl_cli_get_argument_uint16(arguments, 3);

    CHIP_ERROR err = BLEMgrImpl().SideChannelSetConnectionParams(Optional<uint8_t>(), minInterval, maxInterval, slaveLatency,
                                                                 supervisionTimeout);
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to set BLE connection parameters: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Set BLE connection parameters\n");
    }
}

void HandleGapUpdateConnParams(sl_cli_command_arg_t * arguments)
{
    uint16_t connectionHandle   = sl_cli_get_argument_uint16(arguments, 0);
    uint16_t minInterval        = sl_cli_get_argument_uint16(arguments, 1);
    uint16_t maxInterval        = sl_cli_get_argument_uint16(arguments, 2);
    uint16_t slaveLatency       = sl_cli_get_argument_uint16(arguments, 3);
    uint16_t supervisionTimeout = sl_cli_get_argument_uint16(arguments, 4);

    CHIP_ERROR err = BLEMgrImpl().SideChannelSetConnectionParams(MakeOptional(connectionHandle), minInterval, maxInterval,
                                                                 slaveLatency, supervisionTimeout);
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to update BLE connection parameters: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Updated BLE connection parameters\n");
    }
}

void HandleGattDiscoverPrimaryServices(sl_cli_command_arg_t * arguments)
{
    uint8_t connectionHandle = sl_cli_get_argument_uint8(arguments, 0);
    VerifyOrReturn(connectionHandle == BLEMgrImpl().SideChannelGetConnHandle(),
                   streamer_printf(streamer_get(), "Invalid connection handle: %d\n", connectionHandle));

    CHIP_ERROR err = BLEMgrImpl().SideChannelDiscoverServices();
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to discover primary services: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Discovered primary services\n");
    }
}

void HandleGattDiscoverCharacteristics(sl_cli_command_arg_t * arguments)
{
    uint8_t connectionHandle = sl_cli_get_argument_uint8(arguments, 0);
    uint32_t serviceHandle   = sl_cli_get_argument_uint32(arguments, 1);

    VerifyOrReturn(connectionHandle == BLEMgrImpl().SideChannelGetConnHandle(),
                   streamer_printf(streamer_get(), "Invalid connection handle: %d\n", connectionHandle));

    CHIP_ERROR err = BLEMgrImpl().SideChannelDiscoverCharacteristics(serviceHandle);
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to discover characteristics: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Discovered characteristics\n");
    }
}

void HandleGattSetCharacteristicNotification(sl_cli_command_arg_t * arguments)
{
    uint8_t connectionHandle = sl_cli_get_argument_uint8(arguments, 0);
    uint16_t charHandle      = sl_cli_get_argument_uint16(arguments, 1);
    uint8_t flags            = sl_cli_get_argument_uint8(arguments, 2);

    VerifyOrReturn(connectionHandle == BLEMgrImpl().SideChannelGetConnHandle(),
                   streamer_printf(streamer_get(), "Invalid connection handle: %d\n", connectionHandle));

    CHIP_ERROR err = BLEMgrImpl().SideChannelSetCharacteristicNotification(charHandle, flags);
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to set characteristic notification: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Set characteristic notification\n");
    }
}

void HandleGattWriteCharacteristic(sl_cli_command_arg_t * arguments)
{
    size_t valueLen;
    uint8_t connectionHandle = sl_cli_get_argument_uint8(arguments, 0);
    uint16_t characteristic  = sl_cli_get_argument_uint16(arguments, 1);
    uint8_t * value          = sl_cli_get_argument_hex(arguments, 2, (size_t *) &valueLen);

    VerifyOrReturn(connectionHandle == BLEMgrImpl().SideChannelGetConnHandle(),
                   streamer_printf(streamer_get(), "Invalid connection handle: %d\n", connectionHandle));

    CHIP_ERROR err = BLEMgrImpl().SideChannelSetCharacteristicValue(characteristic, ByteSpan(value, valueLen));
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to write characteristic value: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Wrote characteristic value\n");
    }
}

void HandleGattClose(sl_cli_command_arg_t * arguments)
{
    uint8_t connectionHandle = sl_cli_get_argument_uint8(arguments, 0);

    VerifyOrReturn(connectionHandle == BLEMgrImpl().SideChannelGetConnHandle(),
                   streamer_printf(streamer_get(), "Invalid connection handle: %d\n", connectionHandle));
    CHIP_ERROR err = BLEMgrImpl().SideChannelCloseConnection();
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to close BLE connection: %s\n", ErrorStr(err));
    }
    else
    {
        streamer_printf(streamer_get(), "Closed BLE connection\n");
    }
}
