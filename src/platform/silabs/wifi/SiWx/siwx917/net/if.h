#ifndef NET_IF_H
#define NET_IF_H

#include <stdlib.h>
#include <string.h>

#define IF_NAMESIZE 16
#define IFF_UP 0x1
#define IFF_BROADCAST 0x2
#define IFF_RUNNING 0x40
#define IFF_LOOPBACK 0x8
#define IFF_MULTICAST 0x1000

#define DEFAULT_INTERFACE_NAME "st0"
// Structure for if_nameindex
struct if_nameindex
{
    unsigned int if_index; // Interface index
    char * if_name;        // Interface name
};

// Hardcoded function to get the interface name from an index
static inline char * if_indextoname(unsigned int ifindex, char * ifname)
{
    if (ifindex == 1)
    {
        strncpy(ifname, DEFAULT_INTERFACE_NAME, IF_NAMESIZE);
        return ifname;
    }
    return NULL;
}

// Hardcoded function to get the interface index from a name
static inline unsigned int if_nametoindex(const char * ifname)
{
    if (strcmp(ifname, DEFAULT_INTERFACE_NAME) == 0)
    {
        return 1;
    }
    return 0;
}

// Hardcoded function to return a list of interfaces
static inline struct if_nameindex * if_nameindex(void)
{
    struct if_nameindex * list = (struct if_nameindex *) malloc(2 * sizeof(struct if_nameindex));
    if (list == NULL)
    {
        return NULL;
    }

    // Hardcoded single interface
    list[0].if_index = 1;
    list[0].if_name  = strdup(DEFAULT_INTERFACE_NAME); // Allocate memory for the name
    list[1].if_index = 0;                              // End of list
    list[1].if_name  = NULL;

    return list;
}

// Free the memory allocated by if_nameindex
static inline void if_freenameindex(struct if_nameindex * list)
{
    if (list != NULL)
    {
        for (struct if_nameindex * entry = list; entry->if_name != NULL; ++entry)
        {
            free(entry->if_name); // Free the allocated name
        }
        free(list); // Free the list itself
    }
}

#endif // NET_IF_H
