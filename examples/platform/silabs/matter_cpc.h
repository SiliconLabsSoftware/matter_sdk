/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once
// Enum of CPC states
typedef enum {
  SL_MATTER_CPC_STATE_DISCONNECTED = 0,
  SL_MATTER_CPC_STATE_CONNECTING   = 1,   // sl_cpc_listen_endpoint() called, SL_CPC_ENDPOINT_ON_CONNECT callback not yet invoked
  SL_MATTER_CPC_STATE_CONNECTED    = 2,   // SL_CPC_ENDPOINT_ON_CONNECT callback invoked
} sl_matter_cpc_state_t;

sl_status_t sl_matter_cpc_init(void);
int sl_matter_cpc_read(uint8_t **read_buf);
void sl_matter_cpc_free(void *buf);
void sl_matter_cpc_rx_done();
sl_status_t sl_matter_cpc_write(uint8_t *data, uint16_t len);
int sl_matter_cpc_wait_for_new_data();
void sl_matter_cpc_on_connect(uint8_t endpoint_id, void *arg);
void sl_matter_cpc_error(uint8_t endpoint_id, void *arg);
bool sl_matter_is_cpc_connected(void);
void sl_matter_reconnect_cpc(void);

/**
 * Callback handler invoked when a new iframe is available. Also invoked
 * from sl_matter_cpc_error following an SL_CPC_ENDPOINT_ON_ERROR notification.
 */
void sl_matter_cpc_on_transport_notify(uint8_t endpoint_id, void * arg);

