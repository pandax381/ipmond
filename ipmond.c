#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_arp.h>

#define PACKAGE_NAME "ipmond" 
#define PACKAGE_VERSION "0.1" 

#define DEBUG(fmt, ...) \
    do { if (debug) fprintf(stderr, "DEBUG: " fmt, ##__VA_ARGS__); } while (0)

#define ISIFUP(x) ((x) & IFF_RUNNING)
#define IFSTATE(x) (ISIFUP(x) ? "UP" : "DOWN")

struct addr {
    struct addr *next;
    unsigned char family;
    unsigned char prefixlen;
    unsigned char flags;
    unsigned char scope;
    struct in_addr addr;
};

struct cache {
    struct cache *next;
    int index;
    unsigned int flags;
    char ifname[IFNAMSIZ];
    struct addr *addrs;
};

static unsigned char debug;
static struct cache *root;
static volatile sig_atomic_t terminate;

static struct cache *
cache_get (int index) {
    struct cache *c;

    for (c = root; c; c = c->next) {
        if (c->index == index) {
            break;
        }
    }
    return c;
}

static struct cache *
cache_add (struct ifinfomsg *msg, char *ifname) {
    struct cache *c;

    c = malloc(sizeof(struct cache));
    if (!c) {
        fprintf(stderr, "malloc: failure\n");
        return NULL;
    }
    c->next = root;
    c->index = msg->ifi_index;
    c->flags = msg->ifi_flags;
    strcpy(c->ifname, ifname);
    c->addrs = NULL;
    root = c;
    return c;
}

static int
cache_del (struct cache *c) {
    struct cache *p;

    if (root == c) {
        root = c->next;
        return 0;
    }
    for (p = root; p; p = p->next) {
        if (p->next == c) {
            p->next = c->next;
            return 0;
        }
    }
    return -1;
}
static struct addr *
addr_get (struct cache *c, struct ifaddrmsg *msg, struct in_addr *la) {
    struct addr *a;

    for (a = c->addrs; a; a = a->next) {
        if (a->family == msg->ifa_family && a->prefixlen == msg->ifa_prefixlen && a->scope == msg->ifa_scope) {
            if (memcmp(&a->addr, (struct in_addr *)la, sizeof(struct in_addr)) == 0) {
                break;
            }
        }
    }
    return a;
}

static struct addr *
addr_add (struct cache *c, struct ifaddrmsg *msg, struct in_addr *la) {
    struct addr *a;

    a = malloc(sizeof(struct addr));
    if (!a) {
        fprintf(stderr, "malloc: failure\n");
        return NULL;
        return NULL;
    }
    a->next = c->addrs;
    a->family = msg->ifa_family;
    a->prefixlen = msg->ifa_prefixlen;
    a->scope = msg->ifa_scope;
    a->addr = *(struct in_addr *)la;
    c->addrs = a;
    return a;
}

static int
addr_del (struct cache *c, struct addr *a) {
    struct addr *p;

    if (c->addrs == a) {
        c->addrs = a->next;
        return 0;
    }
    for (p = c->addrs; p; p = p->next) {
        if (p->next == a) {
            p->next = a->next;
            return 0;
        }
    }
    return -1;
}

static int
process_rtm_link (struct nlmsghdr *hdr) {
    struct ifinfomsg *msg;
    size_t n;
    struct rtattr *rta;
    char *ifname = NULL;
    struct cache *c;

    msg = NLMSG_DATA(hdr);
    n = IFLA_PAYLOAD(hdr);
    for (rta = IFLA_RTA(msg); RTA_OK(rta, n); rta = RTA_NEXT(rta, n)) {
        if (rta->rta_type == IFLA_IFNAME) {
            ifname = (char *)RTA_DATA(rta);
            break;
        }
    }
    if (!ifname) {
        DEBUG("Unknown interface name for index '%d'\n", msg->ifi_index);
        ifname = "<unknown>";
    }
    c = cache_get(msg->ifi_index);
    if (!c) {
        if (hdr->nlmsg_type == RTM_DELLINK) {
            return -1;
        }
        c = cache_add(msg, ifname);
        if (!c) {
            return -1;
        }
        DEBUG("%d (%s): Detect New LINK\n", c->index, c->ifname);
        return 0;
    }
    if (strcmp(c->ifname, ifname) != 0) {
        DEBUG("%d (%s): Interface name has been changed to '%s'\n", c->index, c->ifname, ifname);
        strcpy(c->ifname, ifname);
    }
    if (ISIFUP(c->flags) != ISIFUP(msg->ifi_flags)) {
        fprintf(stderr, "%d (%s): Link is %s\n", c->index, c->ifname, IFSTATE(msg->ifi_flags));
    }
    c->flags = msg->ifi_flags;
    if (hdr->nlmsg_type == RTM_DELLINK) {
        DEBUG("%d (%s): Delete LINK\n", c->index, c->ifname);
        cache_del(c);
        if (!root) {
            DEBUG("No Interfaces\n");
        }
    }
    return 0;
}

static int
process_rtm_addr (struct nlmsghdr *hdr) {
    struct ifaddrmsg *msg;
    size_t n;
    struct rtattr *rta;
    struct in_addr *la = NULL;
    char local[INET_ADDRSTRLEN];
    struct cache *c;
    struct addr *a;

    msg = NLMSG_DATA(hdr);
    for (rta = IFA_RTA(msg), n = IFA_PAYLOAD(hdr); RTA_OK(rta, n); rta = RTA_NEXT(rta, n)) {
        if (rta->rta_type == IFA_LOCAL) {
            la = RTA_DATA(rta);
            break;
        }
    }
    if (!la) {
        return -1;
    }
    c = cache_get(msg->ifa_index);
    if (!c) {
        return -1;
    }
    a = addr_get(c, msg, la);
    if (hdr->nlmsg_type == RTM_NEWADDR) {
        if (a) {
            inet_ntop(a->family, &a->addr, local, sizeof(local));
            DEBUG("%d (%s): Address already exists '%s/%u'\n", c->index, c->ifname, local, a->prefixlen);
            return 0;
        }
        a = addr_add(c, msg, la);
        if (!a) {
            return -1;
        }
    } else { /* RTM_DELADDR */
        if (!a) {
            inet_ntop(a->family, la, local, sizeof(local));
            DEBUG("%d (%s): Address not exists '%s/%u'\n", c->index, c->ifname, local, msg->ifa_prefixlen);
            return 0;
        }
    }
    fprintf(stderr, "%d (%s): %s %s/%u\n",
        c->index,c->ifname, (hdr->nlmsg_type == RTM_NEWADDR) ? "NEWADDR" : "DELADDR", inet_ntop(a->family, &a->addr, local, sizeof(local)), a->prefixlen);
    if (hdr->nlmsg_type == RTM_DELADDR) {
        addr_del(c, a);
        if (!c->addrs) {
            DEBUG("%d (%s): Interface has no address\n", c->index, c->ifname);
        }
    }
    return 0;
}

static int
cache_getaddr (void) {
    int soc, done = 0;
    struct {
        struct nlmsghdr hdr;
        struct ifaddrmsg msg;
    } req;
    char buf[4096];
    ssize_t n;
    struct nlmsghdr *hdr;
    struct ifaddrmsg *msg;
    unsigned int msglen;
    struct rtattr *rta;
    void *la = NULL;
    char local[INET_ADDRSTRLEN];
    struct cache *c;
    struct addr *a;

    soc = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (soc == -1) {
        perror("socket");
        return -1;
    }
    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req.hdr.nlmsg_type = RTM_GETADDR;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.msg.ifa_family = AF_INET;
    n = send(soc, &req, sizeof(req), 0);
    if (n == -1) {
        perror("send");
        close(soc);
        return -1;
    }
    while (!done && !terminate) {
        n = recv(soc, buf, sizeof(buf), 0);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            close(soc);
            return -1;
        }
        if (!n) {
            fprintf(stderr, "EOF\n");
            break;
        }
        for (hdr = (struct nlmsghdr *)buf; NLMSG_OK(hdr, n); hdr = NLMSG_NEXT(hdr, n)) {
            if (hdr->nlmsg_type == NLMSG_DONE) {
                done = 1;
                break;
            }
            msg = NLMSG_DATA(hdr);
            msglen = IFA_PAYLOAD(hdr);
            for (rta = IFA_RTA(msg); RTA_OK(rta, msglen); rta = RTA_NEXT(rta, msglen)) {
                if (rta->rta_type == IFA_LOCAL) {
                    la = RTA_DATA(rta);
                    break;
                }
            }
            c = cache_get(msg->ifa_index);
            if (!c) {
                DEBUG("Interface not found '%d'\n", msg->ifa_index);
                continue;
            }
            a = addr_get(c, msg, la);
            if (a) {
                inet_ntop(a->family, &a->addr, local, sizeof(local));
                DEBUG("%d (%s): Address already exists '%s/%u'\n", c->index, c->ifname, local, a->prefixlen);
                continue;
            }
            a = addr_add(c, msg, la);
            if (!a) {
                continue;
            }
            DEBUG("%d (%s): NEWADDR %s/%u\n",
                c->index,c->ifname, inet_ntop(a->family, &a->addr, local, sizeof(local)), a->prefixlen);
        }
    }
    close(soc);
    return 0;
}

static int
cache_getlink (void) {
    int soc, done = 0;
    struct {
        struct nlmsghdr hdr;
        struct ifinfomsg msg;
    } req;
    char buf[4096];
    ssize_t n;
    struct nlmsghdr *hdr;
    struct ifinfomsg *msg;
    unsigned int msglen;
    struct rtattr *rta;
    char *ifname = NULL;
    struct cache *c;

    soc = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (soc == -1) {
        perror("socket");
        return -1;
    }
    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    req.hdr.nlmsg_type = RTM_GETLINK;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 1;
    n = send(soc, &req, sizeof(req), 0);
    if (n == -1) {
        perror("send");
        close(soc);
        return -1;
    }
    while (!done && !terminate) {
        n = recv(soc, buf, sizeof(buf), 0);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            close(soc);
            return -1;
        }
        if (!n) {
            fprintf(stderr, "EOF\n");
            break;
        }
        for (hdr = (struct nlmsghdr *)buf; NLMSG_OK(hdr, n); hdr = NLMSG_NEXT(hdr, n)) {
            if (hdr->nlmsg_type == NLMSG_DONE) {
                done = 1;
                break;
            }
            msg = NLMSG_DATA(hdr);
            msglen = IFLA_PAYLOAD(hdr);
            for (rta = IFLA_RTA(msg); RTA_OK(rta, msglen); rta = RTA_NEXT(rta, msglen)) {
                if (rta->rta_type == IFLA_IFNAME) {
                    ifname = (char *)RTA_DATA(rta);
                    break;
                }
            }
            if (!ifname) {
                DEBUG("Unknown interface name for index '%d'\n", msg->ifi_index);
                ifname = "<unknown>";
            }
            c = cache_get(msg->ifi_index);
            if (c) {
                DEBUG("%d (%s): Interface already exists\n", c->index, c->ifname);
                continue;
            }
            c = cache_add(msg, ifname);
            if (!c) {
                continue;
            }
            DEBUG("%d (%s) Link is %s\n", c->index, c->ifname, IFSTATE(c->flags));
        }
    }
    close(soc);
    return 0;
}

