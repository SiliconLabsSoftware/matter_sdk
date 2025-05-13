#include "PlatformInterface.h"
#include "socket.h"
#include <cstring>

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    // if (af == AF_INET4) {
    //     return sl_inet_ntop4((const unsigned char *)src, dst, size);
    // }
    if (af == AF_INET6) {
        return sl_inet_ntop6((const unsigned char *)src, dst, size);
    }
    // Add support for AF_INET (IPv4) if needed
    return NULL; // Return NULL for unsupported address families
}

/*static inline*/ int inet_pton(int af, const char *src, void *dst) {
    if (af == AF_INET6) {
        unsigned int result[4]; // Buffer to store the converted IPv6 address in big-endian format
        const char *src_endp = src + strlen(src); // Calculate the end pointer of the source string
        return sl_inet_pton6(src, src_endp, (unsigned char *)dst, result);
    } else if (af == AF_INET) {
        // Use the standard inet_pton for IPv4
        struct in_addr addr4;
        if (::inet_pton(AF_INET, src, &addr4) == 1) {
            memcpy(dst, &addr4, sizeof(addr4));
            return 1; // Success
        }
    }
    // Add support for AF_INET (IPv4) if needed
    return -1; // Return -1 for unsupported address families
}

/*static inline*/ ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    // Ensure the message has exactly one iovec (scatter-gather array)
    if (msg->msg_iovlen != 1) {
        errno = EINVAL; // Invalid argument
        return -1;
    }

    // Use sendto to send the data
    return sendto(sockfd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len, flags,
                  (const struct sockaddr *)msg->msg_name, msg->msg_namelen);
}

/*static inline*/ ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    // Ensure the message has exactly one iovec (scatter-gather array)
    if (msg->msg_iovlen != 1) {
        errno = EINVAL; // Invalid argument
        return -1;
    }

    // Use recvfrom to receive the data
    return recvfrom(sockfd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len, flags,
                    (struct sockaddr *)msg->msg_name, &msg->msg_namelen);
}

int sl_close(int a) {
    return close(a);
}

int sl_bind(int socket_id, const struct sockaddr *addr, socklen_t addr_len) {
    return bind(socket_id, addr, addr_len);
}