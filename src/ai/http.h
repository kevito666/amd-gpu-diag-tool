/* SPDX-License-Identifier: GPL-3.0-or-later
 * Local-only, bounded AI backend HTTP client (not used for remote URLs).
 */
#ifndef AMD_DIAG_AI_HTTP_H
#define AMD_DIAG_AI_HTTP_H
#include <stddef.h>
#define HTTP_MAX_RESPONSE (1024u * 1024u)
/* Writer helpers public for the resource-limit regression fixture. */
struct http_buf { char *data; size_t size, capacity, limit; };
size_t http_response_limit(size_t caller_capacity);
size_t http_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata);
int http_get_auth(const char *url, char *out, size_t cap, int timeout_ms, const char *bearer_token);
int http_get(const char *url, char *out, size_t cap, int timeout_ms);
int check_port(int port);
void cleanup_curl_handle(void);
#endif
