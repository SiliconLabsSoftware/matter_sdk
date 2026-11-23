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

#include "mmic_forward.h"

#include "cmsis_os2.h"
#include "mmic_task.h"

#include <stdlib.h>
#include <string.h>

namespace {

struct PendingRequest
{
    bool active;
    uint16_t transactionId;
    uint8_t status;
    uint8_t * response;
    uint16_t responseCapacity;
    uint16_t responseLen;
    const mmic_bulk_sink_t * bulk;
    uint8_t * bulkBuffer; // Allocated by the sink on the first chunk
    uint32_t bulkTotal;
    uint32_t bulkReceived;
    uint32_t progress; // Number of chunks received, extends the answer timeout
};

osMutexId_t sRequestMutex;  // One blocking request at a time
osMutexId_t sPendingMutex;  // Protects sPending against the MMIC task
osSemaphoreId_t sAnswerSem; // Released when the pending request is answered
PendingRequest sPending;
uint16_t sNextTransactionId = 0;
mmic_cluster_command_handler_t sClusterCommandHandler = NULL;

// Interaction Model status returned to the host when no handler is registered.
constexpr uint8_t kStatusUnsupportedCluster = 0xC3;

uint32_t MsToTicks(uint32_t ms)
{
    uint32_t ticks = static_cast<uint32_t>((static_cast<uint64_t>(ms) * osKernelGetTickFreq() + 999) / 1000);
    return (ticks == 0) ? 1 : ticks;
}

// Builds a request frame from header + data and sends it to the host.
uint8_t SendRequest(mmic_command_id_e opcode, const void * header, size_t headerLen, const uint8_t * data, uint16_t dataLen)
{
    if (dataLen > 0 && data == NULL)
    {
        return MMIC_ERROR_INVALID_ARG;
    }

    const size_t payloadLen = headerLen + dataLen;
    uint8_t * payload       = static_cast<uint8_t *>(malloc(payloadLen));
    if (payload == NULL)
    {
        return MMIC_ERROR_NO_MEMORY;
    }
    memcpy(payload, header, headerLen);
    if (dataLen > 0)
    {
        memcpy(payload + headerLen, data, dataLen);
    }

    uint8_t * frame  = NULL;
    size_t frameSize = 0;
    uint8_t err      = mmic_serialize_packet(MMIC_HEADER_CMD, opcode, payload, payloadLen, &frame, &frameSize);
    free(payload);
    if (err != MMIC_ERROR_OK)
    {
        return err;
    }

    err = mmic_send_frame(frame, static_cast<uint16_t>(frameSize));
    free(frame);
    return err;
}

uint8_t SendClusterCommand(uint16_t transactionId, uint8_t flags, uint16_t endpointId, uint32_t clusterId, uint32_t commandId,
                           const uint8_t * params, uint16_t paramsLen)
{
    clusterCommandHeader_t header;
    header.transactionId = transactionId;
    header.flags         = flags;
    header.endpointId    = endpointId;
    header.clusterId     = clusterId;
    header.commandId     = commandId;
    return SendRequest(cluster_command, &header, sizeof(header), params, paramsLen);
}

uint16_t NextTransactionId()
{
    osMutexAcquire(sPendingMutex, osWaitForever);
    uint16_t id = sNextTransactionId++;
    osMutexRelease(sPendingMutex);
    return id;
}

uint32_t PendingProgress()
{
    osMutexAcquire(sPendingMutex, osWaitForever);
    const uint32_t progress = sPending.progress;
    osMutexRelease(sPendingMutex);
    return progress;
}

// Waits for the answer, as long as bulk chunks keep arriving within timeoutMs of each other.
uint8_t WaitForAnswer(uint32_t timeoutMs)
{
    uint32_t lastProgress = 0;
    while (osSemaphoreAcquire(sAnswerSem, MsToTicks(timeoutMs)) != osOK)
    {
        const uint32_t progress = PendingProgress();
        if (progress == lastProgress)
        {
            return MMIC_ERROR_TIMEOUT;
        }
        lastProgress = progress;
    }
    return MMIC_ERROR_OK;
}

// Stores one bulk_data chunk for the pending request. Called with sPendingMutex held.
uint8_t StoreBulkChunk(const bulkDataHeader_t & header, const uint8_t * data, uint32_t dataLen)
{
    if (!sPending.active || sPending.transactionId != header.transactionId || sPending.bulk == NULL ||
        sPending.bulk->alloc == NULL)
    {
        return MMIC_ERROR_NOT_READY;
    }
    if (header.totalLength == 0)
    {
        return MMIC_ERROR_INVALID_ARG;
    }

    if (sPending.bulkBuffer == NULL)
    {
        if (header.offset != 0)
        {
            return MMIC_ERROR_INVALID_ARG;
        }
        sPending.bulkBuffer = sPending.bulk->alloc(sPending.bulk->context, header.totalLength);
        if (sPending.bulkBuffer == NULL)
        {
            return MMIC_ERROR_NO_MEMORY;
        }
        sPending.bulkTotal    = header.totalLength;
        sPending.bulkReceived = 0;
    }
    else if (header.offset == 0 && header.totalLength == sPending.bulkTotal)
    {
        // The host restarted the transfer.
        sPending.bulkReceived = 0;
    }

    if (header.totalLength != sPending.bulkTotal || header.offset != sPending.bulkReceived ||
        dataLen > sPending.bulkTotal - sPending.bulkReceived)
    {
        return MMIC_ERROR_INVALID_ARG;
    }

    if (dataLen > 0)
    {
        memcpy(sPending.bulkBuffer + sPending.bulkReceived, data, dataLen);
    }
    sPending.bulkReceived += dataLen;
    sPending.progress++;
    return MMIC_ERROR_OK;
}

} // namespace

