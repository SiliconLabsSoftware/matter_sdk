/***************************************************************************//**
 * @brief RTOS interface for Bluetooth HCI-CPC driver
 *******************************************************************************
 * # License
 * <b>Copyright 2023 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmsis_os2.h>
#include "sl_cmsis_os2_common.h"
#include "sl_component_catalog.h"
#include "sl_core.h"
#include "sl_status.h"
#include "sli_cpc.h"
#include "matter_cpc.h"

#include "Assertions.h"

#define SL_MATTER_CPC_RTOS_TASK_PRIORITY osPriorityRealtime3
#define SL_MATTER_CPC_RTOS_TASK_STACK_SIZE 1000


#ifndef SL_CPC_ENDPOINT_MATTER_NCP_ID
#define SL_CPC_ENDPOINT_MATTER_NCP_ID SL_CPC_ENDPOINT_USER_ID_0 // 90
#endif

void sl_matter_packet_step(void);

static osSemaphoreId_t matter_cpc_signal_semaphore;

//Matter CPC thread
static void matter_cpc_thread(void *p_arg);
static osThreadId_t tid_thread_matter_cpc;
__ALIGNED(8) static uint8_t thread_matter_cpc_stk[
  SL_MATTER_CPC_RTOS_TASK_STACK_SIZE & 0xFFFFFFF8u];
__ALIGNED(4) static uint8_t thread_matter_cpc_cb[osThreadCbSize];
static const osThreadAttr_t thread_matter_cpc_attr = {
  .name = "Matter CPC",
  .cb_mem = thread_matter_cpc_cb,
  .cb_size = osThreadCbSize,
  .stack_mem = thread_matter_cpc_stk,
  .stack_size = sizeof(thread_matter_cpc_stk),
  .priority = (osPriority_t) SL_MATTER_CPC_RTOS_TASK_PRIORITY
};

uint8_t sl_matter_cpc_get_stack_space(uint32_t *stack_space)
{
  *stack_space = osThreadGetStackSpace(tid_thread_matter_cpc);
  return 0;
}

void sl_matter_cpc_rtos_deinit(void)
{
  (void) osSemaphoreDelete(matter_cpc_signal_semaphore);
  matter_cpc_signal_semaphore = NULL;

  (void) osThreadTerminate(tid_thread_matter_cpc);
  tid_thread_matter_cpc = NULL;
}

sl_status_t sl_matter_cpc_rtos_init(void)
{
  if (matter_cpc_signal_semaphore == NULL) {
    matter_cpc_signal_semaphore = osSemaphoreNew(1, 0, NULL);
  }
  if (matter_cpc_signal_semaphore == NULL) {
    goto failed;
  }

  // Create thread for Matter CPC
  if (tid_thread_matter_cpc == NULL) {
    tid_thread_matter_cpc = osThreadNew(matter_cpc_thread,
                                        NULL,
                                        &thread_matter_cpc_attr);
  }
  if (tid_thread_matter_cpc == NULL) {
    goto failed;
  }

  return SL_STATUS_OK;
  failed:
  sl_matter_cpc_rtos_deinit();
  return SL_STATUS_FAIL;
}

void sl_matter_cpc_on_transport_notify(uint8_t endpoint_id, void * arg)
{
  (void)endpoint_id;
  (void)arg;

  osSemaphoreRelease(matter_cpc_signal_semaphore);
}

static void matter_cpc_thread(void *p_arg)
{
  (void)p_arg;
  while (true) {
    osSemaphoreAcquire(matter_cpc_signal_semaphore, osWaitForever);
    sl_matter_packet_step();
  }
}

static sl_matter_cpc_state_t cpc_state = SL_MATTER_CPC_STATE_DISCONNECTED;
static sl_cpc_endpoint_handle_t endpoint_handle;

int sl_matter_cpc_new_data()
{
  return 1;//CPC-RTOS calls read packet only if semaphore is set
}

void sl_matter_cpc_rx_done()
{
  //NOOP: semaphore is used to block
}

void sl_btctrl_hci_cpc_rx(uint8_t endpoint_id, void *arg)
{
  sl_matter_cpc_on_transport_notify(endpoint_id, arg);
}

void sl_matter_cpc_tx_callback(sl_cpc_user_endpoint_id_t endpoint_id, void *buffer, void *arg, sl_status_t status)
{
  (void)endpoint_id;
  (void)buffer;
  (void)arg;
//   TODO MATTER specific behavior.
//   hci_common_transport_transmit_complete(status);
}

sl_status_t sl_matter_cpc_init(void)
{
  sl_status_t status = SL_STATUS_OK;
  // TODO remove me when we have a service endpoint
  status = sl_cpc_open_user_endpoint(&endpoint_handle, SL_CPC_ENDPOINT_MATTER_NCP_ID, 0, 1);

  // status = sli_cpc_init_service_endpoint(&endpoint_handle, SL_CPC_ENDPOINT_MATTER_NCP_ID, 0);
  VerifyOrReturnError(status == SL_STATUS_OK, status);
  
  status = sl_cpc_set_endpoint_option(&endpoint_handle, SL_CPC_ENDPOINT_ON_IFRAME_WRITE_COMPLETED, (void *)sl_matter_cpc_tx_callback);
  VerifyOrReturnError(status == SL_STATUS_OK, status);
  status = sl_cpc_set_endpoint_option(&endpoint_handle, SL_CPC_ENDPOINT_ON_IFRAME_RECEIVE, (void *)sl_matter_cpc_on_transport_notify);
  VerifyOrReturnError(status == SL_STATUS_OK, status);
  status = sl_cpc_set_endpoint_option(&endpoint_handle, SL_CPC_ENDPOINT_ON_CONNECT, (void*)sl_matter_cpc_on_connect);
  VerifyOrReturnError(status == SL_STATUS_OK, status);
  status = sl_cpc_set_endpoint_option(&endpoint_handle, SL_CPC_ENDPOINT_ON_ERROR, (void*)sl_matter_cpc_error);
  VerifyOrReturnError(status == SL_STATUS_OK, status);
  // status = sl_cpc_listen_endpoint(&endpoint_handle, SL_CPC_FLAG_NO_BLOCK);
  // VerifyOrReturnError(status == SL_STATUS_OK, status);
  
  return status;
}

void sl_matter_cpc_on_connect(uint8_t endpoint_id, void *arg)
{
  (void)endpoint_id;
  (void)arg;

  // Endpoint is connected. Allow message transmission.
  // TODO MATTER
//   hci_common_transport_transmit_reconnected();
  cpc_state = SL_MATTER_CPC_STATE_CONNECTED;
}

void sl_matter_cpc_error(uint8_t endpoint_id, void *arg)
{
  (void)endpoint_id;
  (void)arg;
  sl_status_t status;

  cpc_state = SL_MATTER_CPC_STATE_DISCONNECTED;
  status = sl_cpc_terminate_endpoint(&endpoint_handle, 0);
  EFM_ASSERT(status == SL_STATUS_OK);
  sl_matter_cpc_on_transport_notify(0, NULL);
}

int sl_matter_cpc_read(uint8_t **read_buf)
{
  sl_status_t status;
  uint16_t len;

  status = sl_cpc_read(&endpoint_handle, (void **) read_buf, &len, 0, 0);
  if (status != SL_STATUS_OK) {
    len = 0;
  }
  return len;
}

sl_status_t sl_matter_cpc_write(uint8_t *data, uint16_t len)
{
  return sl_cpc_write(&endpoint_handle, data, len, 0, NULL);
}

void sl_matter_cpc_free(void *buf)
{
  sl_cpc_free_rx_buffer((void *) buf);
}

bool sl_matter_is_cpc_connected(void)
{
  // Any state other than SL_CPC_STATE_CONNECTED is a disconnected state.
  return (cpc_state == SL_MATTER_CPC_STATE_CONNECTED);
}

void sl_matter_reconnect_cpc(void)
{
  switch (cpc_state) {
    case SL_MATTER_CPC_STATE_DISCONNECTED:
      sl_cpc_listen_endpoint(&endpoint_handle, SL_CPC_FLAG_NO_BLOCK);
      cpc_state = SL_MATTER_CPC_STATE_CONNECTING;
      break;

    case SL_MATTER_CPC_STATE_CONNECTING:
    case SL_MATTER_CPC_STATE_CONNECTED:
      // Nothing to do
      break;

    default:
      // Invalid state
      EFM_ASSERT(false);
  }
}



static bool initialized = false;
static uint8_t *read_buf = NULL;
static uint16_t bytes_remaining;

static void read_complete(uint16_t bytes_read, bool last_fragment)
{
    // TODO MATTER specific behavior.
//   hci_common_transport_receive(read_buf, bytes_read, last_fragment);
  sl_matter_cpc_free(read_buf);
  sl_matter_cpc_rx_done();
}

static void reception_failure(void)
{
  read_complete(0, false);
  // TODO MATTER specific behavior.
//   state = hci_packet_state_read_header;
}

void sl_matter_packet_step(void)
{
  uint16_t bytes_read;
  uint16_t packet_data_read;

  if (false == sl_matter_is_cpc_connected()) {
    sl_matter_reconnect_cpc();
    // The CPC endpoint is reconnected once the sl_matter_cpc_on_connect() callback has been invoked.
    return;
  }

  /* Check if data available */
  if (sl_matter_cpc_new_data() <= 0) {
    return;
  }

  bytes_read = sl_matter_cpc_read(&read_buf);
  if (bytes_read == 0) {
    sl_matter_cpc_rx_done();
    return; // CPC Secondary returned error
  } else {
    // packet = (hci_packet_t *) read_buf;
  }

