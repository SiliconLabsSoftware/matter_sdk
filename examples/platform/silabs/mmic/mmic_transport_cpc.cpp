/*******************************************************************************
 * @file
 * @brief MMIC transport over the matter_cpc endpoint.
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

#include "mmic_transport.h"

#include "matter_cpc.h"

#include <stddef.h>

sl_status_t mmic_transport_init(void)
{
    return sl_matter_cpc_init();
}

int mmic_transport_receive(uint8_t ** frame)
{
    // Ensure the CPC endpoint is connected before any I/O attempt.
    if (!sl_matter_is_cpc_connected() && !sl_matter_is_cpc_waiting())
    {
        sl_matter_reconnect_cpc();
    }

    // Blocking call.
    sl_matter_cpc_wait_for_activity();

    // matter_cpc delivers a full frame per read; no byte-level state machine needed.
    uint8_t * rxBuffer = NULL;
    int rxLen          = sl_matter_cpc_read(&rxBuffer);
    if (rxLen <= 0 || rxBuffer == NULL)
    {
        if (rxBuffer != NULL)
        {
            sl_matter_cpc_free(rxBuffer);
        }
        return 0;
    }

    *frame = rxBuffer;
    return rxLen;
}

void mmic_transport_release(uint8_t * frame)
{
    if (frame != NULL)
    {
        sl_matter_cpc_free(frame);
    }
}

sl_status_t mmic_transport_send(const uint8_t * data, uint16_t len)
{
    return sl_matter_cpc_write(const_cast<uint8_t *>(data), len);
}
