/*******************************************************************************
 * @file sl-matter-attribute-storage.h
 * @brief Link zigbee datamodel attribute changes to Matter attribute storage.
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

#define sl_zigbee_af_core_println(format, ...)                                                                                     \
    do                                                                                                                             \
    {                                                                                                                              \
        (void) (format);                                                                                                           \
        ((void) 0, ##__VA_ARGS__);                                                                                                 \
    } while (0)
