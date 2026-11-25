/* Asynchronous native HTTP(S) worker transport. HTML execution is not supported.
   Included by desktop.c after libc headers and helper-name undefinitions. */
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
static pid_t web_pid = -1;
static int web_fd = -1, web_used;
static struct timespec web_started;
static char web_result[2048];

static void web_stop(void) {
    if (web_pid > 0) {
        kill(web_pid, SIGKILL);
        while (waitpid(web_pid, NULL, 0) < 0 && errno == EINTR) {}
    }
    if (web_fd >= 0) close(web_fd);
    web_pid = -1;
    web_fd = -1;
}
void platform_web_cancel(void) {
    web_stop();
    snprintf(web_result, sizeof(web_result), "Request cancelled.");
}
#ifndef MAYOS_FETCH_PATH
#define MAYOS_FETCH_PATH "/usr/bin/mayos-fetch"
#endif
int platform_web_start(const char *url) {
    web_stop(); web_used = 0;
    snprintf(web_result, sizeof(web_result), "Expected an http:// or https:// URL.");
    if (!url || strlen(url) > 600 ||
        (strncmp(url, "http://", 7) && strncmp(url, "https://", 8))) return 0;
    for (const unsigned char *c = (const unsigned char *)url; *c; c++)
        if (*c <= 32 || *c >= 127) return 0;
    int fds[2];
    if (pipe(fds)) return 0;
    web_pid = fork();
    if (!web_pid) {
        close(fds[0]); dup2(fds[1], STDOUT_FILENO); close(fds[1]);
        execl(MAYOS_FETCH_PATH, "mayos-fetch", url, (char *)NULL);
        dprintf(1, "Modern transport unavailable: mayos-fetch could not start.\n");
        _exit(1);
    }
    close(fds[1]);
    if (web_pid < 0) { close(fds[0]); return 0; }
    web_fd = fds[0];
    if (fcntl(web_fd, F_SETFL, O_NONBLOCK) < 0) {
        web_stop();
        snprintf(web_result, sizeof(web_result), "Cannot start asynchronous request.");
        return 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &web_started);
    web_result[0] = 0;
    return 1;
}
/* Returns 1 only while loading. Poll from the existing GUI event loop. */
int platform_web_poll(char *destination, int capacity) {
    if (web_fd >= 0) {
        ssize_t n = read(web_fd, web_result + web_used, sizeof(web_result) - 1 - (size_t)web_used);
        if (n > 0) { web_used += (int)n; web_result[web_used] = 0; }
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR) || web_used >= (int)sizeof(web_result) - 1)
            web_stop();
        else {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec - web_started.tv_sec > 10 ||
                (now.tv_sec - web_started.tv_sec == 10 && now.tv_nsec >= web_started.tv_nsec)) {
            web_stop();
            snprintf(web_result, sizeof(web_result), "Request timed out after 10 seconds.");
            }
        }
    }
    if (capacity > 0) snprintf(destination, (size_t)capacity, "%s", web_result);
    return web_pid > 0;
}

static int media_fd = -1;
static pid_t media_pid = -1;
static int media_percent, media_state, media_length;
static char media_line[128];

int platform_media_start(const char *url, const char *digest, unsigned bytes) {
    if (media_fd >= 0 || !url[0] || !digest[0]) return 0;
    int fds[2];
    if (pipe(fds)) return 0;
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) < 0) {
        close(fds[0]); close(fds[1]); return 0;
    }
    char size[24];
    snprintf(size, sizeof(size), "%u", bytes);
    media_pid = fork();
    if (!media_pid) {
        close(fds[0]); dup2(fds[1], STDOUT_FILENO); close(fds[1]);
        execl(MAYOS_FETCH_PATH, "mayos-fetch", "--download", url, digest, size, (char *)NULL);
        _exit(1);
    }
    close(fds[1]);
    if (media_pid < 0) { close(fds[0]); return 0; }
    media_fd = fds[0];
    media_length = media_percent = 0;
    media_state = 1;
    return 1;
}

