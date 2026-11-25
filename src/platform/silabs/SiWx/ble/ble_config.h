/*******************************************************************************
 * @file  ble_config.h
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
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

#include "rsi_ble_apis.h"
#include "rsi_bt_common_apis.h"
#include "rsi_user.h"

#ifdef __cplusplus
}
#endif

#if SL_MATTER_GN_BUILD == 0
#include "sl_matter_wifi_config.h"
#endif // SL_MATTER_GN_BUILD

/******************************************************
 * *                      Macros
 * ******************************************************/
#define RSI_BLE_ADDR_LENGTH 6

#define RSI_BLE_TX_OCTETS 251
#define RSI_BLE_TX_TIME 2120 // microseconds
#define RSI_BLE_MATTER_CUSTOM_SERVICE_DATA_LENGTH 240

#define GATT_READ_RESP 0x00
#define GATT_BLOB_READ_RESP 0x01

//! Matter overrides of rsi_ble_common_config.h defaults (must be defined before the include)
#ifdef SLI_SI91X_MCU_INTERFACE
#define RSI_BLE_MAX_NBR_ATT_REC 20
#define RSI_BLE_NUM_CONN_EVENTS 2
#define RSI_BLE_MAX_NBR_PERIPHERALS 1
#endif // SLI_SI91X_MCU_INTERFACE
#define RSI_BLE_MAX_NBR_CENTRALS (1)
#define RSI_BLE_GATT_ASYNC_ENABLE (1)

//! Reduced the BLE adv interval time to match with EFR BLE
#define RSI_BLE_ADV_INT_MIN (0x20)
#define RSI_BLE_ADV_INT_MAX (0x20)

//! Matter uses a random directed advertising address type (common default is LE_PUBLIC_ADDRESS)
#define RSI_BLE_ADV_DIR_ADDR_TYPE (0x01) // LE_RANDOM_ADDRESS

#define BLE_ATT_REC_SIZE (500)
#define NO_OF_VAL_ATT (5) //! Attribute value count

#define RSI_BT_FEATURE_BITMAP (SL_SI91X_BT_RF_TYPE | SL_SI91X_ENABLE_BLE_PROTOCOL)

#include "rsi_ble_common_config.h"

/***********************************************************************************************************************************************/
//! user defined structure
/***********************************************************************************************************************************************/
typedef struct rsi_ble_att_list_s
{
    uuid_t char_uuid;
    uint16_t handle;
    uint16_t value_len;
    uint16_t max_value_len;
    uint8_t char_val_prop;
    void * value;
} rsi_ble_att_list_t;
typedef struct rsi_ble_s
{
    uint8_t DATA[BLE_ATT_REC_SIZE];
    uint16_t DATA_ix;
    uint16_t att_rec_list_count;
    rsi_ble_att_list_t att_rec_list[NO_OF_VAL_ATT];
} rsi_ble_t;
