// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Applies the IPv4 configuration owned by the SiWx917 Matter stack to the
 * si91x_rcp_eth interface so the host shares the device addresses.
 *
 * The kernel driver publishes the 917 state in /sys/class/net/<if>/rcp_status
 * and emits a KOBJ_CHANGE uevent (SI91X_RCP=status) on every update.
 * IPv6 addresses are not pushed: with the shared MAC, EUI-64 SLAAC yields the
 * same addresses as lwIP, so only the related sysctls are adjusted here.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SYSFS_NET "/sys/class/net"
#define STATUS_ATTR "rcp_status"
#define RESOLVED_DIR "/run/systemd/resolve"
#define FALLBACK_RESOLV_DIR "/run/si91x-rcp"
#define FALLBACK_RESOLV_FILE FALLBACK_RESOLV_DIR "/resolv.conf"
#define DEFAULT_ROUTE_METRIC 600u
#define UEVENT_BUF_SIZE 8192
#define NL_BUF_SIZE 4096

struct rcp_state
{
    bool link;
    struct in_addr addr;
    struct in_addr mask;
    struct in_addr gw;
    struct in_addr dns;
};

struct nl_req
{
    struct nlmsghdr nh;
    union
    {
        struct ifinfomsg ifi;
        struct ifaddrmsg ifa;
        struct rtmsg rt;
    };
    char attrs[256];
};

static char g_ifname[IF_NAMESIZE];
static unsigned int g_ifindex;
static bool g_fixed_ifname;
static unsigned int g_metric = DEFAULT_ROUTE_METRIC;
static struct rcp_state g_applied;
static bool g_have_applied;
static int g_nl_fd = -1;
static uint32_t g_nl_seq;
static volatile sig_atomic_t g_stop;

