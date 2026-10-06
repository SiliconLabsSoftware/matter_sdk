/***************************************************************************
 * @file ProvisionShellCommands.h
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
#pragma once

// Implemented in BaseApplication.cpp. Kept out of BaseApplication.h so this
// shell target does not pull in zap-generated app headers.
void SilabsScheduleFactoryReset(bool requestProvisioning);

namespace ProvisionShellCommands {

void RegisterCommands();

} // namespace ProvisionShellCommands
