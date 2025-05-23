#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "errno.h"
#include "sl_utility.h"
#include <socket.h> // include from wifi sdk
#ifdef __cplusplus
}
#endif

struct msghdr
{
    void * msg_name;
    socklen_t msg_namelen;
    struct iovec * msg_iov;
    int msg_iovlen;
    void * msg_control;
    socklen_t msg_controllen;
    int msg_flags;
};

#define AF_UNSPEC 0
#define AF_INET 2

#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define PF_UNSPEC AF_UNSPEC

/* Flags we can use with send and recv. */
#define MSG_PEEK 0x01 /* Peeks at an incoming message */
#define MSG_WAITALL                                                                                                                \
    0x02 /* Unimplemented: Requests that the function block until the full amount of data requested can be returned */
#define MSG_OOB                                                                                                                    \
    0x04 /* Unimplemented: Requests out-of-band data. The significance and semantics of out-of-band data are protocol-specific */
#define MSG_DONTWAIT 0x08 /* Nonblocking i/o for this operation only */
#define MSG_MORE 0x10     /* Sender will send more */
#define MSG_NOSIGNAL                                                                                                               \
    0x20 /* Unimplemented: Requests not to send the SIGPIPE signal if an attempt to send is made on a stream-oriented socket that  \
            is no longer connected. */

#ifndef CMSGHDR_DEFINED
#define CMSGHDR_DEFINED

struct cmsghdr
{
    socklen_t cmsg_len; // Length of control message, including this header
    int cmsg_level;     // Originating protocol
    int cmsg_type;      // Protocol-specific type
    // Followed by unsigned char cmsg_data[] (flexible array member)
};

#endif // CMSGHDR_DEFINED

#ifndef CMSG_FIRSTHDR
#define CMSG_FIRSTHDR(msg)                                                                                                         \
    ((msg)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *) (msg)->msg_control : (struct cmsghdr *) NULL)
#endif

#ifndef CMSG_LEN
#define CMSG_LEN(len) (sizeof(struct cmsghdr) + (len))
#endif

#ifndef CMSG_DATA
#define CMSG_DATA(cmsg) ((unsigned char *) ((cmsg) + 1))
#endif

#ifndef CMSG_SPACE
#define CMSG_SPACE(len) ((sizeof(struct cmsghdr) + ((len) + sizeof(size_t) - 1)) & ~(sizeof(size_t) - 1))
#endif

#ifndef CMSG_NXTHDR
#define CMSG_NXTHDR(msg, cmsg)                                                                                                     \
    (((cmsg) == NULL) ? CMSG_FIRSTHDR(msg)                                                                                         \
                      : ((((unsigned char *) (cmsg) + CMSG_LEN((cmsg)->cmsg_len)) >=                                               \
                          ((unsigned char *) ((msg)->msg_control) + (msg)->msg_controllen))                                        \
                             ? (struct cmsghdr *) NULL                                                                             \
                             : (struct cmsghdr *) ((unsigned char *) (cmsg) + CMSG_LEN((cmsg)->cmsg_len))))
#endif

static inline const char * inet_ntop(int af, const void * src, char * dst, socklen_t size)
{
    if (af == AF_INET6)
    {
        return sl_inet_ntop6((const unsigned char *) src, dst, size);
    }
    // Add support for AF_INET (IPv4) if needed
    return NULL; // Return NULL for unsupported address families
}

static inline int inet_pton(int af, const char * src, void * dst)
{
    if (af == AF_INET6)
    {
        unsigned int result[4];                    // Buffer to store the converted IPv6 address in big-endian format
        const char * src_endp = src + strlen(src); // Calculate the end pointer of the source string
        return sl_inet_pton6(src, src_endp, (unsigned char *) dst, result);
    }
    // Add support for AF_INET (IPv4) if needed
    return -1; // Return -1 for unsupported address families
}

static inline ssize_t sendmsg(int sockfd, const struct msghdr * msg, int flags)
{
    // Ensure the message has exactly one iovec (scatter-gather array)
    if (msg->msg_iovlen != 1)
    {
        errno = EINVAL; // Invalid argument
        return -1;
    }

    // Use sendto to send the data
    return sendto(sockfd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len, flags, (const struct sockaddr *) msg->msg_name,
                  msg->msg_namelen);
}

static inline ssize_t recvmsg(int sockfd, struct msghdr * msg, int flags)
{
    // Ensure the message has exactly one iovec (scatter-gather array)
    if (msg->msg_iovlen != 1)
    {
        errno = EINVAL; // Invalid argument
        return -1;
    }

    // Use recvfrom to receive the data
    return recvfrom(sockfd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len + 1, flags, (struct sockaddr *) msg->msg_name,
                    &msg->msg_namelen);
}
