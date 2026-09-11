/* clawdget: provider.c - OpenAI-compatible chat client (stream + non-stream)
 * Mirrors clawdget pkg/providers/openai_compat: data: line parsing, tool_calls
 * accumulated by index, [DONE] terminator. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "provider.h"
#include "http.h"
#include "spinner.h"

/* ---- stream accumulator ---- */
typedef struct {
	pc_resp_t *resp;
	FILE      *out;      /* stream content here, NULL = quiet */
	int        stopped;  /* set on [DONE] */
	int        failed;
	char       err[512];
} sstate_t;

static void acc_add_tc(pc_resp_t *r, int index)
{
	if (index >= r->n_tcs) {
		int ncap = index + 1;
		pc_toolcall_t *nt = realloc(r->tcs, sizeof(pc_toolcall_t) * (size_t)ncap);
		if (!nt)
			return;
		r->tcs = nt;
		for (int i = r->n_tcs; i < ncap; i++) {
			r->tcs[i].id = NULL;
			r->tcs[i].name = NULL;
			r->tcs[i].arguments = NULL;
		}
		r->n_tcs = ncap;
	}
}

static void on_data(const char *payload, void *ud)
{
	sstate_t *st = ud;
	if (st->stopped)
		return;
	if (strcmp(payload, "[DONE]") == 0) {
		st->stopped = 1;
		return;
	}
	cJSON *j = cJSON_Parse(payload);
	if (!j)
		return; /* ignore malformed lines (keepalives etc.) */
	cJSON *choices = cJSON_GetObjectItem(j, "choices");
	cJSON *usage = cJSON_GetObjectItem(j, "usage");
	if (cJSON_IsObject(usage)) {
		cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
		cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
		if (cJSON_IsNumber(pt)) st->resp->prompt_tokens = (int)pt->valuedouble;
		if (cJSON_IsNumber(ct)) st->resp->completion_tokens = (int)ct->valuedouble;
	}
	if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
		cJSON *ch = cJSON_GetArrayItem(choices, 0);
		cJSON *delta = cJSON_GetObjectItem(ch, "delta");
		cJSON *fr = cJSON_GetObjectItem(ch, "finish_reason");
		if (cJSON_IsString(fr) && fr->valuestring[0]) {
			free(st->resp->finish_reason);
			st->resp->finish_reason = strdup(fr->valuestring);
		}
		if (cJSON_IsObject(delta)) {
			cJSON *c = cJSON_GetObjectItem(delta, "content");
			if (cJSON_IsString(c) && c->valuestring[0]) {
				size_t ol = st->resp->content ? strlen(st->resp->content) : 0;
				size_t al = strlen(c->valuestring);
				char *nc = realloc(st->resp->content, ol + al + 1);
				if (nc) {
					st->resp->content = nc;
					memcpy(st->resp->content + ol, c->valuestring, al + 1);
					if (st->out) {
						pc_thinking_stop(); /* erase spinner before first token */
						fwrite(c->valuestring, 1, al, st->out);
					}
				}
			}
			cJSON *tcs = cJSON_GetObjectItem(delta, "tool_calls");
			if (cJSON_IsArray(tcs)) {
				int n = cJSON_GetArraySize(tcs);
				for (int i = 0; i < n; i++) {
					cJSON *tc = cJSON_GetArrayItem(tcs, i);
					cJSON *ix = cJSON_GetObjectItem(tc, "index");
					int idx = cJSON_IsNumber(ix) ? (int)ix->valuedouble : i;
					acc_add_tc(st->resp, idx);
					pc_toolcall_t *t = &st->resp->tcs[idx];
					cJSON *v;
					if ((v = cJSON_GetObjectItem(tc, "id")) && cJSON_IsString(v) && v->valuestring[0]) {
						free(t->id);
						t->id = strdup(v->valuestring);
					}
					cJSON *fn = cJSON_GetObjectItem(tc, "function");
					if (cJSON_IsObject(fn)) {
						if ((v = cJSON_GetObjectItem(fn, "name")) && cJSON_IsString(v) && v->valuestring[0]) {
							free(t->name);
							t->name = strdup(v->valuestring);
						}
						if ((v = cJSON_GetObjectItem(fn, "arguments")) && cJSON_IsString(v) && v->valuestring[0]) {
							size_t ol = t->arguments ? strlen(t->arguments) : 0;
							size_t al = strlen(v->valuestring);
							char *na = realloc(t->arguments, ol + al + 1);
							if (na) {
								t->arguments = na;
								memcpy(t->arguments + ol, v->valuestring, al + 1);
							}
						}
					}
				}
			}
		}
	}
	cJSON_Delete(j);
}