static void log_msg(const char * fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_msg(const char * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---------------------------------------------------------------------------
 * rtnetlink
 * ------------------------------------------------------------------------- */

static int nl_add_attr(struct nlmsghdr * nh, size_t max_len, unsigned short type, const void * data, size_t len)
{
    size_t rta_len = RTA_LENGTH(len);
    size_t offset  = NLMSG_ALIGN(nh->nlmsg_len);

    if (offset + RTA_ALIGN(rta_len) > max_len)
    {
        return -ENOSPC;
    }

    struct rtattr * rta = (struct rtattr *) ((char *) nh + offset);
    rta->rta_type       = type;
    rta->rta_len        = (unsigned short) rta_len;
    memcpy(RTA_DATA(rta), data, len);
    nh->nlmsg_len = (uint32_t) (offset + RTA_ALIGN(rta_len));
    return 0;
}

// Sends a request and waits for its ACK. Returns 0 or a negative errno.
static int nl_talk(struct nlmsghdr * nh)
{
    char buf[NL_BUF_SIZE];

    nh->nlmsg_flags |= NLM_F_REQUEST | NLM_F_ACK;
    nh->nlmsg_seq = ++g_nl_seq;

    if (send(g_nl_fd, nh, nh->nlmsg_len, 0) < 0)
    {
        return -errno;
    }

    for (;;)
    {
        int len = (int) recv(g_nl_fd, buf, sizeof(buf), 0);
        if (len < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -errno;
        }

        for (struct nlmsghdr * rsp = (struct nlmsghdr *) buf; NLMSG_OK(rsp, len); rsp = NLMSG_NEXT(rsp, len))
        {
            if (rsp->nlmsg_seq != g_nl_seq || rsp->nlmsg_type != NLMSG_ERROR)
            {
                continue;
            }
            const struct nlmsgerr * err = NLMSG_DATA(rsp);
            return err->error;
        }
    }
}

static int link_set_up(int ifindex)
{
    struct nl_req req = { 0 };

    req.nh.nlmsg_len    = NLMSG_LENGTH(sizeof(req.ifi));
    req.nh.nlmsg_type   = RTM_NEWLINK;
    req.ifi.ifi_family  = AF_UNSPEC;
    req.ifi.ifi_index   = ifindex;
    req.ifi.ifi_flags   = IFF_UP;
    req.ifi.ifi_change  = IFF_UP;
    return nl_talk(&req.nh);
}

static int addr_op(int type, int ifindex, struct in_addr addr, struct in_addr mask)
{
    struct nl_req req = { 0 };
    uint8_t prefix    = (uint8_t) __builtin_popcount(ntohl(mask.s_addr));

    req.nh.nlmsg_len      = NLMSG_LENGTH(sizeof(req.ifa));
    req.nh.nlmsg_type     = (uint16_t) type;
    req.nh.nlmsg_flags    = (type == RTM_NEWADDR) ? (NLM_F_CREATE | NLM_F_REPLACE) : 0;
    req.ifa.ifa_family    = AF_INET;
    req.ifa.ifa_prefixlen = prefix;
    req.ifa.ifa_scope     = RT_SCOPE_UNIVERSE;
    req.ifa.ifa_index     = (uint32_t) ifindex;

    nl_add_attr(&req.nh, sizeof(req), IFA_LOCAL, &addr, sizeof(addr));
    nl_add_attr(&req.nh, sizeof(req), IFA_ADDRESS, &addr, sizeof(addr));
    if (type == RTM_NEWADDR && prefix < 31)
    {
        struct in_addr brd = { .s_addr = addr.s_addr | ~mask.s_addr };
        nl_add_attr(&req.nh, sizeof(req), IFA_BROADCAST, &brd, sizeof(brd));
    }
    return nl_talk(&req.nh);
}

static int default_route_op(int type, int ifindex, struct in_addr gw)
{
    struct nl_req req = { 0 };
    uint32_t oif      = (uint32_t) ifindex;

    req.nh.nlmsg_len    = NLMSG_LENGTH(sizeof(req.rt));
    req.nh.nlmsg_type   = (uint16_t) type;
    req.nh.nlmsg_flags  = (type == RTM_NEWROUTE) ? (NLM_F_CREATE | NLM_F_REPLACE) : 0;
    req.rt.rtm_family   = AF_INET;
    req.rt.rtm_table    = RT_TABLE_MAIN;
    req.rt.rtm_protocol = RTPROT_STATIC;
    req.rt.rtm_scope    = RT_SCOPE_UNIVERSE;
    req.rt.rtm_type     = RTN_UNICAST;

    nl_add_attr(&req.nh, sizeof(req), RTA_GATEWAY, &gw, sizeof(gw));
    nl_add_attr(&req.nh, sizeof(req), RTA_OIF, &oif, sizeof(oif));
    nl_add_attr(&req.nh, sizeof(req), RTA_PRIORITY, &g_metric, sizeof(g_metric));
    return nl_talk(&req.nh);
}

/* ---------------------------------------------------------------------------
 * System helpers
 * ------------------------------------------------------------------------- */

static void write_sysctl(const char * family, const char * key, const char * value)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/sys/net/%s/conf/%s/%s", family, g_ifname, key);

    FILE * f = fopen(path, "w");
    if (f == NULL)
    {
        log_msg("cannot open %s: %s", path, strerror(errno));
        return;
    }
    fputs(value, f);
    fclose(f);
}

static int run_cmd(char * const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
    {
        return -errno;
    }
    if (pid == 0)
    {
        execvp(argv[0], argv);
        _exit(127);
    }

    int wstatus;
    while (waitpid(pid, &wstatus, 0) < 0)
    {
        if (errno != EINTR)
        {
            return -errno;
        }
    }
    return (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) ? 0 : -1;
}

static bool have_resolved(void)
{
    struct stat st;
    return stat(RESOLVED_DIR, &st) == 0 && S_ISDIR(st.st_mode);
}

static void dns_apply(struct in_addr dns)
{
    char dns_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dns, dns_str, sizeof(dns_str));

    if (have_resolved())
    {
        char * argv[] = { "resolvectl", "dns", g_ifname, dns_str, NULL };
        if (run_cmd(argv) != 0)
        {
            log_msg("resolvectl dns %s %s failed", g_ifname, dns_str);
        }
        return;
    }

    mkdir(FALLBACK_RESOLV_DIR, 0755);
    FILE * f = fopen(FALLBACK_RESOLV_FILE, "w");
    if (f == NULL)
    {
        log_msg("cannot write %s: %s", FALLBACK_RESOLV_FILE, strerror(errno));
        return;
    }
    fprintf(f, "nameserver %s\n", dns_str);
    fclose(f);
}

static void dns_revert(void)
{
    if (have_resolved())
    {
        char * argv[] = { "resolvectl", "revert", g_ifname, NULL };
        run_cmd(argv);
        return;
    }
    unlink(FALLBACK_RESOLV_FILE);
}

