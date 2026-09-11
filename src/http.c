/* clawdget: http.c - libcurl wrapper (SSE stream + GET) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include "http.h"

const char *pc_http_ca_info = NULL;

/* shared CA auto-detection: configured value, else well-known locations.
 * returns CAINFO file path / CAPATH dir string, or NULL if nothing found. */
void pc_http_apply_ca(CURL *h, const char *configured)
{
	char buf[256];
	const char *ca = (configured && *configured) ? configured
			     : pc_http_ca_default(buf, sizeof(buf));
	if (!ca)
		return;
	struct stat st;
	if (stat(ca, &st) == 0 && S_ISDIR(st.st_mode))
		curl_easy_setopt(h, CURLOPT_CAPATH, ca);
	else
		curl_easy_setopt(h, CURLOPT_CAINFO, ca);
}

const char *pc_http_ca_default(char *buf, size_t bufsz)
{
	if (pc_http_ca_info && *pc_http_ca_info)
		return pc_http_ca_info;
	struct stat st;
	if (stat("/etc/ssl/certs/ca-certificates.crt", &st) == 0)
		return "/etc/ssl/certs/ca-certificates.crt";
	if (stat("/etc/ssl/certs", &st) == 0 && S_ISDIR(st.st_mode))
		return "/etc/ssl/certs";
	if (stat("/etc/pki/tls/certs", &st) == 0)
		return "/etc/pki/tls/certs";
	return NULL;
}

#define SSE_IDLE_TIMEOUT 300L /* seconds without data before abort */

/* ---- line reassembly across chunks ---- */
typedef struct {
	pc_sse_cb cb;
	void     *ud;
	char     *linebuf;
	size_t    len, cap;
	int       stream_done;   /* set by caller via ctx when [DONE] seen */
} linectx_t;

static int is_stream_stopped(const linectx_t *lc)
{
	/* provider.c sets *(int*)(ud chain)? simpler: caller stops by NULL cb */
	return lc->cb == NULL;
}

static void feed_line(linectx_t *lc, char *line)
{
	/* SSE: lines starting with ':' are comments; "data: ..." payloads */
	if (line[0] == ':')
		return;
	const char *p = line;
	if (strncmp(p, "data:", 5) == 0) {
		p += 5;
		if (*p == ' ')
			p++;
		lc->cb(p, lc->ud);
	}
	/* other fields (event:, id:, retry:) ignored */
}

static void feed_chunk(linectx_t *lc, const char *data, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (is_stream_stopped(lc))
			return;
		char c = data[i];
		if (c == '\r')
			continue;
		if (c == '\n') {
			if (lc->len > 0) {
				lc->linebuf[lc->len] = 0;
				feed_line(lc, lc->linebuf);
				lc->len = 0;
			}
			/* empty line = event boundary; payload already dispatched */
		} else {
			if (lc->len + 2 > lc->cap) {
				lc->cap *= 2;
				char *nb = realloc(lc->linebuf, lc->cap);
				if (!nb)
					return;
				lc->linebuf = nb;
			}
			lc->linebuf[lc->len++] = c;
		}
	}
}

/* ---- write callbacks ---- */
typedef struct {
	linectx_t   lc;
	pc_http_result *res;
	int         collect;    /* collect body instead of streaming (errors) */
	char       *acc;
	size_t      acc_len, acc_cap, max_acc;
} wctx_t;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	wctx_t *w = userdata;
	size_t n = size * nmemb;

	if (w->collect) {
		size_t room = w->max_acc - w->acc_len;
		size_t take = n < room ? n : room;
		if (take > 0) {
			if (w->acc_len + take + 1 > w->acc_cap) {
				w->acc_cap = (w->acc_len + take + 1) * 2;
				char *nb = realloc(w->acc, w->acc_cap);
				if (!nb)
					return 0;
				w->acc = nb;
			}
			memcpy(w->acc + w->acc_len, ptr, take);
			w->acc_len += take;
			w->acc[w->acc_len] = 0;
		}
		return n;
	}
	feed_chunk(&w->lc, ptr, n);
	return n;
}

/* We need the HTTP code inside write_cb to decide error-body collection.
 * libcurl gives it via CURLINFO_RESPONSE_CODE on the handle; pass handle
 * through a wrapper struct. */
typedef struct {
	CURL *h;
	wctx_t w;
} req_t;

static size_t write_cb_entry(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	req_t *r = userdata;
	long code = 0;
	curl_easy_getinfo(r->h, CURLINFO_RESPONSE_CODE, &code);
	/* Once headers arrive with a non-200 code, switch to collecting the
	 * error body instead of streaming. 200 => stream. */
	if (code != 200)
		r->w.collect = 1;
	return write_cb(ptr, size, nmemb, &r->w);
}

static void result_from_curl(pc_http_result *res, CURLcode rc, CURL *h)
{
	res->err_body = NULL;
	res->curl_err[0] = 0;
	long code = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
	res->http_code = code;
	if (rc != CURLE_OK)
		snprintf(res->curl_err, sizeof(res->curl_err), "%s",
			 curl_easy_strerror(rc));
}