static void
on_signal (int signum) {
    terminate = 1;
}

static void
usage (char *name) {
    fprintf(stderr, "usage: %s [options]\n", name);
    printf("  options:\n");
    printf("    -D, --debug      # debug mode\n");
    printf("    -h  --help       # show this message\n");
    printf("    -v  --version    # show version\n");
}

static void
version (void) {
    printf("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

static int
prepare (int argc, char *argv[]) {
    int opt;
    struct option long_options[] = {
        {"debug",      0, NULL, 'D'},
        {"help",       0, NULL, 'h' },
        {"version",    0, NULL, 'V' },
        { NULL,        0, NULL,  0 }
    };
    struct sigaction sig;

    while ((opt = getopt_long_only(argc, argv, "DhV", long_options, NULL)) != -1) {
        switch (opt) {
        case 'D':
            debug = 1;
            break;
        case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);
        case 'V':
            version();
            exit(EXIT_SUCCESS);
        default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    if (optind != argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    memset(&sig, 0, sizeof(sig));
    sig.sa_handler = on_signal;
    if (sigaction(SIGINT, &sig, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    if (sigaction(SIGTERM, &sig, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    if (cache_getlink() == -1) {
        return -1;
    }
    if (cache_getaddr() == -1) {
        return -1;
    }
    return 0;
}

int
main (int argc, char *argv[]) {
    int soc;
    struct sockaddr_nl sa;
    char buf[4096];
    ssize_t n;
    struct nlmsghdr *hdr;

    if (prepare(argc, argv) == -1) {
        return -1;
    }
    soc = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (soc == -1) {
        perror("socket");
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = 0;
    sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;
    if (bind(soc, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        perror("bind");
        close(soc);
        return -1;
    }
    while (!terminate) {
        n = recv(soc, buf, sizeof(buf), 0);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            close(soc);
            return -1;
        }
        if (!n) {
            fprintf(stderr, "EOF\n");
            break;
        }
        for (hdr = (struct nlmsghdr *)buf; NLMSG_OK(hdr, n); hdr = NLMSG_NEXT(hdr, n)) {
            switch (hdr->nlmsg_type) {
            case RTM_NEWLINK:
            case RTM_DELLINK:
                process_rtm_link(hdr);
                break;
            case RTM_NEWADDR:
            case RTM_DELADDR:
                process_rtm_addr(hdr);
                break;
            default:
                fprintf(stderr, "Unknown Type (%u)\n", hdr->nlmsg_type);
                break;
            }
        }
    }
    close(soc);
    return 0;
}
