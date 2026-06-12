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
        // Can be encoded and sent directly
        if (id == ping) 
        {
            workbuffer[1] = MMIC_PACKET_OVERHEAD; // no arguments
            workbuffer[2] = id;
            
            uint16_t crc = crc16(workbuffer,3);
            memcpy(workbuffer+3, &crc, 2);
        }
        
        *encodedPacket=workbuffer; // Warning must be freed by the caller
    }
  
    return 0;
}

uint8_t decodeResponse(uint8_t * buffer, size_t len, uint8_t ** stringToPrint)
{

    if(buffer == NULL || len == 0 || stringToPrint == NULL)
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
            uint8_t * tmp = malloc( ((buffer[1]-MMIC_PACKET_OVERHEAD) +4) * sizeof(uint8_t));
            if(tmp==NULL) return 3;

            snprintf(tmp,((buffer[1]-MMIC_PACKET_OVERHEAD) +4), "\r\n%s\r\n",(char *)buffer+3 );
            *stringToPrint = tmp;
            break;
        default:
            return 3; //Not implemented
    };
    return 0;
}

void printHelp(void)
{
    #define X(a,b,c, d) fprintf(stderr, #a " :\t" b "\r\n");
    COMMAND_LIST
    #undef X   
}
#else

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
            encodeResponse(ping, "pong", sizeof("pong"), response, packetSize);
            break;
        default:
            return 3; //Not implemented
    };
        

    return 0;
}

#endif // HOST_SIDE