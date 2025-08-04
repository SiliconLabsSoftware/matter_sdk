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

#ifndef __RMC_CONFIG_H
#define __RMC_CONFIG_H

/* Instance configuration */

#ifndef SL_RMC_NVM_EMBED_CERT
#define SL_RMC_NVM_EMBED_CERT (0)
#endif

/* Task Configuration */
#define RMC_TASK_NAME "RMC"
#define RMC_TASK_STACK_SIZE (2 * 1024) // 2k
#define RMC_TASK_PRIORITY (5)

/* Network Configuration */
#define RMC_SERVER_HOST ""
#define RMC_SERVER_PORT (8883)

#define RMC_KEEP_ALIVE (0)

/* MQTT Client Configuration */
#define RMC_CLIENT_ID "sl_rmc_client"
#define RMC_CLIENT_USER NULL
#define RMC_CLIENT_PASS NULL

#define MQTT_QOS_0 (0)
#define MQTT_SUBSCRIBE_TOPIC "command"

/* MQTT Client Certification Configuration */
#define RMC_CA_CERT_LENGTH (1212)
#define RMC_DEV_CERT_LENGTH (1212)
#define RMC_DEV_KEY_LENGTH (1212)
#define RMC_HOSTNAME_LENGTH (55)
#define RMC_CLIENTID_LENGTH (30)

#ifdef ENABLE_AWS_OTA_FEAT
#define AWS_OTA_TASK_STACK_SIZE (1024)
#define AWS_OTA_TASK_PRIORITY (1)
#endif // ENABLE_AWS_OTA_FEAT

#endif // __RMC_CONFIG_H
