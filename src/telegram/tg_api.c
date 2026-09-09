/* clawdget: tg_api.c - Telegram Bot API client over libcurl */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "tg_api.h"
#include "http.h"

static volatile int *g_abort = NULL;

void tg_api_set_abort(volatile int *flag)
{
	g_abort = flag;
}

void tg_api_init(tg_api_t *api, const char *base_url, const char *token,
		 const char *proxy)
{
	memset(api, 0, sizeof(*api));
	snprintf(api->base_url, sizeof(api->base_url), "%s",
		 base_url && *base_url ? base_url : "https://api.telegram.org");
	snprintf(api->token, sizeof(api->token), "%s", token ? token : "");
	snprintf(api->proxy, sizeof(api->proxy), "%s", proxy ? proxy : "");
}

typedef struct {
	char *acc;
	size_t len, cap;
} tgbuf_t;

static size_t accum_cb(char *p, size_t sz, size_t n, void *ud)
{
	tgbuf_t *c = ud;
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

static int xfer_cb(void *p, curl_off_t dl, curl_off_t dln, curl_off_t ul,
		   curl_off_t uln)
{
	(void)p; (void)dl; (void)dln; (void)ul; (void)uln;
	return g_abort && *g_abort ? 1 : 0;
}

char *tg_post(const tg_api_t *api, const char *method, const char *json_body,
	      char *err, size_t errsz)
{
	CURL *h = curl_easy_init();
	if (!h) {
		snprintf(err, errsz, "curl init failed");
		return NULL;
	}
	tgbuf_t buf = { malloc(4096), 0, 4096 };
	buf.acc[0] = 0;

	char url[768];
	snprintf(url, sizeof(url), "%s/bot%s/%s", api->base_url, api->token,
		 method);

	struct curl_slist *hdrs = NULL;
	hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

	curl_easy_setopt(h, CURLOPT_URL, url);
	curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, accum_cb);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 90L);
	curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xfer_cb);
	curl_easy_setopt(h, CURLOPT_PROGRESSDATA, g_abort);
	if (api->proxy[0]) {
		curl_easy_setopt(h, CURLOPT_PROXY, api->proxy);
		/* proxy itself must also be reachable: cap stalls at 90s */
	}
	if (json_body) {
		curl_easy_setopt(h, CURLOPT_POST, 1L);
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, json_body);
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE,
				 (long)strlen(json_body));
	}

	CURLcode rc = curl_easy_perform(h);
	long code = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(h);

	if (rc == CURLE_ABORTED_BY_CALLBACK) {
		snprintf(err, errsz, "aborted");
		free(buf.acc);
		return NULL;
	}
	if (rc != CURLE_OK) {
		snprintf(err, errsz, "transport: %s", curl_easy_strerror(rc));
		free(buf.acc);
		return NULL;
	}
	if (code < 200 || code >= 300) {
		snprintf(err, errsz, "HTTP %ld: %.200s", code,
			 buf.acc ? buf.acc : "");
		free(buf.acc);
		return NULL;
	}
	if (!buf.acc) {
		buf.acc = strdup("");
	}
	return buf.acc;
}
