#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../linux/network.h"

int main(void) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(bind(server, (struct sockaddr *)&address, sizeof(address)) == 0);
    assert(listen(server, 1) == 0);
    socklen_t address_length = sizeof(address);
    assert(getsockname(server, (struct sockaddr *)&address, &address_length) == 0);

    pid_t responder = fork();
    assert(responder >= 0);
    if (!responder) {
        int client = accept(server, NULL, NULL);
        assert(client >= 0);
        char request[1024] = {0};
        assert(read(client, request, sizeof(request) - 1) > 0);
        assert(strstr(request, "GET /hello HTTP/1.0\r\n"));
        const char reply[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nMayOS network works\n";
        assert(write(client, reply, sizeof(reply) - 1) == (ssize_t)sizeof(reply) - 1);
        close(client);
        close(server);
        _exit(0);
    }
    close(server);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%u/hello", ntohs(address.sin_port));
    assert(platform_web_start(url));
    char result[2048] = {0};
    for (int attempt = 0; attempt < 200 && platform_web_poll(result, sizeof(result)); attempt++)
        usleep(10000);
    platform_web_poll(result, sizeof(result));
    assert(strstr(result, "HTTP/1.0 200 OK"));
    assert(strstr(result, "MayOS network works"));
    assert(waitpid(responder, NULL, 0) == responder);

    assert(!platform_web_start("https://example.com/"));
    platform_web_poll(result, sizeof(result));
    assert(strstr(result, "Only http://"));
    assert(!platform_web_start("http://bad host/"));
    puts("PASS: asynchronous HTTP transport, response capture and URL rejection");
    return 0;
}
