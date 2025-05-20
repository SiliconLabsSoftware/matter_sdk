#include "if.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
// #include <unistd.h>
// #include <ioctl.h>
#include <net/if.h>

#ifndef HAVE_STRDUP
char *strdup(const char *s)
{
    if (s == NULL)
    {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy != NULL)
    {
        memcpy(copy, s, len);
    }
    return copy;
}
#endif

// Convert an interface name to an index
unsigned int if_nametoindex(const char *ifname)
{
#if 0
    struct ifaddrs *ifaddr, *ifa;
    unsigned int index = 0;

    if (getifaddrs(&ifaddr) == -1)
    {
        return 0; // Failed to retrieve interfaces
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_name != NULL && strcmp(ifa->ifa_name, ifname) == 0)
        {
            index = if_nametoindex(ifa->ifa_name);
            break;
        }
    }

    freeifaddrs(ifaddr);
#endif
    // return index;
    return 0;
}

// Convert an interface index to a name
char *if_indextoname(unsigned int ifindex, char *ifname)
{
    if (ifname == NULL)
    {
        return NULL; // Invalid buffer
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        return NULL; // Failed to open socket
    }

    struct ifreq ifr;
    // ifr.ifr_ifindex = ifindex;

    // if (ioctl(sock, SIOCGIFNAME, &ifr) < 0)
    {
        close(sock);
        return NULL; // Failed to retrieve name
    }

    close(sock);
    strncpy(ifname, ifr.ifr_name, IF_NAMESIZE - 1);
    ifname[IF_NAMESIZE - 1] = '\0';
    return ifname;
}

// Return a list of all interfaces and their indices
struct if_nameindex *if_nameindex(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        return NULL; // Failed to open socket
    }

    struct ifconf ifc;
    char buffer[4096];
    ifc.ifc_len = sizeof(buffer);
    ifc.ifc_buf = buffer;

    // Retrieve the list of interfaces
    // if (ioctl(sock, SIOCGIFCONF, &ifc) < 0)
    {
        close(sock);
        return NULL; // Failed to retrieve interface list
    }

    struct ifreq *ifr = ifc.ifc_req;
    int numInterfaces = ifc.ifc_len / sizeof(struct ifreq);

    struct if_nameindex *list = (struct if_nameindex *)malloc((numInterfaces + 1) * sizeof(struct if_nameindex));
    if (list == NULL)
    {
        close(sock);
        return NULL; // Memory allocation failed
    }

    for (int i = 0; i < numInterfaces; i++)
    {
        list[i].if_index = if_nametoindex(ifr[i].ifr_name);
        list[i].if_name = strdup(ifr[i].ifr_name);
        if (list[i].if_name == NULL)
        {
            // Free previously allocated memory on failure
            for (int j = 0; j < i; j++)
            {
                free(list[j].if_name);
            }
            free(list);
            close(sock);
            return NULL;
        }
    }

    // Null-terminate the list
    list[numInterfaces].if_index = 0;
    list[numInterfaces].if_name = NULL;

    close(sock);
    return list;
}

// Free the data returned from if_nameindex
void if_freenameindex(struct if_nameindex *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    for (struct if_nameindex *entry = ptr; entry->if_name != NULL; entry++)
    {
        free(entry->if_name);
    }

    free(ptr);
}