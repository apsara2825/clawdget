/* clawdget: wx_api.c - Tencent iLink bot REST client (libcurl) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "wx_api.h"
#include "http.h"

void wx_api_init(wx_api_t *api, const char *base_url, const char *token)
{
	memset(api, 0, sizeof(*api));
	snprintf(api->base_url, sizeof(api->base_url), "%s",
		 base_url && *base_url ? base_url : WX_DEFAULT_BASE);
	snprintf(api->token, sizeof(api->token), "%s", token ? token : "");
}

/* shared curl collector for POST/GET */
typedef struct {
	CURL *h;
	char *acc;
	size_t len, cap, max;
} collector_t;

static size_t accum_cb(char *p, size_t sz, size_t n, void *ud)
{
	collector_t *c = ud;
	size_t total = sz * n;
	if (c->len + total + 1 > c->cap) {
		c->cap = (c->len + total + 1) * 2;
		char *nb = realloc(c->acc, c->cap);
		if (!nb)
			return 0;
		c->acc = nb;
	}
	memcpy(c->acc + c->len, p, total);
	c->len += total;
	c->acc[c->len] = 0;
	return total;
}

static void build_headers(const wx_api_t *api, int skip_auth,
			  struct curl_slist **hdrs_out)
{
	struct curl_slist *hdrs = NULL;
	hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
	hdrs = curl_slist_append(hdrs, "iLink-App-Id: " WX_APP_ID);
	char v[64];
	snprintf(v, sizeof(v), "iLink-App-ClientVersion: %d", WX_CLIENT_VERSION);
	hdrs = curl_slist_append(hdrs, v);
	if (!skip_auth) {
		hdrs = curl_slist_append(hdrs, "AuthorizationType: ilink_bot_token");
		unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(long)api;
		char uin[32];
		snprintf(uin, sizeof(uin), "X-WECHAT-UIN: %u", seed);
		hdrs = curl_slist_append(hdrs, uin);
		if (api->token[0]) {
			char auth[600];
			snprintf(auth, sizeof(auth), "Authorization: Bearer %s",
				 api->token);
			hdrs = curl_slist_append(hdrs, auth);
		}
	}
	*hdrs_out = hdrs;
}

static int do_request(const wx_api_t *api, const char *url, const char *post_body,
		      int skip_auth, char **out, char *err, size_t errsz)
{
	CURL *h = curl_easy_init();
	if (!h) {
		snprintf(err, errsz, "curl init failed");
		return -1;
	}
	collector_t c = { h, malloc(4096), 0, 4096 };
	c.acc[0] = 0;

	struct curl_slist *hdrs = NULL;
	build_headers(api, skip_auth, &hdrs);

	curl_easy_setopt(h, CURLOPT_URL, url);
	curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, accum_cb);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &c);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 90L);
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	if (post_body) {
		curl_easy_setopt(h, CURLOPT_POST, 1L);
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, post_body);
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)strlen(post_body));
	}

	CURLcode rc = curl_easy_perform(h);
	long code = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(h);

	if (rc != CURLE_OK) {
		snprintf(err, errsz, "transport: %s", curl_easy_strerror(rc));
		free(c.acc);
		return -1;
	}
	if (code < 200 || code >= 300) {
		snprintf(err, errsz, "HTTP %ld: %.200s", code,
			 c.acc ? c.acc : "");
		free(c.acc);
		return -1;
	}
	if (!c.acc) {
		c.acc = strdup("");
	}
	*out = c.acc;
	return 0;
}

static void build_url(const wx_api_t *api, const char *endpoint,
		      const char *query, char *url, size_t sz)
{
	size_t bl = strlen(api->base_url);
	snprintf(url, sz, "%s%s%s%s%s", api->base_url,
		 bl && api->base_url[bl - 1] == '/' ? "" : "/", endpoint,
		 query ? "?" : "", query ? query : "");
}

char *wx_post(const wx_api_t *api, const char *endpoint, const char *body,
	      char *err, size_t errsz, int skip_auth)
{
	char url[512];
	build_url(api, endpoint, NULL, url, sizeof(url));
	char *out = NULL;
	if (do_request(api, url, body, skip_auth, &out, err, errsz) != 0)
		return NULL;
	return out;
}

char *wx_get(const wx_api_t *api, const char *endpoint, char *err, size_t errsz,
	     int skip_auth)
{
	char url[768];
	build_url(api, endpoint, NULL, url, sizeof(url));
	char *out = NULL;
	if (do_request(api, url, NULL, skip_auth, &out, err, errsz) != 0)
		return NULL;
	return out;
}