sl_status_t mmic_forward_init(void)
{
    sRequestMutex = osMutexNew(NULL);
    sPendingMutex = osMutexNew(NULL);
    sAnswerSem    = osSemaphoreNew(1, 0, NULL);
    if (sRequestMutex == NULL || sPendingMutex == NULL || sAnswerSem == NULL)
    {
        return SL_STATUS_ALLOCATION_FAILED;
    }
    memset(&sPending, 0, sizeof(sPending));
    return SL_STATUS_OK;
}

uint8_t mmic_send_cluster_command(uint16_t endpointId, uint32_t clusterId, uint32_t commandId, const uint8_t * params,
                                  uint16_t paramsLen)
{
    if (!mmic_is_ready())
    {
        return MMIC_ERROR_NOT_READY;
    }
    return SendClusterCommand(NextTransactionId(), 0, endpointId, clusterId, commandId, params, paramsLen);
}

uint8_t mmic_request_cluster_command(uint16_t endpointId, uint32_t clusterId, uint32_t commandId, const uint8_t * params,
                                     uint16_t paramsLen, uint32_t timeoutMs, uint8_t * hostStatus, uint8_t * response,
                                     uint16_t responseCapacity, uint16_t * responseLen)
{
    return mmic_request_cluster_command_bulk(endpointId, clusterId, commandId, params, paramsLen, timeoutMs, hostStatus, response,
                                             responseCapacity, responseLen, NULL, NULL);
}

uint8_t mmic_request_cluster_command_bulk(uint16_t endpointId, uint32_t clusterId, uint32_t commandId, const uint8_t * params,
                                          uint16_t paramsLen, uint32_t timeoutMs, uint8_t * hostStatus, uint8_t * response,
                                          uint16_t responseCapacity, uint16_t * responseLen, const mmic_bulk_sink_t * bulk,
                                          uint32_t * bulkLen)
{
    if (hostStatus == NULL || responseLen == NULL || (responseCapacity > 0 && response == NULL) ||
        (bulk != NULL && (bulk->alloc == NULL || bulkLen == NULL)))
    {
        return MMIC_ERROR_INVALID_ARG;
    }
    if (!mmic_is_ready())
    {
        return MMIC_ERROR_NOT_READY;
    }

    osMutexAcquire(sRequestMutex, osWaitForever);

    const uint16_t transactionId = NextTransactionId();

    osMutexAcquire(sPendingMutex, osWaitForever);
    // Drop a late answer to a previous, timed-out request.
    while (osSemaphoreAcquire(sAnswerSem, 0) == osOK)
    {
    }
    sPending.active           = true;
    sPending.transactionId    = transactionId;
    sPending.status           = 0;
    sPending.response         = response;
    sPending.responseCapacity = responseCapacity;
    sPending.responseLen      = 0;
    sPending.bulk             = bulk;
    sPending.bulkBuffer       = NULL;
    sPending.bulkTotal        = 0;
    sPending.bulkReceived     = 0;
    sPending.progress         = 0;
    osMutexRelease(sPendingMutex);

    uint8_t err = SendClusterCommand(transactionId, MMIC_CLUSTER_COMMAND_FLAG_ANSWER_EXPECTED, endpointId, clusterId, commandId,
                                     params, paramsLen);
    if (err == MMIC_ERROR_OK)
    {
        err = WaitForAnswer(timeoutMs);
    }

    osMutexAcquire(sPendingMutex, osWaitForever);
    if (sPending.active)
    {
        // Not answered: the answer handler cleared active otherwise.
        sPending.active = false;
        if (err == MMIC_ERROR_OK)
        {
            err = MMIC_ERROR_TIMEOUT;
        }
    }
    else if (err == MMIC_ERROR_OK)
    {
        *hostStatus  = sPending.status;
        *responseLen = sPending.responseLen;
        if (bulkLen != NULL)
        {
            *bulkLen = sPending.bulkReceived;
        }
        if (sPending.status == 0 && sPending.bulkReceived != sPending.bulkTotal)
        {
            err = MMIC_ERROR_INVALID_PACKET;
        }
    }
    sPending.response   = NULL;
    sPending.bulk       = NULL;
    sPending.bulkBuffer = NULL;
    osMutexRelease(sPendingMutex);

    osMutexRelease(sRequestMutex);
    return err;
}

