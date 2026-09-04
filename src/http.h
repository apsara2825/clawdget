/* clawdget: http.h - libcurl wrapper: SSE streaming POST + simple GET */
#ifndef PC_HTTP_H
#define PC_HTTP_H

#include <stddef.h>

/* optional: path to CA bundle PEM; set before any http call (NULL = libcurl default) */
extern const char *pc_http_ca_info;

/* callback invoked for each complete SSE `data:` payload line (after "data: ").
 * Also invoked for other event lines' payloads; caller filters. */
typedef void (*pc_sse_cb)(const char *payload, void *ud);

typedef struct {
	long  http_code;      /* 0 if unknown */
	char *err_body;       /* response body when HTTP >= 400 (malloc'd), else NULL */
	char  curl_err[256];  /* curl error string if transport failure */
} pc_http_result;

/* POST JSON body to url, stream the response through sse_cb line by line.
 * Returns 0 on HTTP 200 (sse_cb may have been called any number of times),
 * -1 on transport error (curl_err set), -2 on HTTP error (err_body set).
 * Non-200 response bodies are NOT passed to sse_cb. */
int http_post_stream(const char *url, const char *auth_bearer,
		     const char *body, pc_sse_cb cb, void *ud,
		     pc_http_result *res);

/* POST and collect the entire response body into *out (malloc'd).
 * Same return conventions: 0 / -1 transport / -2 HTTP error (out = body). */
int http_post_collect(const char *url, const char *auth_bearer,
		      const char *body, size_t max_bytes,
		      char **out, pc_http_result *res);

/* Simple GET, response collected into *out (malloc'd, NUL terminated),
 * truncated at max_bytes. Returns 0 / -1 / -2 like above (out may still be
 * set on -2 for error bodies). */
int http_get(const char *url, const char *auth_bearer, size_t max_bytes,
	     char **out, pc_http_result *res);

#endif