static CURLcode common_setup(CURL *h, const char *url, const char *auth)
{
	CURLcode rc = curl_easy_setopt(h, CURLOPT_URL, url);
	if (getenv("CLAWDGET_VERBOSE"))
		curl_easy_setopt(h, CURLOPT_VERBOSE, 1L);
	if (rc) return rc;
	rc = curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	if (rc) return rc;
	rc = curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_NOPROXY,
			 "localhost,127.0.0.1,::1,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16");
	if (rc) return rc;
	rc = curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
	if (rc) return rc;
	if (pc_http_ca_info && *pc_http_ca_info) {
		rc = curl_easy_setopt(h, CURLOPT_CAINFO, pc_http_ca_info);
		if (rc) return rc;
	} else {
		pc_http_apply_ca(h, NULL);
	}
	if (rc) return rc;
	/* abort if slower than 1 byte/s for SSE_IDLE_TIMEOUT seconds */
	rc = curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
	if (rc) return rc;
	rc = curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, SSE_IDLE_TIMEOUT);
	if (rc) return rc;
	struct curl_slist *hdrs = NULL;
	hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
	hdrs = curl_slist_append(hdrs, "Accept: text/event-stream");
	if (auth && *auth) {
		char ab[1024];
		snprintf(ab, sizeof(ab), "Authorization: Bearer %s", auth);
		hdrs = curl_slist_append(hdrs, ab);
	}
	rc = curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
	return rc;
}

int http_post_stream(const char *url, const char *auth_bearer,
		     const char *body, pc_sse_cb cb, void *ud,
		     pc_http_result *res)
{
	memset(res, 0, sizeof(*res));
	CURL *h = curl_easy_init();
	if (!h)
		return -1;
	req_t r;
	memset(&r, 0, sizeof(r));
	r.h = h;
	r.w.lc.cb = cb;
	r.w.lc.ud = ud;
	r.w.lc.cap = 4096;
	r.w.lc.linebuf = malloc(r.w.lc.cap);
	r.w.max_acc = 1 << 20;
	r.w.res = res;

	CURLcode rc = common_setup(h, url, auth_bearer);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_POST, 1L);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb_entry);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_WRITEDATA, &r);

	if (!rc)
		rc = curl_easy_perform(h);
	result_from_curl(res, rc, h);

	int ret;
	if (rc != CURLE_OK) {
		ret = -1;
	} else if (res->http_code >= 400 || (res->http_code != 200 && res->http_code != 201)) {
		res->err_body = r.w.acc; /* may be NULL */
		r.w.acc = NULL;
		ret = -2;
	} else {
		/* deliver any trailing buffered line */
		if (r.w.lc.len > 0 && cb) {
			r.w.lc.linebuf[r.w.lc.len] = 0;
			feed_line(&r.w.lc, r.w.lc.linebuf);
		}
		ret = 0;
	}
	free(r.w.lc.linebuf);
	free(r.w.acc);
	curl_easy_cleanup(h);
	return ret;
}

/* move the collected body out of the request context and decide the return
 * code from transport result + HTTP status */
static int finish_collect(req_t *r, pc_http_result *res, char **out, CURLcode rc)
{
	*out = NULL;
	if (rc != CURLE_OK)
		return -1;
	if (res->http_code >= 400) {
		*out = r->w.acc;
		r->w.acc = NULL;
		return -2;
	}
	*out = r->w.acc;
	r->w.acc = NULL;
	return 0;
}

int http_get(const char *url, const char *auth_bearer, size_t max_bytes,
	     char **out, pc_http_result *res)
{
	memset(res, 0, sizeof(*res));
	*out = NULL;
	CURL *h = curl_easy_init();
	if (!h)
		return -1;
	req_t r;
	memset(&r, 0, sizeof(r));
	r.h = h;
	r.w.collect = 1;
	r.w.max_acc = max_bytes;
	r.w.acc_cap = max_bytes + 1;
	r.w.acc = malloc(r.w.acc_cap);
	r.w.acc[0] = 0;

	CURLcode rc = common_setup(h, url, auth_bearer);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, ""); /* let curl do gzip */
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_TIMEOUT, 60L);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb_entry);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_WRITEDATA, &r);
	if (!rc)
		rc = curl_easy_perform(h);
	result_from_curl(res, rc, h);

	int ret = finish_collect(&r, res, out, rc);
	free(r.w.acc);
	curl_easy_cleanup(h);
	return ret;
}

int http_post_collect(const char *url, const char *auth_bearer,
		      const char *body, size_t max_bytes,
		      char **out, pc_http_result *res)
{
	memset(res, 0, sizeof(*res));
	*out = NULL;
	CURL *h = curl_easy_init();
	if (!h)
		return -1;
	req_t r;
	memset(&r, 0, sizeof(r));
	r.h = h;
	r.w.collect = 1;
	r.w.max_acc = max_bytes;
	r.w.acc_cap = max_bytes + 1;
	r.w.acc = malloc(r.w.acc_cap);
	r.w.acc[0] = 0;

	CURLcode rc = common_setup(h, url, auth_bearer);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_POST, 1L);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb_entry);
	if (!rc) rc = curl_easy_setopt(h, CURLOPT_WRITEDATA, &r);
	if (!rc)
		rc = curl_easy_perform(h);
	result_from_curl(res, rc, h);

	int ret = finish_collect(&r, res, out, rc);
	free(r.w.acc);
	curl_easy_cleanup(h);
	return ret;
}
