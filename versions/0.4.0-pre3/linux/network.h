/* Minimal asynchronous HTTP/1.0 text transport. No TLS, redirects or HTML execution.
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
static time_t web_started;
static char web_result[8192];

static void web_stop(void) {
    if (web_pid > 0) {
        kill(web_pid, SIGKILL);
        while (waitpid(web_pid, NULL, 0) < 0 && errno == EINTR) {}
    }
    if (web_fd >= 0) close(web_fd);
    web_pid = -1;
    web_fd = -1;
}
static void web_child(const char *host, const char *port, const char *path) {
    struct addrinfo hints = {0}, *addresses = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &addresses)) {
        dprintf(STDOUT_FILENO, "DNS lookup failed. Check DHCP and /etc/resolv.conf.");
        _exit(1);
    }
    int sock = -1;
    for (struct addrinfo *a = addresses; a; a = a->ai_next) {
        sock = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (sock < 0) continue;
        struct timeval timeout = {.tv_sec = 5};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (!connect(sock, a->ai_addr, a->ai_addrlen)) break;
        close(sock); sock = -1;
    }
    freeaddrinfo(addresses);
    if (sock < 0) { dprintf(1, "Connection failed. Check interface and route."); _exit(1); }
    char request[1024];
    int count = snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s:%s\r\nConnection: close\r\nUser-Agent: MayOS-Explorer/0.4\r\nAccept: text/plain, text/html\r\nAccept-Encoding: identity\r\n\r\n", path, host, port);
    if (count < 0 || count >= (int)sizeof(request)) { dprintf(1, "HTTP request too long."); _exit(1); }
    int sent = 0;
    while (sent < count) {
        ssize_t n = send(sock, request + sent, (size_t)(count - sent), MSG_NOSIGNAL);
        if (n <= 0) { dprintf(1, "HTTP request failed."); _exit(1); }
        sent += (int)n;
    }
    char buffer[512];
    int total = 0;
    while (total < 7800) {
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            if (!total) dprintf(1, "Empty response or read timeout.");
            break;
        }
        if (n > 7800 - total) n = 7800 - total;
        for (ssize_t i = 0; i < n; i++)
            if ((unsigned char)buffer[i] < 32 && buffer[i] != '\n') buffer[i] = ' ';
            /* Preserve UTF-8 bytes for the native multilingual font renderer. */
        if (write(1, buffer, (size_t)n) != n) break;
        total += (int)n;
    }
    if (total >= 7800) dprintf(1, "\n[Preview truncated at 7800 bytes]");
    close(sock);
    _exit(0);
}
int platform_web_start(const char *url) {
    web_stop(); web_used = 0;
    snprintf(web_result, sizeof(web_result), "Only http:// URLs are supported. HTTPS/search engines need TLS.");
    if (strncmp(url, "http://", 7) || strlen(url) > 600) return 0;
    const char *authority = url + 7, *path = strchr(authority, '/');
    size_t length = path ? (size_t)(path - authority) : strlen(authority);
    if (!length || length >= 254) return 0;
    char host[254], port[6] = "80";
    memcpy(host, authority, length); host[length] = 0;
    char *colon = strchr(host, ':');
    if (colon) {
        *colon++ = 0;
        if (!*colon || strlen(colon) > 5) return 0;
        for (const char *c = colon; *c; c++) if (*c < '0' || *c > '9') return 0;
        long value = strtol(colon, NULL, 10);
        if (value < 1 || value > 65535) return 0;
        snprintf(port, sizeof(port), "%s", colon);
    }
    if (!host[0]) return 0;
    for (const char *c = host; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') || *c == '.' || *c == '-')) return 0;
    if (!path) path = "/";
    for (const char *c = path; *c; c++) if (*c <= 32 || (unsigned char)*c >= 127) return 0;
    int fds[2];
    if (pipe(fds)) return 0;
    web_pid = fork();
    if (!web_pid) {
        close(fds[0]); dup2(fds[1], STDOUT_FILENO); close(fds[1]);
        web_child(host, port, path);
    }
    close(fds[1]);
    if (web_pid < 0) { close(fds[0]); return 0; }
    web_fd = fds[0];
    fcntl(web_fd, F_SETFL, O_NONBLOCK);
    web_started = time(NULL);
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
        else if (time(NULL) - web_started >= 10) {
            web_stop();
            snprintf(web_result, sizeof(web_result), "Request timed out after 10 seconds.");
        }
    }
    if (capacity > 0) snprintf(destination, (size_t)capacity, "%s", web_result);
    return web_pid > 0;
}

/* Lightweight IPv4 diagnostics for MayOS Settings, Explorer and MayShell.
   Reports actual Linux interface, DHCP-assigned address, default route and DNS.
   Not a Wi-Fi manager, TLS client or custom-kernel network stack. */
int platform_network_status(char *destination, int capacity) {
    if (!destination || capacity < 16) return 0;
    char iface[32] = "none", address[INET_ADDRSTRLEN] = "unassigned";
    char gateway[INET_ADDRSTRLEN] = "none", dns[64] = "none";
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
             "Network: %s\nDevice: %s\nIPv4: %s\nGateway: %s\nDNS: %s",
             connected ? "connected" : "DHCP pending / no IPv4", iface, address, gateway, dns);
    return connected;
}
