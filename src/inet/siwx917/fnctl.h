#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h> // For mode_t, off_t

#ifdef __cplusplus
extern "C" {
#endif

// File access modes
#define O_RDONLY  0x0000 // Open for reading only
#define O_WRONLY  0x0001 // Open for writing only
#define O_RDWR    0x0002 // Open for reading and writing

// File creation flags
#define O_CREAT   0x0100 // Create file if it does not exist
#define O_EXCL    0x0200 // Exclusive use flag
#define O_NOCTTY  0x0400 // Do not assign controlling terminal
#define O_TRUNC   0x0800 // Truncate flag

// File status flags
#define O_APPEND  0x1000 // Set append mode
#define O_NONBLOCK 0x2000 // Non-blocking mode
#define O_SYNC    0x4000 // Synchronous writes

// File descriptor flags
#define FD_CLOEXEC 1 // Close on exec flag

// Commands for fcntl()
#define F_DUPFD    0 // Duplicate file descriptor
#define F_GETFD    1 // Get file descriptor flags
#define F_SETFD    2 // Set file descriptor flags
#define F_GETFL    3 // Get file status flags
#define F_SETFL    4 // Set file status flags

// Function prototypes
int open(const char *pathname, int flags, ...);
int creat(const char *pathname, mode_t mode);
int fcntl(int fd, int cmd, ...);
int close(int fd);

#ifdef __cplusplus
}
#endif

#endif // _FCNTL_H