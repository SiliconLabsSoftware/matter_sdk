/***************************************************************************
 * @file wifi_rcp.h
 * @brief Forwards non-Matter Wi-Fi traffic to a Linux host over SDIO.
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

#ifdef __cplusplus
extern "C" {
#endif

// Requires the SDIO secondary driver to be initialized (driver_init) and
// -Wl,--wrap=sli_si91x_wifi_data_packet_handler at link time.
void wifi_rcp_init(void);

#ifdef __cplusplus
}
#endif
