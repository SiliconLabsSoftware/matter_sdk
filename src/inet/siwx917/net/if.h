
#ifndef _NET_IF_H
#define _NET_IF_H 1

#include "socket.h" // For sockaddr
#include <sys/types.h>  // For basic types

#ifdef __cplusplus
extern "C" {
#endif

// Length of interface name
#define IF_NAMESIZE 16

// Interface flags
#define IFF_UP           0x1    // Interface is up
#define IFF_BROADCAST    0x2    // Broadcast address valid
#define IFF_LOOPBACK     0x8    // Is a loopback net
#define IFF_POINTOPOINT  0x10   // Interface is point-to-point link
#define IFF_RUNNING      0x40   // Resources allocated
#define IFF_MULTICAST    0x1000 // Supports multicast

// Interface request structure used for socket ioctl's
struct ifreq {
    char ifr_name[IF_NAMESIZE]; // Interface name, e.g., "eth0"
    union {
        struct sockaddr ifr_addr;      // Address
        struct sockaddr ifr_dstaddr;  // Other end of point-to-point link
        struct sockaddr ifr_broadaddr;// Broadcast address
        struct sockaddr ifr_netmask;  // Netmask
        short ifr_flags;              // Flags
        int ifr_metric;               // Metric
        int ifr_mtu;                  // MTU
        char ifr_slave[IF_NAMESIZE];  // Slave device
        char ifr_newname[IF_NAMESIZE];// New name
        void *ifr_data;               // Data
    };
};

// Structure used in SIOCGIFCONF request
struct ifconf {
    int ifc_len; // Size of buffer
    union {
        char *ifc_buf;         // Buffer address
        struct ifreq *ifc_req; // Array of structures
    };
};

// Interface name and index mapping
struct if_nameindex {
    unsigned int if_index; // Interface index
    char *if_name;         // Interface name
};

// Function prototypes
char *strdup(const char *s);
unsigned int if_nametoindex(const char *ifname);
char *if_indextoname(unsigned int ifindex, char *ifname);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *ptr);

#ifdef __cplusplus
}
#endif

#endif // _NET_IF_H