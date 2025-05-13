#ifndef _IOCTL_H
#define _IOCTL_H

#include <sys/types.h> // For basic types

#ifdef __cplusplus
extern "C" {
#endif

// Macro to create ioctl request codes
#define _IO(type, nr)           ((type << 8) | nr)
#define _IOR(type, nr, size)    ((type << 8) | nr)
#define _IOW(type, nr, size)    ((type << 8) | nr)
#define _IOWR(type, nr, size)   ((type << 8) | nr)

// Common ioctl request codes
#define SIOCGIFFLAGS 0x8913 // Get interface flags
#define SIOCSIFFLAGS 0x8914 // Set interface flags
#define SIOCGIFADDR  0x8915 // Get interface address
#define SIOCSIFADDR  0x8916 // Set interface address
#define SIOCGIFMTU   0x8921 // Get MTU size
#define SIOCSIFMTU   0x8922 // Set MTU size

// Function prototype for ioctl
int ioctl(int fd, unsigned long request, void *arg);

#ifdef __cplusplus
}
#endif

#endif // _IOCTL_H