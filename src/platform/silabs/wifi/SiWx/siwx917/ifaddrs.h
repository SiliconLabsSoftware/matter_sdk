#ifndef _IFADDRS_H
#define _IFADDRS_H

#include <netinet_in.h> // For sockaddr_in and sockaddr_in6
#include <socket.h>     // For sockaddr
#include <stdint.h>     // For standard integer types

#ifdef __cplusplus
extern "C" {
#endif

// Structure describing a network interface address
struct ifaddrs
{
    struct ifaddrs * ifa_next;       // Pointer to the next structure in the list
    char * ifa_name;                 // Name of the interface
    unsigned int ifa_flags;          // Interface flags (e.g., IFF_UP, IFF_LOOPBACK)
    struct sockaddr * ifa_addr;      // Address of the interface
    struct sockaddr * ifa_netmask;   // Netmask of the interface
    struct sockaddr * ifa_broadaddr; // Broadcast address (if applicable)
    struct sockaddr * ifa_dstaddr;   // Destination address (for point-to-point interfaces)
    void * ifa_data;                 // Address-specific data
};

// Function prototypes
int getifaddrs(struct ifaddrs ** ifap);
void freeifaddrs(struct ifaddrs * ifa);

#ifdef __cplusplus
}
#endif

#endif // _IFADDRS_H
