#include "mmic.h"
#include "string.h"
#include "stdlib.h"

uint16_t crc16(const uint8_t * buffer, uint8_t size)
{
    uint16_t crc = 0xFFFF;
    while (size--)
    {
        crc ^= (uint16_t)(*buffer++) << 8;
        for (uint8_t i = 0; i < 8; i++)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

#if HOST_SIDE
#include <stdio.h>

static const commandsData_t commands[] =
{
    #define X(a,b,c, d) {.argsCnt=c, .argsSize= (c * sizeof(d))},
    COMMAND_LIST
    #undef X
};

const char commandsString[][255] = {
    #define X(a,b,c, d) #a,
    COMMAND_LIST
    #undef X
};


uint8_t encodeCommand(mmic_command_id_e id, void * parameter, uint16_t size, uint8_t ** encodedPacket, size_t * packetSize)
{
    if(id >= INVALID_COMMAND_ID) 
    {
        return 1;
    }

    if((commands[id].argsCnt == 0 && parameter != NULL) || (commands[id].argsCnt != 0 && parameter == NULL)) 
    {
        return 1;
    }    
    

    // allocate buffer 
    *packetSize = (MMIC_PACKET_OVERHEAD+(commands[id].argsSize)) * sizeof(uint8_t);
    uint8_t * workbuffer = (uint8_t *) malloc(*packetSize);
    if (workbuffer == NULL)
    {
        return 2;
    }
    workbuffer[0] = MMIC_HEADER_CMD;
    if (commands[id].argsCnt == 0)
    {
        workbuffer[1] = MMIC_PACKET_OVERHEAD; // no arguments
        workbuffer[2] = id;
        
        uint16_t crc = crc16(workbuffer,3);
        memcpy(workbuffer+3, &crc, 2);
        
        *encodedPacket=workbuffer; // Warning must be freed by the caller
    }
    else
    {
        switch(id)
        {
            case establish_subscription:
            {
                // Caller (host_main) serializes the subscriptionArgs_t payload
                // into `parameter`. Validate and copy it onto the wire.
                if (size != sizeof(subscriptionArgs_t))
                {
                    free(workbuffer);
                    return 1;
                }

                const size_t actualPacketSize = MMIC_PACKET_OVERHEAD + size;

                workbuffer[1] = (uint8_t)actualPacketSize;
                workbuffer[2] = id;
                memcpy(workbuffer + 3, parameter, size);

                uint16_t crc = crc16(workbuffer, (uint8_t)(actualPacketSize - 2));
                memcpy(workbuffer + actualPacketSize - 2, &crc, 2);

                *packetSize    = actualPacketSize;
                *encodedPacket = workbuffer; // Warning must be freed by the caller
                break;
            }
            default:
                free(workbuffer);
                return 2;
        }
    }
  
    return 0;
}

uint8_t decodeAndPrintResponse(uint8_t * buffer, size_t len)
{

    if(buffer == NULL || len == 0 )
    {
        return 1;
    }

    // Packet Integrity validation
    if (buffer[0] != MMIC_HEADER_ANS || buffer[2] >= INVALID_COMMAND_ID || buffer[1] > len || buffer[1] < MMIC_PACKET_OVERHEAD)
    {
        return 2;
    }

    // CRC validation
    uint16_t crc=0;
    memcpy(&crc, buffer + (buffer[1] - 2), 2);
    if (crc != crc16(buffer,(buffer[1]-2)))
    {
        return 2;
    }

    switch(buffer[2])
    {
        case ping:
            printf("\r\n%s\r\n",(char *)buffer+3 );
            break;
        case version:
            printf("\r\n%s\r\n",(char *)buffer+3 );
            break;
        case matter_state:
            matterState_t state;
            memcpy(&state,buffer+3, sizeof(matterState_t));
            printf("\r\nNumber of Fabrics: %d\r\nCommisionning Window Open:  %s\r\n",state.nbOfFabric, (state.commissioningWindowOpen)? "true" : "false" );
            break; 
        case establish_subscription:
            printf("\r\n%s\r\n", buffer[2] == 0 ? "Success":"Failure");
            break;
        default:
            return 3; //Not implemented
    };
    fflush(stdout);
    return 0;
}

void printHelp(void)
{
    #define X(a,b,c, d) fprintf(stderr, #a " :\t" b "\r\n");
    COMMAND_LIST
    #undef X   
}
#else
#include "../subscription/SubscriptionManager.h"
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <credentials/FabricTable.h>
#include <lib/core/CHIPError.h>


uint8_t encodeResponse(mmic_command_id_e id, void * response, size_t responseLen, uint8_t ** encodedPacket, size_t * packetSize)
{
    if(id >= INVALID_COMMAND_ID) 
    {
        return 1;
    }

    if(packetSize == NULL) 
    {
        return 1;
    }    
    // allocate buffer 
    *packetSize = (MMIC_PACKET_OVERHEAD+responseLen) * sizeof(uint8_t);
    uint8_t * workbuffer = (uint8_t *) malloc(*packetSize);
    if (workbuffer == NULL)
    {
        return 2;
    }
    workbuffer[0] = MMIC_HEADER_ANS;
    workbuffer[2] = id;
    if(responseLen != 0)
    {
        memcpy(workbuffer+3, response, responseLen);
    }
    workbuffer[1] = MMIC_PACKET_OVERHEAD + responseLen;
    uint16_t crc = crc16(workbuffer, ((*packetSize)-2));
    memcpy(workbuffer+(*packetSize-2), &crc, 2);
    *encodedPacket=workbuffer;
    return 0;


}

uint8_t parseAndRunCommand(uint8_t * buffer, uint16_t len, uint8_t ** response, size_t * packetSize)
{
    if (buffer == NULL || len == 0 || response == NULL || packetSize == NULL)
    {
        return 1;
    }

    uint16_t crc=0;
    memcpy(&crc, buffer + (len - 2), 2);
    // Packet Integrity validation
    if (buffer[0] != MMIC_HEADER_CMD || buffer[2] >= INVALID_COMMAND_ID || crc != crc16(buffer,(len-2)))
    {
        return 2;
    }

    switch(buffer[2])
    {
        case ping:
            encodeResponse(ping, const_cast<char *>("pong"), sizeof("pong"), response, packetSize);
            break;
        case version:
            encodeResponse(ping, const_cast<char *>(MMIC_VERSION_STRING), sizeof(MMIC_VERSION_STRING), response, packetSize);
            break;
        case matter_state: // To verify commissioning
            {
                matterState_t state;
                if (encodeMatterState(&state) == 0)
                {
                    encodeResponse(matter_state, &state, sizeof(matterState_t), response, packetSize);
                }
            }
            break;
        case openCommissioning:
            {
                if(chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow() != CHIP_NO_ERROR)
                {
                    // encode success response
                }
            }
            break;
        case establish_subscription:
            {
                const uint16_t payloadLen = buffer[1] - MMIC_PACKET_OVERHEAD;
                if (payloadLen < sizeof(subscriptionArgs_t))
                {
                    return 2;
                }

                subscriptionArgs_t args;
                memcpy(&args, buffer + 3, sizeof(args));

                chip::Silabs::SubscriptionManager::Info info;
                info.fabricIndex = args.fabricIndex;
                info.nodeId      = args.nodeId;
                info.endpointId  = args.endpointId;
                info.clusterId   = args.clusterId;
                info.attributeId = args.attributeId;

                chip::Silabs::SubscriptionManager::Handle handle =
                    chip::Silabs::SubscriptionManager::kInvalidHandle;
                CHIP_ERROR err = chip::Silabs::SubscriptionManager::Instance().Subscribe(info, &handle);

                uint8_t status = err == CHIP_NO_ERROR ? 0 : 1;
                encodeResponse(establish_subscription, &status, sizeof(status), response, packetSize);
            }
            break;
        default:
            return 3; //Not implemented
    };
        

    return 0;
}



uint8_t encodeMatterState(matterState_t * state)
{
    if(state == nullptr)
    {
        return 1;
    }

    state->nbOfFabric = chip::Server::GetInstance().GetFabricTable().FabricCount();
    state->commissioningWindowOpen= chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    return 0;
    
}
uint8_t establishSubscription()
{
    return 0;
}
uint8_t getSubscriptionsInfo()
{
    return 0;
}
#endif // HOST_SIDE