/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "http.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
/* HTTP GET using libcurl */
/* Bound local API responses: a runaway /metrics endpoint must not grow this
 * root-owned GUI indefinitely. CURL_WRITEFUNC_ERROR is signalled by 0 bytes. */
/* Do not download 1 MiB just to reject it for a 4 KiB caller buffer.
 * The bounded writer fails the transfer immediately at the usable size. */
size_t http_response_limit(size_t caller_capacity) {
    if (caller_capacity < 2) return 0;
    return caller_capacity - 1 < HTTP_MAX_RESPONSE ? caller_capacity - 1 : HTTP_MAX_RESPONSE;
}


size_t http_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct http_buf *buf = userdata;
    if (!buf || (size && nmemb > SIZE_MAX / size)) return 0;
    size_t total = size * nmemb;
    if (!total) return 0;
    if (buf->size > buf->limit || total > buf->limit - buf->size) return 0;
    size_t required = buf->size + total + 1;
    if (required > buf->capacity) {
        size_t next_cap = buf->capacity ? buf->capacity : 1024;
        while (next_cap < required) {
            if (next_cap > (buf->limit + 1) / 2) { next_cap = buf->limit + 1; break; }
            next_cap *= 2;
        }
        char *next = realloc(buf->data, next_cap);
        if (!next) return 0;  /* Never lose ownership of the original allocation. */
        buf->data = next;
        buf->capacity = next_cap;
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = 0;
    return total;
}

/* Reusable CURL handle to avoid init/cleanup overhead on every poll */
static CURL *g_curl = NULL;

static CURL *get_curl_handle(void) {
    if (!g_curl) {
        g_curl = curl_easy_init();
    }
    return g_curl;
}

void cleanup_curl_handle(void) {
    if (g_curl) {
        curl_easy_cleanup(g_curl);
        g_curl = NULL;
    }
}

/* The shared CURL handle must never retain a header-list pointer between calls.
 * Treat a truncated response as a failure rather than parsing half a JSON value. */
int http_get_auth(const char *url, char *out, size_t cap, int timeout_ms, const char *bearer_token) {
    if (!url || !out || cap < 2) return -1;
    out[0] = '\0';
    CURL *curl = get_curl_handle();
    if (!curl) return -1;
    struct http_buf buf = { .data = malloc(1024), .size = 0,
                            .capacity = 1024, .limit = http_response_limit(cap) };
    if (!buf.data) return -1;
    struct curl_slist *headers = NULL;
    if (bearer_token && bearer_token[0]) {
        char auth_header[512];
        int n = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", bearer_token);
        if (n < 0 || (size_t)n >= sizeof(auth_header) ||
            !(headers = curl_slist_append(NULL, auth_header))) { free(buf.data); return -1; }
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*"); /* loopback APIs, never proxy */
    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    int ok = rc == CURLE_OK && http_code == 200 && buf.size > 0 && buf.size < cap;
    if (ok) memcpy(out, buf.data, buf.size + 1);
    /* Clear dangling pointers before freeing the per-request header list. */
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_slist_free_all(headers);
    free(buf.data);
    return ok ? 0 : -1;
}

int http_get(const char *url, char *out, size_t cap, int timeout_ms) {
    return http_get_auth(url, out, cap, timeout_ms, NULL);
}

/* Check if a TCP port is listening */
int check_port(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return rc == 0;
}
