#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define HTTP_PREFIX "http://"
#define HTTP_PREFIX_LEN 7

int http_get(const char* url, char* buffer, const size_t buf_size) {
    struct addrinfo hints, *result, *rp;
    int sfd;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    if (strncmp(url, HTTP_PREFIX, HTTP_PREFIX_LEN) == 0) {
        url += HTTP_PREFIX_LEN;
    }

    char host[256], path[256], port[10];

    char* has_colon = strchr(url, ':');
    char* has_slash = strchr(url, '/');
    int idex = has_slash ? has_slash - url : -1;

    if (has_colon) {
        if (idex != -1 && has_colon < has_slash) {
            strncpy(port, has_colon + 1, idex - (has_colon - url) - 1);
            port[idex - (has_colon - url) - 1] = '\0';
            strcpy(path, url + idex + 1);
            path[strlen(path)] = '\0';
        } else {
            strcpy(port, has_colon + 1);
        }
        strncpy(host, url, has_colon - url);
        host[has_colon - url] = '\0';
    } else {
        strcpy(port, "80");
    }

    if (getaddrinfo(host, port, &hints, &result) != 0) {
        printf("Error retrieving address information\n");
        return 1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
          continue;
        }
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
          break;
        }
        close(sfd);
    }

    if (rp == NULL) {
        printf("Error connecting to host\n");
        return 1;
    }

    freeaddrinfo(result);

    char sendline[256];
    sprintf(sendline, "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    write(sfd, sendline, strlen(sendline));

    size_t response_len = 512;
    char response[response_len];
    size_t offset = 0;
    ssize_t n;
    do {
        n = read(sfd, response + offset, response_len - offset - 1);
        if (n > 0) {
            offset += n;
        }
    } while (n != 0 && offset < response_len - 1);
    response[offset] = '\0';

    shutdown(sfd, SHUT_RDWR);
    close(sfd);

    char* body = strstr(response, "\r\n\r\n");
    if (body) {
        body += 4; // Skip the header-body separator
        size_t body_len = strlen(body);
        if (body[body_len-1] == '\n') {
            body_len--; // Remove trailing newline if present
        }
        if (body_len < buf_size) {
            strncpy(buffer, body, body_len);
            buffer[body_len] = '\0';
        } else {
            printf("Response body is too large for buffer\n");
            return 1;
        }
    } else {
        printf("Failed to parse HTTP response\n");
        return 1;
    }

    return 0;
}