int platform_media_poll(int *percent) {
    if (media_fd >= 0) {
        char buffer[256];
        ssize_t n = read(media_fd, buffer, sizeof(buffer));
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            close(media_fd);
            media_fd = -1;
            while (waitpid(media_pid, NULL, 0) < 0 && errno == EINTR) {}
            if (media_state == 1) media_state = -1;
        }
        for (ssize_t i = 0; i < n; i++) {
            char c = buffer[i];
            if (c == '\n') {
                media_line[media_length] = 0;
                unsigned value;
                if (sscanf(media_line, "@DOWNLOAD %u", &value) == 1 && value <= 100)
                    media_percent = (int)value;
                else if (!strncmp(media_line, "@SAVED ", 7)) media_state = 2;
                else if (!strncmp(media_line, "@FAILED ", 8)) media_state = -1;
                media_length = 0;
            } else if (media_length < (int)sizeof(media_line) - 1) media_line[media_length++] = c;
        }
    }
    *percent = media_percent;
    return media_fd >= 0 ? 1 : media_state;
}

/* Lightweight IPv4/IPv6 diagnostics for MayOS Settings, Explorer and MayShell.
   Reports actual Linux interface, DHCP-assigned address, default route and DNS.
   Not a Wi-Fi manager or custom-kernel network stack. */
int platform_network_status(char *destination, int capacity) {
    if (!destination || capacity < 16) return 0;
    char iface[32] = "none", address[INET_ADDRSTRLEN] = "unassigned";
    char gateway[INET_ADDRSTRLEN] = "none", dns[64] = "none";
    char ipv6[INET6_ADDRSTRLEN] = "unassigned";
    int has_ipv6 = 0;
    struct ifaddrs *interfaces = NULL;
    int connected = 0;
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs *a = interfaces; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET ||
                !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
            const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)a->ifa_addr;
            if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) {
                snprintf(iface, sizeof(iface), "%.31s", a->ifa_name);
                connected = 1;
                break;
            }
        }
        for (struct ifaddrs *a = interfaces; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET6 ||
                !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
            const struct sockaddr_in6 *ip6 = (const struct sockaddr_in6 *)a->ifa_addr;
            if (IN6_IS_ADDR_LINKLOCAL(&ip6->sin6_addr)) continue;
            if (connected && strcmp(a->ifa_name, iface)) continue;
            if (inet_ntop(AF_INET6, &ip6->sin6_addr, ipv6, sizeof(ipv6))) {
                has_ipv6 = 1;
                if (!connected) snprintf(iface, sizeof(iface), "%.31s", a->ifa_name);
                break;
            }
        }
        freeifaddrs(interfaces);
    }
    FILE *routes = fopen("/proc/net/route", "r");
    if (routes) {
        char row[256];
        while (fgets(row, sizeof(row), routes)) {
            char dev[32];
            unsigned long dst = 0, gw = 0, flags = 0;
            if (sscanf(row, "%31s %lx %lx %lx", dev, &dst, &gw, &flags) == 4 &&
                dst == 0 && (flags & 1) && (!connected || !strcmp(dev, iface))) {
                uint32_t route = (uint32_t)gw;
                inet_ntop(AF_INET, &route, gateway, sizeof(gateway));
                break;
            }
        }
        fclose(routes);
    }
    FILE *resolv = fopen("/etc/resolv.conf", "r");
    if (resolv) {
        char row[128];
        while (fgets(row, sizeof(row), resolv))
            if (sscanf(row, "nameserver %63s", dns) == 1) break;
        fclose(resolv);
    }
    snprintf(destination, (size_t)capacity,
             "Network: %s\nDevice: %s\nIPv4: %s\nIPv6: %s\nIPv4 gateway: %s\nDNS: %s",
             connected || has_ipv6 ? "IP assigned" : "waiting for address", iface, address, ipv6, gateway, dns);
    return connected || has_ipv6;
}
