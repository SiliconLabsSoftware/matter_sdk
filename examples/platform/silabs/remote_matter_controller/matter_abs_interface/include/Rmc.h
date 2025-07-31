/**
 * @file
 * @brief Matter abstraction layer for Direct Internet Connectivity.
 *******************************************************************************
 * # License
 * <b>Copyright 2023 Silicon Laboratories Inc.
 *www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon
 *Laboratories Inc. Your use of this software is
 *governed by the terms of Silicon Labs Master
 *Software License Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.
 *This software is distributed to you in Source Code
 *format and is governed by the sections of the MSLA
 *applicable to Source Code.
 *
 ******************************************************************************/

#ifndef __RMC_H
#define __RMC_H

#include <stdint.h>

#include "RmcConfig.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "mqtt.h"
typedef enum
{
    RMC_OK = 0,
    RMC_ERR_INVAL,
    RMC_ERR_MEM,
    RMC_ERR_FAIL,
    RMC_ERR_CONN,
    RMC_ERR_PUBLISH,
} rmc_err_t;

typedef struct
{
    uint8_t * dataP;
    uint16_t dataLen;
} rmc_buff_t;

typedef void (*rmc_subscribe_cb)(void);

rmc_err_t rmc_init(rmc_subscribe_cb subs_cb);

rmc_err_t rmc_mqtt_subscribe(mqtt_client_t * client, mqtt_incoming_publish_cb_t publish_cb, mqtt_incoming_data_cb_t data_cb,
                             const char * topic, uint8_t qos);

rmc_err_t rmc_sendmsg(const char * subject, const char * content);

#ifdef ENABLE_AWS_OTA_FEAT

typedef void (*callback_t)(const char * sub_topic, uint16_t top_len, const void * pload, uint16_t pLoadLength);
struct sub_cb_info
{
    char * sub_topic;
    callback_t cb;
};

int rmc_init_status(void);

rmc_err_t rmc_aws_ota_publish(const char * const topic, const char * message, uint32_t message_len, uint8_t qos);

rmc_err_t rmc_aws_ota_unsubscribe(const char * topic);

rmc_err_t rmc_aws_ota_subscribe(const char * topic, uint8_t qos, callback_t subscribe_cb);

rmc_err_t rmc_aws_ota_process();

rmc_err_t rmc_aws_ota_close();
#endif // ENABLE_AWS_OTA_FEAT

#ifdef __cplusplus
}
#endif
#endif //__RMC_H
