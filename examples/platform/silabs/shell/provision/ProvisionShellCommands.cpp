/***************************************************************************
 * @file ProvisionShellCommands.cpp
 * @brief Shell command for factory reset with provisioning enabled.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
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

#include "ProvisionShellCommands.h"
#include <lib/shell/Engine.h>
#include <lib/support/CodeUtils.h>

using namespace chip;
using Shell::Engine;
using Shell::streamer_get;
using Shell::streamer_printf;

namespace {

CHIP_ERROR ProvisionCommand(int argc, char ** argv)
{
    if (argc != 0)
    {
        streamer_printf(streamer_get(), "Usage: provision\r\n");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    streamer_printf(streamer_get(), "Factory reset with provisioning enabled\r\n");
    SilabsScheduleFactoryReset(true);
    return CHIP_NO_ERROR;
}

} // namespace

namespace ProvisionShellCommands {

void RegisterCommands()
{
    static const Shell::Command sProvisionCmd = { &ProvisionCommand, "provision",
                                                  "Factory reset with provisioning enabled" };
    Engine::Root().RegisterCommands(&sProvisionCmd, 1);
}

} // namespace ProvisionShellCommands