uint8_t mmic_send_attribute_changed(uint16_t endpointId, uint32_t clusterId, uint32_t attributeId, const uint8_t * value,
                                    uint16_t valueLen)
{
    if (!mmic_is_ready())
    {
        return MMIC_ERROR_NOT_READY;
    }

    attributeChangedHeader_t header;
    header.endpointId  = endpointId;
    header.clusterId   = clusterId;
    header.attributeId = attributeId;
    return SendRequest(attribute_changed, &header, sizeof(header), value, (value != NULL) ? valueLen : 0);
}

void mmic_set_cluster_command_handler(mmic_cluster_command_handler_t handler)
{
    sClusterCommandHandler = handler;
}

uint8_t mmic_forward_handle_request(const uint8_t * payload, uint16_t payloadLen, uint8_t ** response, size_t * packetSize)
{
    if (payload == NULL || payloadLen < sizeof(clusterCommandHeader_t))
    {
        return MMIC_ERROR_INVALID_PACKET;
    }

    clusterCommandHeader_t request;
    memcpy(&request, payload, sizeof(request));

    clusterCommandAnswerHeader_t answer;
    answer.transactionId = request.transactionId;
    answer.status        = kStatusUnsupportedCluster;
    if (sClusterCommandHandler != NULL)
    {
        answer.status = sClusterCommandHandler(request.endpointId, request.clusterId, request.commandId,
                                               payload + sizeof(request), static_cast<uint16_t>(payloadLen - sizeof(request)));
    }

    return mmic_serialize_packet(MMIC_HEADER_ANS, cluster_command, &answer, sizeof(answer), response, packetSize);
}

uint8_t mmic_forward_handle_bulk_data(const uint8_t * payload, uint16_t payloadLen, uint8_t ** response, size_t * packetSize)
{
    if (payload == NULL || payloadLen < sizeof(bulkDataHeader_t))
    {
        return MMIC_ERROR_INVALID_PACKET;
    }

    bulkDataHeader_t header;
    memcpy(&header, payload, sizeof(header));

    bulkDataAnswerHeader_t answer;
    answer.transactionId = header.transactionId;
    osMutexAcquire(sPendingMutex, osWaitForever);
    answer.status = StoreBulkChunk(header, payload + sizeof(header), static_cast<uint32_t>(payloadLen - sizeof(header)));
    osMutexRelease(sPendingMutex);

    return mmic_serialize_packet(MMIC_HEADER_ANS, bulk_data, &answer, sizeof(answer), response, packetSize);
}

void mmic_forward_handle_answer(const uint8_t * frame, uint16_t len)
{
    mmic_command_id_e opcode = INVALID_COMMAND_ID;
    const uint8_t * payload  = NULL;
    uint16_t payloadLen      = 0;
    if (mmic_deserialize_packet(frame, len, MMIC_HEADER_ANS, &opcode, &payload, &payloadLen) != MMIC_ERROR_OK ||
        opcode != cluster_command || payloadLen < sizeof(clusterCommandAnswerHeader_t))
    {
        return;
    }

    clusterCommandAnswerHeader_t header;
    memcpy(&header, payload, sizeof(header));
    const uint8_t * data    = payload + sizeof(header);
    const uint16_t dataLen  = static_cast<uint16_t>(payloadLen - sizeof(header));

    osMutexAcquire(sPendingMutex, osWaitForever);
    if (sPending.active && sPending.transactionId == header.transactionId)
    {
        const uint16_t copyLen = (dataLen < sPending.responseCapacity) ? dataLen : sPending.responseCapacity;
        if (copyLen > 0)
        {
            memcpy(sPending.response, data, copyLen);
        }
        sPending.responseLen = copyLen;
        sPending.status      = header.status;
        sPending.active      = false;
        osSemaphoreRelease(sAnswerSem);
    }
    osMutexRelease(sPendingMutex);
}
