/**
 * @file
 * @brief Matter abstraction layer for AWS
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

#ifndef __MATTER_HTTPS_H
#define __MATTER_HTTPS_H

#include <stdint.h>

#include "MatterHttpsConfig.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "mqtt.h"
typedef enum
{
    MATTER_HTTPS_OK = 0,
    MATTER_HTTPS_ERR_INVAL,
    MATTER_HTTPS_ERR_MEM,
    MATTER_HTTPS_ERR_FAIL,
    MATTER_HTTPS_ERR_CONN,
    MATTER_HTTPS_ERR_PUBLISH,
} matterHttps_err_t;

typedef struct
{
    uint8_t * dataP;
    uint16_t dataLen;
} matterHttps_buff_t;

typedef void (*matterHttps_subscribe_cb)(void);

matterHttps_err_t MatterHttpsInit(matterHttps_subscribe_cb subs_cb);

matterHttps_err_t MatterHttpsMqttSubscribe(mqtt_client_t * client, mqtt_incoming_publish_cb_t publish_cb,
                                       mqtt_incoming_data_cb_t data_cb, const char * topic, uint8_t qos);

matterHttps_err_t MatterHttpsSendMsg(const char * subject, const char * content);

#ifdef SL_MATTER_ENABLE_HTTPS_OTA_FEAT

typedef void (*callback_t)(const char * sub_topic, uint16_t top_len, const void * pload, uint16_t pLoadLength);
struct sub_cb_info
{
    char * sub_topic;
    callback_t cb;
};

int MatterHttpsInitStatus(void);

matterHttps_err_t MatterHttpsOtaPublish(const char * const topic, const char * message, uint32_t message_len, uint8_t qos);

matterHttps_err_t MatterHttpsOtaUnsubscribe(const char * topic);

matterHttps_err_t MatterHttpsOtaSubscribe(const char * topic, uint8_t qos, callback_t subscribe_cb);

matterHttps_err_t MatterHttpsOtaProcess();

matterHttps_err_t MatterHttpsOtaClose();
#endif // SL_MATTER_ENABLE_HTTPS_OTA_FEAT

#ifdef __cplusplus
}
#endif
#endif //__MATTER_HTTPS_H