/* ---------------------------------------------------------------------------
 * Interface handling
 * ------------------------------------------------------------------------- */

static bool find_interface(char * name, size_t len)
{
    DIR * dir = opendir(SYSFS_NET);
    if (dir == NULL)
    {
        return false;
    }

    bool found = false;
    struct dirent * de;
    while (!found && (de = readdir(dir)) != NULL)
    {
        char path[512];
        if (de->d_name[0] == '.')
        {
            continue;
        }
        snprintf(path, sizeof(path), SYSFS_NET "/%s/" STATUS_ATTR, de->d_name);
        if (access(path, R_OK) == 0 && strlen(de->d_name) < len)
        {
            strcpy(name, de->d_name);
            found = true;
        }
    }
    closedir(dir);
    return found;
}

static bool read_status(struct rcp_state * state)
{
    char path[512];
    char line[128];

    snprintf(path, sizeof(path), SYSFS_NET "/%s/" STATUS_ATTR, g_ifname);
    FILE * f = fopen(path, "r");
    if (f == NULL)
    {
        return false;
    }

    memset(state, 0, sizeof(*state));
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char * value = strchr(line, '=');
        if (value == NULL)
        {
            continue;
        }
        *value++ = '\0';
        value[strcspn(value, "\n")] = '\0';

        if (strcmp(line, "link") == 0)
        {
            state->link = (strcmp(value, "1") == 0);
        }
        else if (strcmp(line, "ipv4") == 0)
        {
            inet_pton(AF_INET, value, &state->addr);
        }
        else if (strcmp(line, "netmask") == 0)
        {
            inet_pton(AF_INET, value, &state->mask);
        }
        else if (strcmp(line, "gateway") == 0)
        {
            inet_pton(AF_INET, value, &state->gw);
        }
        else if (strcmp(line, "dns") == 0)
        {
            inet_pton(AF_INET, value, &state->dns);
        }
    }
    fclose(f);
    return true;
}

// Must run before the carrier comes up so the shared IPv6 addresses never go through DAD.
static void prepare_interface(void)
{
    write_sysctl("ipv6", "accept_dad", "0");
    write_sysctl("ipv6", "dad_transmits", "0");
    write_sysctl("ipv6", "use_tempaddr", "0");
    write_sysctl("ipv6", "addr_gen_mode", "0");
    write_sysctl("ipv6", "accept_ra", "1");

    int ifindex = (int) if_nametoindex(g_ifname);
    if (ifindex > 0)
    {
        int ret = link_set_up(ifindex);
        if (ret < 0)
        {
            log_msg("%s: cannot bring link up: %s", g_ifname, strerror(-ret));
        }
    }
}

static void remove_applied(int ifindex)
{
    if (!g_have_applied)
    {
        return;
    }
    if (g_applied.gw.s_addr != 0)
    {
        default_route_op(RTM_DELROUTE, ifindex, g_applied.gw);
    }
    addr_op(RTM_DELADDR, ifindex, g_applied.addr, g_applied.mask);
    if (g_applied.dns.s_addr != 0)
    {
        dns_revert();
    }
    g_have_applied = false;
}

static bool same_config(const struct rcp_state * a, const struct rcp_state * b)
{
    return a->addr.s_addr == b->addr.s_addr && a->mask.s_addr == b->mask.s_addr && a->gw.s_addr == b->gw.s_addr &&
        a->dns.s_addr == b->dns.s_addr;
}

static void apply(const struct rcp_state * state)
{
    int ifindex = (int) if_nametoindex(g_ifname);
    if (ifindex <= 0)
    {
        g_have_applied = false;
        return;
    }

    bool want = state->link && state->addr.s_addr != 0;
    if (g_have_applied && want && same_config(&g_applied, state))
    {
        return;
    }

    remove_applied(ifindex);
    if (!want)
    {
        log_msg("%s: link down", g_ifname);
        return;
    }

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &state->addr, addr_str, sizeof(addr_str));

    int ret = addr_op(RTM_NEWADDR, ifindex, state->addr, state->mask);
    if (ret < 0)
    {
        log_msg("%s: cannot add %s: %s", g_ifname, addr_str, strerror(-ret));
        return;
    }
    if (state->gw.s_addr != 0)
    {
        ret = default_route_op(RTM_NEWROUTE, ifindex, state->gw);
        if (ret < 0)
        {
            log_msg("%s: cannot add default route: %s", g_ifname, strerror(-ret));
        }
    }
    if (state->dns.s_addr != 0)
    {
        dns_apply(state->dns);
    }

    g_applied      = *state;
    g_have_applied = true;
    log_msg("%s: link up, %s/%d", g_ifname, addr_str, __builtin_popcount(ntohl(state->mask.s_addr)));
}

