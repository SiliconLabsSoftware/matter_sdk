/*******************************************************************************
 * @file
 * @brief Host side test of the camera command forwarding over MMIC.
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

#include <stddef.h>
#include <stdint.h>

// Prints the list of expected messages.
void mmic_camera_test_begin(void);

// Handles one frame initiated by the device. When the device expects an answer,
// *answer is set to a malloc'd frame (freed by the caller) to send back.
void mmic_camera_test_handle_request(const uint8_t * frame, size_t len, uint8_t ** answer, size_t * answerLen);

// True once every expected message has been received.
bool mmic_camera_test_complete(void);

// Prints the final result.
void mmic_camera_test_print_summary(bool interrupted);
