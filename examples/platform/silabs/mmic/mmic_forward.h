/*******************************************************************************
 * @file
 * @brief Generic forwarding of cluster commands and attribute changes to the host.
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

#include "mmic.h"
#include "sl_status.h"

// Payloads (params, value, response) are opaque to MMIC; Matter TLV is expected.
// See mmic_matter_forward.h for typed C++ helpers.

// Sends a cluster command to the host without waiting for an answer.
uint8_t mmic_send_cluster_command(uint16_t endpointId, uint32_t clusterId, uint32_t commandId, const uint8_t * params,
                                  uint16_t paramsLen);

// Sends a cluster command and blocks until the host answers or timeoutMs elapses.
// On MMIC_ERROR_OK, *hostStatus holds the status returned by the host and up to
// responseCapacity bytes of response data are copied to response (*responseLen).
// Only one request can be pending at a time; concurrent callers are serialized.
uint8_t mmic_request_cluster_command(uint16_t endpointId, uint32_t clusterId, uint32_t commandId, const uint8_t * params,
                                     uint16_t paramsLen, uint32_t timeoutMs, uint8_t * hostStatus, uint8_t * response,
                                     uint16_t responseCapacity, uint16_t * responseLen);

// Notifies the host that an attribute changed. value may be NULL when not provided.
uint8_t mmic_send_attribute_changed(uint16_t endpointId, uint32_t clusterId, uint32_t attributeId, const uint8_t * value,
                                    uint16_t valueLen);

// Handler of the cluster commands sent by the host (cluster_command requests). Called from the
// MMIC task: it must not block nor take the Matter stack lock. Returns the Interaction Model
// status code sent back to the host.
typedef uint8_t (*mmic_cluster_command_handler_t)(uint16_t endpointId, uint32_t clusterId, uint32_t commandId,
                                                   const uint8_t * params, uint16_t paramsLen);
void mmic_set_cluster_command_handler(mmic_cluster_command_handler_t handler);

// Internal, used by the MMIC task.
sl_status_t mmic_forward_init(void);
void mmic_forward_handle_answer(const uint8_t * frame, uint16_t len);

// Internal, used by the MMIC core to answer a cluster_command request received from the host.
uint8_t mmic_forward_handle_request(const uint8_t * payload, uint16_t payloadLen, uint8_t ** response, size_t * packetSize);