/* ---- request building ---- */

static cJSON *build_body(const config_t *cfg, cJSON *messages, cJSON *tools)
{
	cJSON *body = cJSON_CreateObject();
	cJSON_AddStringToObject(body, "model", cfg->model ? cfg->model : "");
	cJSON_AddItemToObject(body, "messages", cJSON_Duplicate(messages, 1));
	if (tools && cJSON_GetArraySize(tools) > 0) {
		cJSON_AddItemToObject(body, "tools", cJSON_Duplicate(tools, 1));
		cJSON_AddStringToObject(body, "tool_choice", "auto");
	}
	if (cfg->temperature >= 0)
		cJSON_AddNumberToObject(body, "temperature", cfg->temperature);
	if (cfg->max_tokens > 0)
		cJSON_AddNumberToObject(body, "max_tokens", cfg->max_tokens);
	cJSON_AddBoolToObject(body, "stream", cfg->stream ? 1 : 0);
	return body;
}

static int parse_error_body(const char *body, char *err, size_t errsz)
{
	if (!body || !*body) {
		snprintf(err, errsz, "empty error response");
		return -1;
	}
	cJSON *j = cJSON_Parse(body);
	if (j) {
		cJSON *e = cJSON_GetObjectItem(j, "error");
		cJSON *m = e ? cJSON_GetObjectItem(e, "message") : cJSON_GetObjectItem(j, "message");
		if (cJSON_IsString(m) && m->valuestring[0])
			snprintf(err, errsz, "API error: %s", m->valuestring);
		else
			snprintf(err, errsz, "API error: %.300s", body);
		cJSON_Delete(j);
	} else {
		snprintf(err, errsz, "API error (non-JSON): %.300s", body);
	}
	return -1;
}

static int fill_from_message(cJSON *msg, pc_resp_t *out)
{
	cJSON *c = cJSON_GetObjectItem(msg, "content");
	if (cJSON_IsString(c) && c->valuestring[0])
		out->content = strdup(c->valuestring);
	cJSON *tcs = cJSON_GetObjectItem(msg, "tool_calls");
	if (cJSON_IsArray(tcs)) {
		int n = cJSON_GetArraySize(tcs);
		out->tcs = calloc((size_t)(n ? n : 1), sizeof(pc_toolcall_t));
		out->n_tcs = n;
		for (int i = 0; i < n; i++) {
			cJSON *tc = cJSON_GetArrayItem(tcs, i);
			cJSON *v;
			pc_toolcall_t *t = &out->tcs[i];
			if ((v = cJSON_GetObjectItem(tc, "id")) && cJSON_IsString(v))
				t->id = strdup(v->valuestring);
			cJSON *fn = cJSON_GetObjectItem(tc, "function");
			if (cJSON_IsObject(fn)) {
				if ((v = cJSON_GetObjectItem(fn, "name")) && cJSON_IsString(v))
					t->name = strdup(v->valuestring);
				if ((v = cJSON_GetObjectItem(fn, "arguments")) && cJSON_IsString(v))
					t->arguments = strdup(v->valuestring);
			}
		}
	}
	return 0;
}

