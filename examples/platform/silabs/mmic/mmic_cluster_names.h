/*******************************************************************************
 * @file
 * @brief Human readable names for clusters forwarded over MMIC (host side).
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

#include <stdint.h>

// Each lookup returns NULL when the id is unknown.
const char * mmic_cluster_name(uint32_t clusterId);
const char * mmic_command_name(uint32_t clusterId, uint32_t commandId);
const char * mmic_attribute_name(uint32_t clusterId, uint32_t attributeId);
