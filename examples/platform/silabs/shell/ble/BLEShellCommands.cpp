/***************************************************************************
 * @file BLEShellCommands.cpp
 * @brief Instrumenting to call BLE shell commands for the Silicon Labs platform.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
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

#include "BLEShellCommands.h"
#include <lib/shell/Command.h>
#include <lib/shell/Engine.h>
#include <lib/shell/commands/Help.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>

#include <platform/ConfigurationManager.h>
#include <platform/internal/BLEManager.h>

using namespace chip;
using Shell::Engine;
using Shell::shell_command_t;
using Shell::streamer_get;
using Shell::streamer_printf;

namespace {

Engine sShellBLESubCommands;

/********************************************************
 * Silabs Tracing shell functions
 *********************************************************/

CHIP_ERROR BLEHelpHandler(int argc, char ** argv)
{
    sShellBLESubCommands.ForEachCommand(Shell::PrintCommandHelp, nullptr);
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLECommandHandler(int argc, char ** argv)
{
    if (argc == 0)
    {
        return BLEHelpHandler(argc, argv);
    }

    return sShellBLESubCommands.ExecCommand(argc, argv);
}

CHIP_ERROR StartBLESideChannelAdvertising(int argc, char ** argv)
{
    // TODO : Configure first
    CHIP_ERROR err = DeviceLayer::Internal::BLEMgrImpl().ConfigureSideChannelAdvertisingDefaultData();
    err            = DeviceLayer::Internal::BLEMgrImpl().StartSideChannelAdvertising();
    if (err != CHIP_NO_ERROR)
    {
        streamer_printf(streamer_get(), "Failed to start BLE side channel advertising: %s\n", ErrorStr(err));
        return err;
    }
    streamer_printf(streamer_get(), "Started BLE side channel advertising\n");
    return CHIP_NO_ERROR;
}

CHIP_ERROR StopBLESideChannelAvertising(int argc, char ** argv)
{
    DeviceLayer::Internal::BLEMgrImpl().StopSideChannelAdvertising();
    return CHIP_NO_ERROR;
}

} // namespace

namespace BLEShellCommands {

void RegisterCommands()
{
    static const Shell::Command sBLESubCommands[] = {
        { &BLEHelpHandler, "help", "Output the BLE Commands help menu" },
        { &StartBLESideChannelAdvertising, "AdvStart", "Start BLE Side Channel advertising with default parameters" },
        { &StopBLESideChannelAvertising, "AdvStop", "Stop BLE Side Channel advertising" },
    };
    static const Shell::Command cmds_silabs_ble = { &BLECommandHandler, "ble-side",
                                                    "Dispatch Silicon Labs BLE Side Channel command" };

    sShellBLESubCommands.RegisterCommands(sBLESubCommands, ArraySize(sBLESubCommands));
    Engine::Root().RegisterCommands(&cmds_silabs_ble, 1);
}

} // namespace BLEShellCommands
