#ifndef IFADDRS_H
#define IFADDRS_H

#include <socket.h>
#include <netinet_in.h>
#include <string.h>

struct ifaddrs
{
    struct ifaddrs *ifa_next;       // Pointer to the next interface
    char *ifa_name;                // Interface name
    unsigned int ifa_flags;        // Interface flags
    struct sockaddr *ifa_addr;     // Address of the interface
    struct sockaddr *ifa_netmask;  // Netmask of the interface
};

// Hardcoded function to return a single interface
static inline int getifaddrs(struct ifaddrs **ifap)
{
    static struct sockaddr_in addr;
    static struct sockaddr_in netmask;
    static struct ifaddrs ifa;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0xC0A80001); // 192.168.0.1

    netmask.sin_family = AF_INET;
    netmask.sin_addr.s_addr = htonl(0xFFFFFF00); // 255.255.255.0

    ifa.ifa_next = NULL;
    ifa.ifa_name = (char *)"eth0";
    ifa.ifa_flags = 0;
    ifa.ifa_addr = (struct sockaddr *)&addr;
    ifa.ifa_netmask = (struct sockaddr *)&netmask;

    *ifap = &ifa;
    return 0;
}

// Hardcoded function to free the interface list
static inline void freeifaddrs(struct ifaddrs *ifa)
{
    // No-op for hardcoded implementation
}

#endif // IFADDRS_H