/*******************************************************************************
 * @file
 * @brief iostream usart examples functions
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
#include "sl_iostream.h"
#include "sl_iostream_init_instances.h"
#include "sl_iostream_handles.h"
#include "cmsis_os2.h"
#include "sl_cmsis_os2_common.h"

#include "mmic.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

#ifndef BUFSIZE
#define BUFSIZE    80
#endif

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
void mmic_init(void)
{
  /* Prevent buffering of output/input. */
#if !defined(__CROSSWORKS_ARM) && defined(__GNUC__)
  setvbuf(stdout, NULL, _IONBF, 0);   /* Set unbuffered mode for stdout (newlib) */
  setvbuf(stdin, NULL, _IONBF, 0);    /* Set unbuffered mode for stdin (newlib) */
#endif

  // Create terminal task (CMSIS-OS 2)
  sTerminalTaskHandle = osThreadNew(mmic_task, NULL, &sTerminalTaskAttr);
  (void)sTerminalTaskHandle;
}

/*******************************************************************************
 * Terminal task.
 ******************************************************************************/
void mmic_task(void *pvParameters)
{
  
  uint8_t buffer[64];
  char byte;
  sl_status_t status;
  uint8_t cmdReceived =0;
  uint8_t state=0; // 0 -- Header, 1 -- Length
  uint8_t packetLenght=0;
  uint8_t * responseBuffer = NULL;
  size_t responseSize = 0;
  (void)pvParameters;

  /* Setting default stream */
  sl_iostream_set_default(sl_iostream_vcom_handle);

  /* Retrieve characters, print local echo and full line back */
  while (1) {

    while(cmdReceived == 0)
    {
      sl_iostream_getchar(SL_IOSTREAM_STDIN, &byte);
      if(byte==MMIC_HEADER_CMD && state==0)
      {
        buffer[0]=byte;
        state=1;
      } else if(state == 1)
      {
        buffer[1]=byte;
        packetLenght=2;
        state=3;
      } else if(state == 3 && packetLenght < buffer[1])
      {
        buffer[packetLenght] = byte;
        packetLenght++;
      }

      if(packetLenght >= sizeof(buffer))
      {
        // cleanup
        state=0;
        memset(buffer, 0, sizeof(buffer));
      }

      if(state == 3 && packetLenght == buffer[1])
      {
        // received everything
        break;
      }
    }
    
    parseAndRunCommand(buffer, buffer[1], &responseBuffer, &responseSize);
    if(responseBuffer!=NULL)
    {
      sl_iostream_write(SL_IOSTREAM_STDOUT, responseBuffer, responseSize);
      free(responseBuffer);responseBuffer=NULL;
    }
    // cleanup
    state=0;
    memset(buffer, 0, sizeof(buffer));
  }
}