int provider_chat(const config_t *cfg, cJSON *messages, cJSON *tools_defs,
		  pc_resp_t *out, char *err, size_t errsz, FILE *out_stream)
{
	memset(out, 0, sizeof(*out));
	if (!cfg->api_base || !cfg->api_key || !cfg->model) {
		snprintf(err, errsz, "config missing api_base/api_key/model");
		return -1;
	}
	cJSON *body = build_body(cfg, messages, tools_defs);
	char *reqbody = cJSON_PrintUnformatted(body);
	cJSON_Delete(body);
	if (!reqbody) {
		snprintf(err, errsz, "request build failed");
		return -1;
	}

	/* join: trim trailing '/' from api_base, then append /chat/completions */
	char base[900];
	snprintf(base, sizeof(base), "%s", cfg->api_base);
	size_t bl = strlen(base);
	while (bl > 0 && base[bl - 1] == '/')
		base[--bl] = 0;
	char url[1024];
	snprintf(url, sizeof(url), "%s/chat/completions", base);

	int ret = -1;
	if (cfg->stream) {
		sstate_t st;
		memset(&st, 0, sizeof(st));
		st.resp = out;
		st.out = out_stream;
		pc_http_result res;
		for (int attempt = 0; attempt < 2; attempt++) {
			if (attempt > 0) {
				if (out_stream)
					fprintf(out_stream,
						"[重试 %d/1]...\n", attempt);
				struct timespec ts = {2, 0};
				nanosleep(&ts, NULL);
			}
			pc_thinking_start();
			int rc = http_post_stream(url, cfg->api_key, reqbody,
						  on_data, &st, &res);
			pc_thinking_stop();
			if (out_stream)
				fflush(out_stream);
			if (res.curl_err[0]) {
				snprintf(err, errsz, "transport: %s",
					 res.curl_err);
				/* retry transient network errors once */
				if (strstr(res.curl_err, "(curl 7)") ||
				    strstr(res.curl_err, "(curl 28)") ||
				    strstr(res.curl_err, "(curl 56)")) {
					free(res.err_body);
					memset(&res, 0, sizeof(res));
					continue;
				}
				strncat(err, "（运行 clawdget doctor 可诊断）",
					errsz - strlen(err) - 1);
			} else if (res.http_code != 200) {
				if (res.err_body)
					parse_error_body(res.err_body, err, errsz);
				else
					snprintf(err, errsz, "HTTP %ld",
						 res.http_code);
			} else {
				ret = 0;
			}
			free(res.err_body);
			break;
		}
	} else {
		/* non-stream: buffered POST, parse the full JSON response */
		char *resp_body = NULL;
		pc_http_result res;
		pc_thinking_start();
		http_post_collect(url, cfg->api_key, reqbody, 8 << 20, &resp_body, &res);
		pc_thinking_stop();
		if (res.curl_err[0]) {
			snprintf(err, errsz, "transport: %s", res.curl_err);
		} else if (res.http_code != 200) {
			parse_error_body(resp_body, err, errsz);
		} else if (resp_body) {
			cJSON *j = cJSON_Parse(resp_body);
			if (!j) {
				snprintf(err, errsz, "bad JSON response");
			} else {
				cJSON *choices = cJSON_GetObjectItem(j, "choices");
				if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
					cJSON *msg = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
					if (cJSON_IsObject(msg))
						fill_from_message(msg, out);
					cJSON *u = cJSON_GetObjectItem(j, "usage");
					if (cJSON_IsObject(u)) {
						cJSON *pt = cJSON_GetObjectItem(u, "prompt_tokens");
						cJSON *ct = cJSON_GetObjectItem(u, "completion_tokens");
						if (cJSON_IsNumber(pt)) out->prompt_tokens = (int)pt->valuedouble;
						if (cJSON_IsNumber(ct)) out->completion_tokens = (int)ct->valuedouble;
					}
					ret = 0;
				} else {
					snprintf(err, errsz, "no choices in response");
				}
				cJSON_Delete(j);
			}
		} else {
			snprintf(err, errsz, "empty response");
		}
		free(resp_body);
		free(res.err_body);
	}
	free(reqbody);
	return ret;
}

void pc_resp_free(pc_resp_t *r)
{
	free(r->content);
	free(r->finish_reason);
	for (int i = 0; i < r->n_tcs; i++) {
		free(r->tcs[i].id);
		free(r->tcs[i].name);
		free(r->tcs[i].arguments);
	}
	free(r->tcs);
	memset(r, 0, sizeof(*r));
}
