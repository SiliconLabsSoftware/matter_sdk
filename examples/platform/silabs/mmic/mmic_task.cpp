/*******************************************************************************
 * @file
 * @brief MMIC task: receives host requests/answers and sends frames to the host.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cmsis_os2.h"
#include "sl_cmsis_os2_common.h"

#include "mmic.h"
#include "mmic_forward.h"
#include "mmic_task.h"
#include "mmic_transport.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

#ifndef TERMINAL_TASK_STACK_SIZE
#define TERMINAL_TASK_STACK_SIZE      4096  // Stack size in bytes (CMSIS-OS 2)
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

// Serializes frames sent by the MMIC task and by application threads.
static osMutexId_t sTxMutex;
static volatile bool sReady = false;

/*******************************************************************************
 *********************   LOCAL FUNCTION PROTOTYPES   ***************************
 ******************************************************************************/
void mmic_task(void *pvParameters);

/*******************************************************************************
 **************************   GLOBAL FUNCTIONS   *******************************
 ******************************************************************************/

/*******************************************************************************
 * Initialize the MMIC transport and task.
 ******************************************************************************/
sl_status_t mmic_init(void)
{
  if (sReady) {
    return SL_STATUS_OK;
  }

  sTxMutex = osMutexNew(NULL);
  if (sTxMutex == NULL) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  sl_status_t status = mmic_forward_init();
  if (status != SL_STATUS_OK) {
    return status;
  }

  status = mmic_transport_init();
  if (status != SL_STATUS_OK) {
    return status;
  }

  // Ready before the task starts so that its first answer can be sent.
  sReady = true;
  sTerminalTaskHandle = osThreadNew(mmic_task, NULL, &sTerminalTaskAttr);
  if (sTerminalTaskHandle == NULL) {
    sReady = false;
    return SL_STATUS_FAIL;
  }
  return SL_STATUS_OK;
}

bool mmic_is_ready(void)
{
  return sReady;
}

uint8_t mmic_send_frame(const uint8_t * frame, uint16_t len)
{
  if (frame == NULL || len == 0) {
    return MMIC_ERROR_INVALID_ARG;
  }
  if (!sReady) {
    return MMIC_ERROR_NOT_READY;
  }

  osMutexAcquire(sTxMutex, osWaitForever);
  sl_status_t status = mmic_transport_send(frame, len);
  osMutexRelease(sTxMutex);

  return (status == SL_STATUS_OK) ? MMIC_ERROR_OK : MMIC_ERROR_TRANSPORT;
}

/*******************************************************************************
 * MMIC task.
 ******************************************************************************/

void mmic_task(void *pvParameters)
{
  uint8_t * rxBuffer = NULL;
  int rxLen = 0;
  uint8_t * responseBuffer = NULL;
  size_t responseSize = 0;
  (void)pvParameters;

  while (1) {
    rxBuffer = NULL;
    // Blocking call.
    rxLen = mmic_transport_receive(&rxBuffer);
    if (rxLen <= 0 || rxBuffer == NULL) {
      mmic_transport_release(rxBuffer);
      continue;
    }

    // Basic length sanity check against the MMIC framing (header + advertised length).
    if (rxLen >= (int)MMIC_PACKET_OVERHEAD) {
      const uint16_t frameLen = mmic_read_length(rxBuffer);
      if (frameLen >= MMIC_PACKET_OVERHEAD && (uint16_t)rxLen >= frameLen) {
        if (rxBuffer[0] == MMIC_HEADER_CMD) {
          parseAndRunCommand(rxBuffer, frameLen, &responseBuffer, &responseSize);
        } else if (rxBuffer[0] == MMIC_HEADER_ANS) {
          mmic_forward_handle_answer(rxBuffer, frameLen);
        }
      }
    }

    mmic_transport_release(rxBuffer);

    if (responseBuffer != NULL) {
      (void)mmic_send_frame(responseBuffer, (uint16_t)responseSize);
      free(responseBuffer);
      responseBuffer = NULL;
      responseSize = 0;
    }
  }
}
