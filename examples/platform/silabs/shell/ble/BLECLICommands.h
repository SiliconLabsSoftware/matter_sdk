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

#include "sl_bt_api.h"
#include "sl_cli.h"
#include <lib/core/CHIPError.h>
#include <lib/core/Optional.h>

void HandleHello(sl_cli_command_arg_t * arguments);
void HandleGetAddress(sl_cli_command_arg_t * arguments);
void HandlePrintConnections(sl_cli_command_arg_t * arguments);
void HandleGapStopAdvertising(sl_cli_command_arg_t * arguments);
void HandleGapSetMode(sl_cli_command_arg_t * arguments);
void HandleGapSetBt5Mode(sl_cli_command_arg_t * arguments);
void HandleGapConnOpen(sl_cli_command_arg_t * arguments);
void HandleGapSetAdvParams(sl_cli_command_arg_t * arguments);
void HandleGapSetConnParams(sl_cli_command_arg_t * arguments);
void HandleGapUpdateConnParams(sl_cli_command_arg_t * arguments);
void HandleGattDiscoverPrimaryServices(sl_cli_command_arg_t * arguments);
void HandleGattDiscoverCharacteristics(sl_cli_command_arg_t * arguments);
void HandleGattSetCharacteristicNotification(sl_cli_command_arg_t * arguments);
void HandleGattWriteCharacteristic(sl_cli_command_arg_t * arguments);
void HandleGattClose(sl_cli_command_arg_t * arguments);
