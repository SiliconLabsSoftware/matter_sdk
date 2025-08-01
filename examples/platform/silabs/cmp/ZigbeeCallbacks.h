/*****************************************************************************
 * @file ZigbeeCallbacks.h
 * @brief Callbacks implementation and application specific code.
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

#pragma once

// Small wrapper for Zigbee critical api
namespace Zigbee {
void RequestStart(uint8_t channel = 0);
void RequestLeave(void);
uint8_t GetZigbeeChannel(void);
void ZLLNotFactoryNew(void);
void TokenFactoryReset(void);
}; // namespace Zigbee
