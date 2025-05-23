#ifndef SYS_IOCTL_H
#define SYS_IOCTL_H

#include <net/if.h>
#include <string.h>

#define SIOCGIFFLAGS 0x8913 // Get interface flags

struct ifreq
{
    char ifr_name[IF_NAMESIZE]; // Interface name
    short ifr_flags;            // Interface flags
};

// Hardcoded ioctl function to handle interface flags
static inline int ioctl(int fd, unsigned long request, struct ifreq * ifr)
{
    if (request == SIOCGIFFLAGS && strcmp(ifr->ifr_name, DEFAULT_INTERFACE_NAME) == 0)
    {
        ifr->ifr_flags = IFF_UP | IFF_RUNNING | IFF_MULTICAST;
        return 0;
    }
    return -1;
}

#endif // SYS_IOCTL_H
