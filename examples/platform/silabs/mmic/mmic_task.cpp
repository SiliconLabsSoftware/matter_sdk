/*******************************************************************************
 * @file
 * @brief MMIC task using the matter_cpc transport.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cmsis_os2.h"
#include "sl_cmsis_os2_common.h"

#include "matter_cpc.h"
#include "mmic.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

#ifndef TERMINAL_TASK_STACK_SIZE
#define TERMINAL_TASK_STACK_SIZE      1024  // Stack size in bytes (CMSIS-OS 2)
#endif

#ifndef TERMINAL_TASK_PRIO
#define TERMINAL_TASK_PRIO            osPriorityNormal
#endif

/*******************************************************************************
 ***************************  LOCAL VARIABLES   ********************************
 ******************************************************************************/
static uint8_t sTerminalTaskStack[TERMINAL_TASK_STACK_SIZE];
static osThread_t sTerminalTaskControlBlock;
static const osThreadAttr_t sTerminalTaskAttr = {
    .name       = "mmic task",
    .attr_bits  = osThreadDetached,
    .cb_mem     = &sTerminalTaskControlBlock,
    .cb_size    = osThreadCbSize,
    .stack_mem  = sTerminalTaskStack,
    .stack_size = TERMINAL_TASK_STACK_SIZE,
    .priority   = TERMINAL_TASK_PRIO,
};
static osThreadId_t sTerminalTaskHandle;

/*******************************************************************************
 *********************   LOCAL FUNCTION PROTOTYPES   ***************************
 ******************************************************************************/
void mmic_task(void *pvParameters);

/*******************************************************************************
 **************************   GLOBAL FUNCTIONS   *******************************
 ******************************************************************************/

/*******************************************************************************
 * Initialize example.
 ******************************************************************************/
sl_status_t mmic_init(void)
{
  sl_status_t status;
  // Initialize the matter_cpc transport that backs the MMIC protocol.
  status=sl_matter_cpc_init();
  if(status != SL_STATUS_OK)
  {
    return status;
  }
  // Create terminal task (CMSIS-OS 2)
  sTerminalTaskHandle = osThreadNew(mmic_task, NULL, &sTerminalTaskAttr);
  return (sTerminalTaskHandle != NULL) ? SL_STATUS_OK : SL_STATUS_FAIL;
}

/*******************************************************************************
 * Terminal task.
 ******************************************************************************/

void mmic_task(void *pvParameters)
{
  uint8_t * rxBuffer = NULL;
  int rxLen = 0;
  uint8_t * responseBuffer = NULL;
  size_t responseSize = 0;
  (void)pvParameters;

  while (1) {
    // Ensure the CPC endpoint is connected before any I/O attempt.
    if (!sl_matter_is_cpc_connected()) {
      sl_matter_reconnect_cpc();
      osDelay(10);
      continue;
    }

    // Blocking call. 
    sl_matter_cpc_wait_for_new_data();


    // matter_cpc delivers a full frame per read; no byte-level state machine needed.
    rxLen = sl_matter_cpc_read(&rxBuffer);
    if (rxLen <= 0 || rxBuffer == NULL) {
      if (rxBuffer != NULL) {
        sl_matter_cpc_free(rxBuffer);
      }
      continue;
    }

    // Basic length sanity check against the MMIC framing (header + advertised length).
    if (rxLen >= 2 && (uint16_t)rxLen >= rxBuffer[1] && rxBuffer[0] == MMIC_HEADER_CMD) {
      parseAndRunCommand(rxBuffer, rxBuffer[1], &responseBuffer, &responseSize);
    }

    sl_matter_cpc_free(rxBuffer);
    rxBuffer = NULL;

    if (responseBuffer != NULL) {
      (void)sl_matter_cpc_write(responseBuffer, (uint16_t)responseSize);
      free(responseBuffer);
      responseBuffer = NULL;
      responseSize = 0;
    }
  }
}