static void refresh(void)
{
    if (!g_fixed_ifname)
    {
        char name[IF_NAMESIZE];
        if (!find_interface(name, sizeof(name)))
        {
            g_ifname[0]    = '\0';
            g_ifindex      = 0;
            g_have_applied = false;
            return;
        }
        strcpy(g_ifname, name);
    }

    unsigned int ifindex = if_nametoindex(g_ifname);
    if (ifindex == 0)
    {
        g_ifindex      = 0;
        g_have_applied = false;
        return;
    }
    if (ifindex != g_ifindex)
    {
        // A new or renamed netdev starts without any of our configuration.
        g_ifindex      = ifindex;
        g_have_applied = false;
        log_msg("using interface %s", g_ifname);
        prepare_interface();
    }

    struct rcp_state state;
    if (read_status(&state))
    {
        apply(&state);
    }
}

static bool uevent_is_relevant(const char * buf, size_t len)
{
    bool is_net = false;
    bool is_rcp = false;
    bool is_hotplug = false;

    for (size_t off = 0; off < len;)
    {
        const char * s = buf + off;
        size_t slen    = strnlen(s, len - off);

        if (strcmp(s, "SUBSYSTEM=net") == 0)
        {
            is_net = true;
        }
        else if (strcmp(s, "SI91X_RCP=status") == 0)
        {
            is_rcp = true;
        }
        else if (strcmp(s, "ACTION=add") == 0 || strcmp(s, "ACTION=remove") == 0 || strcmp(s, "ACTION=move") == 0)
        {
            is_hotplug = true;
        }
        off += slen + 1;
    }
    return is_net && (is_rcp || is_hotplug);
}

static int open_uevent_socket(void)
{
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (fd < 0)
    {
        return -1;
    }

    struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
    if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static int open_rtnl_socket(void)
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0)
    {
        return -1;
    }

    struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
    if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void on_signal(int sig)
{
    (void) sig;
    g_stop = 1;
}

static void usage(const char * prog)
{
    fprintf(stderr,
            "Usage: %s [-i ifname] [-m metric]\n"
            "  -i ifname  interface to manage (default: auto-detect si91x_rcp_eth)\n"
            "  -m metric  default route metric (default: %u)\n",
            prog, DEFAULT_ROUTE_METRIC);
}

int main(int argc, char ** argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "i:m:h")) != -1)
    {
        switch (opt)
        {
        case 'i':
            if (strlen(optarg) >= sizeof(g_ifname))
            {
                fprintf(stderr, "interface name too long\n");
                return EXIT_FAILURE;
            }
            strcpy(g_ifname, optarg);
            g_fixed_ifname = true;
            break;
        case 'm':
            g_metric = (unsigned int) strtoul(optarg, NULL, 10);
            break;
        default:
            usage(argv[0]);
            return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    struct sigaction sa = { .sa_handler = on_signal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    g_nl_fd       = open_rtnl_socket();
    int uevent_fd = open_uevent_socket();
    if (g_nl_fd < 0 || uevent_fd < 0)
    {
        log_msg("cannot open netlink sockets: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    refresh();

    char buf[UEVENT_BUF_SIZE];
    struct pollfd pfd = { .fd = uevent_fd, .events = POLLIN };
    while (!g_stop)
    {
        if (poll(&pfd, 1, -1) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            log_msg("poll failed: %s", strerror(errno));
            break;
        }

        ssize_t len = recv(uevent_fd, buf, sizeof(buf) - 1, 0);
        if (len <= 0)
        {
            continue;
        }
        buf[len] = '\0';
        if (uevent_is_relevant(buf, (size_t) len))
        {
            refresh();
        }
    }

    if (g_ifindex > 0)
    {
        remove_applied((int) g_ifindex);
    }
    close(uevent_fd);
    close(g_nl_fd);
    return EXIT_SUCCESS;
}
