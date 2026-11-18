/*******************************************************************************
 * @file
 * @brief MMIC request handlers specific to the wake-on-matter application.
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

#include "mmic.h"

// Invoked from OnAttributeData on the CHIP stack thread for every subscription report.
// value carries the primitive attribute value decoded from the TLV payload:
//   - boolean:          0 or 1
//   - unsigned integer: raw value
//   - signed integer:   reinterpreted as uint64_t (two's complement)
//   - anything else:    0
typedef void (*mmic_subscription_cb_t)(uint16_t endpointId, uint32_t clusterId, uint32_t attributeId, uint64_t value);
void mmic_set_subscription_callback(mmic_subscription_cb_t cb);
