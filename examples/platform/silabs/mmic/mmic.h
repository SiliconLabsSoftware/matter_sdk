#pragma once

#include "stdint.h"
#include "stddef.h"
/* HOST_SIDE: 1 when building on a host OS (Linux or macOS), 0 otherwise. */
#if defined(__linux__) || (defined(__APPLE__) && defined(__MACH__))
#define HOST_SIDE 1
#else
#define HOST_SIDE 0
#endif

#define MMIC_VERSION_STRING "0.0.0.2"

#define MMIC_MAX_COMMAND_LENTGH 15
#define MMIC_HEADER_CMD 0xF1
#define MMIC_HEADER_ANS 0xF2
// Packet layout (little-endian):
//   [0]     header (0xF1 cmd / 0xF2 ans)
//   [1..2]  packet length (uint16_t LE, total frame size including header/crc)
//   [3]     op code
//   [4..N-3] payload
//   [N-2..N-1] CRC-16/CCITT-FALSE over bytes [0..N-3]
#define MMIC_PACKET_OVERHEAD (1 + 2 + 1 + 2) // Header, Packet Len (u16), OP code, CRC16
#define MMIC_OFFSET_LEN_LO   1
#define MMIC_OFFSET_LEN_HI   2
#define MMIC_OFFSET_OP       3
#define MMIC_OFFSET_PAYLOAD  4

static inline uint16_t mmic_read_length(const uint8_t * pkt)
{
    return (uint16_t)((uint16_t)pkt[MMIC_OFFSET_LEN_LO] | ((uint16_t)pkt[MMIC_OFFSET_LEN_HI] << 8));
}

static inline void mmic_write_length(uint8_t * pkt, uint16_t len)
{
    pkt[MMIC_OFFSET_LEN_LO] = (uint8_t)(len & 0xFF);
    pkt[MMIC_OFFSET_LEN_HI] = (uint8_t)((len >> 8) & 0xFF);
}

#define COMMAND_LIST \
    X(ping, "Counterpart replies with \"pong\"", 0, uint8_t)\
    X(version, "Returns version string", 0, uint8_t)\
    X(matter_state, "Returns Matter current state", 0, uint8_t)\
    X(establish_subscription, "Establish subscription (usage: establish_subscription <fabricIndex> <nodeId> <endpointId> <clusterId> <attributeId>)", 5, subscriptionArgs_t)\
    X(subscription_info, "List active subscriptions", 0, uint8_t)\
    X(openCommissioning, "Open Commissioning Window", 0, uint8_t)\
    X(commission, "Commission using chip-tool storage (usage: commission <nodeId>)", 1, uint64_t)\
    X(decommission, "Delete all fabrics on the device", 0, uint8_t)

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

// Wire layout for the establish_subscription response (little-endian, packed).
// status: 0 on success, non-zero on failure.
// handle: SubscriptionManager slot handle (0xFF if not allocated).
struct __attribute__((packed)) subscriptionEstablishResp_t
{
    uint8_t status;
    uint8_t handle;
};

// Wire layout for one entry in the subscription_info response.
struct __attribute__((packed)) subscriptionEntry_t
{
    uint8_t  handle;
    uint8_t  fabricIndex;
    uint64_t nodeId;
    uint16_t endpointId;
    uint32_t clusterId;
    uint32_t attributeId;
};

// Cap for wire encoding of subscription_info. Must match SubscriptionManager::kMaxSubscriptions.
#define MMIC_SUBSCRIPTION_MAX_ENTRIES 10

// Wire layout for the commission command payload (little-endian, packed).
// This is the fixed-size header; certs (RCAC || ICAC || NOC) follow inline.
// All key/cert material is in Matter-TLV form except opkeyPriv/opkeyPub which
// are raw scalars / uncompressed EC points.
#define MMIC_COMMISSION_IPK_LEN        16
#define MMIC_COMMISSION_OPKEY_PRIV_LEN 32
#define MMIC_COMMISSION_OPKEY_PUB_LEN  65
struct __attribute__((packed)) commissionArgs_t
{
    uint64_t nodeId;
    uint64_t fabricId;
    uint16_t vendorId;
    uint8_t  ipk[MMIC_COMMISSION_IPK_LEN];
    uint8_t  opkeyPriv[MMIC_COMMISSION_OPKEY_PRIV_LEN];
    uint8_t  opkeyPub[MMIC_COMMISSION_OPKEY_PUB_LEN];
    uint16_t rcacLen;
    uint16_t icacLen; // 0 if no ICAC
    uint16_t nocLen;
    // Followed inline by: rcac[rcacLen] || icac[icacLen] || noc[nocLen]
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

    // First-fabric identity (populated only when nbOfFabric > 0). Useful for
    // diagnosing CASE destinationId mismatches: the Sigma1 destinationId is
    // HMAC(IPK, random || rootPublicKey || fabricId || peerNodeId), so the
    // peer must see the same fabricId + compressedFabricId + rootPublicKey.
    uint8_t fabricIndex;
    uint64_t fabricId;
    uint8_t compressedFabricId[8];  // Big-endian, matches Matter spec.
    uint8_t rootPublicKey[65];      // Uncompressed SEC1 point (0x04 || X || Y).

    // mDNS advertisement. DnssdServer has no public "is advertising" getter, so
    // this is inferred: operational records are advertised whenever the device
    // has at least one commissioned fabric. Commissionable advertising state is
    // reflected by commissioningWindowOpen above.
    bool mdnsOperationalAdvertising;

    // On-network commissioning (_matterc._udp) advertisement details.
    // Only meaningful when commissioningWindowOpen is true.
    char mdnsCommissionableInstanceName[17]; // 16 hex chars + null
    uint16_t mdnsCommissionableUdpPort;      // Secured Matter UDP port (SRV record)
    uint16_t setupDiscriminator;             // 12-bit long discriminator

    // Thread status (of the Matter OpenThread instance).
    bool threadInterfaceUp;         // otIp6IsEnabled
    bool threadAttached;            // ConnectivityMgr view
    uint8_t threadMeshLocalEid[16]; // Mesh-local EID (zeroed if unavailable)
    uint16_t threadPanId;
    uint16_t threadChannel;
    uint8_t threadExtendedPanId[8];
    char threadNetworkName[17];     // OT network name (16) + null
} matterState_t;

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no xorout). */
uint16_t crc16(const uint8_t * buffer, uint16_t size);


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

// Invoked from OnAttributeData on the CHIP stack thread for every subscription report.
// value carries the primitive attribute value decoded from the TLV payload:
//   - boolean:          0 or 1
//   - unsigned integer: raw value
//   - signed integer:   reinterpreted as uint64_t (two's complement)
//   - anything else:    0
typedef void (*mmic_subscription_cb_t)(uint16_t endpointId, uint32_t clusterId, uint32_t attributeId, uint64_t value);
void mmic_set_subscription_callback(mmic_subscription_cb_t cb);

#endif // HOST_SIDE

// Commands Prototype
#define X(a,b,c, d) uint8_t a ## Handler(void * parameter);
COMMAND_LIST
#undef X