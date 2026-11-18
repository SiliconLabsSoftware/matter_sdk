/*******************************************************************************
 * @file
 * @brief MMIC transport over a raw UART, using the matter_uart console driver.
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

// The UART carries MMIC frames only: the Matter shell and UART logs must not share it.

#include "mmic_transport.h"

#include "cmsis_os2.h"
#include "mmic.h"
#include "uart.h"

#include <stddef.h>

#ifndef SL_MATTER_MMIC_UART_MAX_FRAME_SIZE
#define SL_MATTER_MMIC_UART_MAX_FRAME_SIZE 2048
#endif

// Delay between two polls of the UART RX FIFO when it is empty.
#ifndef SL_MATTER_MMIC_UART_POLL_INTERVAL_MS
#define SL_MATTER_MMIC_UART_POLL_INTERVAL_MS 5
#endif

// Maximum gap between two bytes of the same frame before it is dropped.
#ifndef SL_MATTER_MMIC_UART_INTER_BYTE_TIMEOUT_MS
#define SL_MATTER_MMIC_UART_INTER_BYTE_TIMEOUT_MS 100
#endif

static_assert(SL_MATTER_MMIC_UART_MAX_FRAME_SIZE >= MMIC_PACKET_OVERHEAD && SL_MATTER_MMIC_UART_MAX_FRAME_SIZE <= UINT16_MAX,
              "Invalid SL_MATTER_MMIC_UART_MAX_FRAME_SIZE");

namespace {

// Must not exceed UART_TX_MAX_BUF_LEN in uart.cpp.
constexpr uint16_t kTxChunkSize = 100;

// Staging buffer, must not exceed the uart.cpp RX FIFO size.
constexpr uint16_t kRxStageSize = 64;

uint8_t sRxFrame[SL_MATTER_MMIC_UART_MAX_FRAME_SIZE];
uint8_t sRxStage[kRxStageSize];
uint16_t sRxStageLen = 0;
uint16_t sRxStagePos = 0;

uint32_t MsToTicks(uint32_t ms)
{
    uint32_t ticks = static_cast<uint32_t>((static_cast<uint64_t>(ms) * osKernelGetTickFreq() + 999) / 1000);
    return (ticks == 0) ? 1 : ticks;
}

// Reads one byte from the UART. timeoutMs == 0 waits forever.
bool ReadByte(uint8_t & out, uint32_t timeoutMs)
{
    const uint32_t pollTicks = MsToTicks(SL_MATTER_MMIC_UART_POLL_INTERVAL_MS);
    uint32_t waitedMs        = 0;

    while (sRxStagePos >= sRxStageLen)
    {
        int16_t count = uartConsoleRead(reinterpret_cast<char *>(sRxStage), kRxStageSize);
        if (count > 0)
        {
            sRxStageLen = static_cast<uint16_t>(count);
            sRxStagePos = 0;
            break;
        }
        if (timeoutMs != 0 && waitedMs >= timeoutMs)
        {
            return false;
        }
        osDelay(pollTicks);
        waitedMs += SL_MATTER_MMIC_UART_POLL_INTERVAL_MS;
    }

    out = sRxStage[sRxStagePos++];
    return true;
}

} // namespace

sl_status_t mmic_transport_init(void)
{
    // No-op if the platform already initialized the console UART.
    uartConsoleInit();
    return SL_STATUS_OK;
}

int mmic_transport_receive(uint8_t ** frame)
{
    uint8_t byte = 0;

    // Hunt for the start of a frame; anything else on the line is discarded.
    do
    {
        ReadByte(byte, 0);
    } while (byte != MMIC_HEADER_CMD && byte != MMIC_HEADER_ANS);
    sRxFrame[0] = byte;

    for (uint16_t i = 1; i <= MMIC_OFFSET_LEN_HI; i++)
    {
        if (!ReadByte(sRxFrame[i], SL_MATTER_MMIC_UART_INTER_BYTE_TIMEOUT_MS))
        {
            return 0;
        }
    }

    const uint16_t frameLen = mmic_read_length(sRxFrame);
    if (frameLen < MMIC_PACKET_OVERHEAD || frameLen > sizeof(sRxFrame))
    {
        return 0;
    }

    for (uint16_t i = MMIC_OFFSET_LEN_HI + 1; i < frameLen; i++)
    {
        if (!ReadByte(sRxFrame[i], SL_MATTER_MMIC_UART_INTER_BYTE_TIMEOUT_MS))
        {
            return 0;
        }
    }

    // CRC is verified by the MMIC packet deserializer.
    *frame = sRxFrame;
    return frameLen;
}

void mmic_transport_release(uint8_t * frame)
{
    // Frames are received in a static buffer.
    (void) frame;
}

sl_status_t mmic_transport_send(const uint8_t * data, uint16_t len)
{
    if (data == NULL || len == 0)
    {
        return SL_STATUS_INVALID_PARAMETER;
    }

    while (len > 0)
    {
        const uint16_t chunk = (len < kTxChunkSize) ? len : kTxChunkSize;
        if (uartConsoleWrite(reinterpret_cast<const char *>(data), chunk) != static_cast<int16_t>(chunk))
        {
            return SL_STATUS_FAIL;
        }
        data += chunk;
        len = static_cast<uint16_t>(len - chunk);
    }
    return SL_STATUS_OK;
}
