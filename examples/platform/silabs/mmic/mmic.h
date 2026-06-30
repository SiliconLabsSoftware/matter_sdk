#pragma once

#include "stdint.h"
#include "stddef.h"
/* HOST_SIDE: 1 when building on a host OS (Linux or macOS), 0 otherwise. */
#if defined(__linux__) || (defined(__APPLE__) && defined(__MACH__))
#define HOST_SIDE 1
#else
#define HOST_SIDE 0
#endif

#define MMIC_VERSION_STRING "0.0.0.1"

#define MMIC_MAX_COMMAND_LENTGH 15
#define MMIC_HEADER_CMD 0xF1
#define MMIC_HEADER_ANS 0xF2
#define MMIC_PACKET_OVERHEAD (1 + 1 + 1 + 2) // Header, Packet Len, OP code, CRC16

#define COMMAND_LIST \
    X(ping, "Counterpart replies with \"pong\"", 0, uint8_t)\
    X(version, "Returns version string", 0, uint8_t)\
    X(matter_state, "Returns Matter current state", 0, uint8_t)\
    X(establish_subscription, "Establish subscription ", 5, matterState_t)\
    X(subscription_info, "Returns Matter current state", 0, uint8_t)\
    X(openCommissioning, "Open Commissioning Window", 0, uint8_t)

typedef enum mmic_command_id : uint8_t
{
    #define X(a,b,c, d) a,
    COMMAND_LIST
    #undef X
    INVALID_COMMAND_ID,
} mmic_command_id_e;

// Wire layout for the establish_subscription command payload (little-endian, packed).
struct __attribute__((packed)) subscriptionArgs_t
{
    uint8_t  fabricIndex;
    uint64_t nodeId;
    uint16_t endpointId;
    uint32_t clusterId;
    uint32_t attributeId;
};

typedef struct mmic
{
    uint8_t argsCnt;
    uint16_t argsSize;
}commandsData_t;

typedef struct matterState
{
    uint64_t nodeId;
    uint8_t nbOfFabric;
    uint8_t nbOfSubscription;
    bool commissioningWindowOpen;
}matterState_t;

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no xorout). */
uint16_t crc16(const uint8_t * buffer, uint8_t size);


#if HOST_SIDE
uint8_t encodeCommand(mmic_command_id_e id, void * parameter, uint16_t size, uint8_t ** encodedPacket, size_t * packetSize);
void printHelp(void);
 // TODO find a way to either do a map or find the max string len
extern const char commandsString[][255];
uint8_t decodeAndPrintResponse(uint8_t * buffer, size_t len);


#else
uint8_t encodeResponse(mmic_command_id_e id, void * response, size_t responseLen, uint8_t ** encodedPacket, size_t * packetSize);
uint8_t parseAndRunCommand(uint8_t * buffer, uint16_t len, uint8_t ** response, size_t * packetSize);

uint8_t encodeMatterState(matterState_t * state);

#endif // HOST_SIDE

// Commands Prototype
#define X(a,b,c, d) uint8_t a ## Handler(void * parameter);
COMMAND_LIST
#undef X