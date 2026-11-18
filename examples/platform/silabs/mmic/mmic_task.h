/*******************************************************************************
 * @file
 * @brief MMIC task: receives host requests/answers and sends frames to the host.
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

#include "sl_status.h"
#include "mmic.h"

// Initializes the linked MMIC transport and starts the MMIC task.
sl_status_t mmic_init(void);

bool mmic_is_ready(void);

// Sends one complete MMIC frame to the host. Thread-safe.
uint8_t mmic_send_frame(const uint8_t * frame, uint16_t len);
