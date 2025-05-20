#include <string.h>
#include "ifaddrs.h"
#include <stdlib.h>
#include <stdio.h>
// #include <unistd.h>
// #include <ioctl.h>
#include <net/if.h>
// #include <arpa/inet.h>

// Allocate and populate a sockaddr structure
static struct sockaddr *create_sockaddr(int family, void *addr_data)
{
    if (addr_data == NULL)
    {
        return NULL;
    }

    struct sockaddr *addr = NULL;
    if (family == AF_INET)
    {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
        if (addr_in == NULL)
        {
            return NULL;
        }
        addr_in->sin_family = AF_INET;
        memcpy(&addr_in->sin_addr, addr_data, sizeof(struct in_addr));
        addr = (struct sockaddr *)addr_in;
    }
    else if (family == AF_INET6)
    {
        struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)malloc(sizeof(struct sockaddr_in6));
        if (addr_in6 == NULL)
        {
            return NULL;
        }
        addr_in6->sin6_family = AF_INET6;
        memcpy(&addr_in6->sin6_addr, addr_data, sizeof(struct in6_addr));
        addr = (struct sockaddr *)addr_in6;
    }

    return addr;
}

// Populate the ifaddrs linked list with real data
int getifaddrs(struct ifaddrs **ifap)
{
    if (ifap == NULL)
    {
        return -1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        return -1; // Failed to open socket
    }

    struct ifconf ifc;
    char buffer[4096];
    ifc.ifc_len = sizeof(buffer);
    ifc.ifc_buf = buffer;

    // Retrieve the list of interfaces
    // if (ioctl(sock, SIOCGIFCONF, &ifc) < 0)
    // {
    //     close(sock);
    //     return -1; // Failed to retrieve interface list
    // }

    struct ifreq *ifr = ifc.ifc_req;
    int numInterfaces = ifc.ifc_len / sizeof(struct ifreq);

    struct ifaddrs *head = NULL;
    struct ifaddrs *prev = NULL;

    for (int i = 0; i < numInterfaces; i++)
    {
        struct ifreq *currentIfreq = &ifr[i];

        struct ifaddrs *ifa = (struct ifaddrs *)malloc(sizeof(struct ifaddrs));
        if (ifa == NULL)
        {
            freeifaddrs(head);
            close(sock);
            return -1;
        }

        memset(ifa, 0, sizeof(struct ifaddrs));
        ifa->ifa_name = strdup(currentIfreq->ifr_name);
        ifa->ifa_flags = 0; // Flags can be retrieved using SIOCGIFFLAGS if needed

        // Populate IPv4 address
        if (currentIfreq->ifr_addr.sa_family == AF_INET)
        {
            ifa->ifa_addr = create_sockaddr(AF_INET, &((struct sockaddr_in *)&currentIfreq->ifr_addr)->sin_addr);
        }
        // Populate IPv6 address (if available)
        else if (currentIfreq->ifr_addr.sa_family == AF_INET6)
        {
            ifa->ifa_addr = create_sockaddr(AF_INET6, &((struct sockaddr_in6 *)&currentIfreq->ifr_addr)->sin6_addr);
        }

        if (prev == NULL)
        {
            head = ifa;
        }
        else
        {
            prev->ifa_next = ifa;
        }
        prev = ifa;
    }

    close(sock);
    *ifap = head;
    return 0;
}

// Free the memory allocated by getifaddrs
void freeifaddrs(struct ifaddrs *ifa)
{
    while (ifa != NULL)
    {
        struct ifaddrs *next = ifa->ifa_next;

        free(ifa->ifa_name);
        free(ifa->ifa_addr);
        free(ifa->ifa_netmask);
        free(ifa);

        ifa = next;
    }
}