//   switch (state) {
//     case hci_packet_state_read_header:
//     {
//       switch (packet->packet_type) {
//         case hci_packet_type_ignore:
//         {
//           state = hci_packet_state_read_header;
//           return;
//         }
//         case hci_packet_type_command:
//         {
//           packet_data_read = bytes_read - (hci_command_header_size + 1);
//           if (packet->hci_cmd.param_len > packet_data_read) { // 1 byte for packet type
//             bytes_remaining = packet->hci_cmd.param_len - packet_data_read;
//           } else {
//             bytes_remaining = 0;
//           }
//           break;
//         }
//         case hci_packet_type_acl_data:
//         {
//           packet_data_read = bytes_read - (hci_acl_data_header_size + 1);
//           if (packet->acl_pkt.length > packet_data_read) {
//             bytes_remaining = packet->acl_pkt.length - packet_data_read;
//           } else {
//             bytes_remaining = 0;
//           }
//           break;
//         }
//         default:
//         {
//           reception_failure();
//           return;
//         }
//       }

//       if (bytes_remaining == 0) {
//         read_complete(bytes_read, true);
//         state = hci_packet_state_read_header;
//         return;
//       } else {
//         read_complete(bytes_read, false);
//         state = hci_packet_state_read_data;
//       }
//       break;
//     }
//     case hci_packet_state_read_data:
//     {
//       if (bytes_remaining > bytes_read) {
//         bytes_remaining -= bytes_read;
//         read_complete(bytes_read, false);
//       } else {
//         read_complete(bytes_read, true);
//         state = hci_packet_state_read_header;
//       }
//       break;
//     }
//     default:
//     {
//       reception_failure();
//       return;
//     }
//   }
}

uint32_t hci_common_transport_transmit(uint8_t *data, int16_t len)
{
  return sl_matter_cpc_write(data, len);
}

// void hci_common_transport_init(void)
// {
//   if (initialized) {
//     return;
//   } else {
//     initialized = true;
//   }

//   state = hci_packet_state_read_header;
//  TODO MATTER specific behavior.
//   sl_matter_cpc_init();
// }
