/*******************************************************************************
 * @file
 * @brief Transport abstraction used by the MMIC task to exchange frames with the host.
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
#include <stdint.h>

// Exactly one implementation must be linked:
//   - mmic_transport_cpc.cpp  (matter_mmic_cpc, default)
//   - mmic_transport_uart.cpp (matter_mmic_uart, raw UART through the matter_uart driver)

sl_status_t mmic_transport_init(void);

// Blocks until one MMIC frame is received. Returns the frame length and sets *frame,
// which must then be released with mmic_transport_release(). Returns <= 0 when no
// frame could be read; the caller simply retries.
int mmic_transport_receive(uint8_t ** frame);

void mmic_transport_release(uint8_t * frame);

// Sends one complete MMIC frame. Callers must serialize concurrent sends.
sl_status_t mmic_transport_send(const uint8_t * data, uint16_t len);
