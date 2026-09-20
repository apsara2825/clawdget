/* clawdget: provider.c - chat clients: OpenAI-compatible + Anthropic Messages
 * (stream + non-stream), tool-calling in both wire formats. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "provider.h"
#include "http.h"
#include "spinner.h"

/* ---- internal response accumulator ---- */
typedef struct {
	pc_resp_t *resp;
	FILE      *out;      /* stream content here, NULL = quiet */
	int        stopped;  /* openai: set on [DONE] */
	int        anth;     /* anthropic SSE mode */
	int        failed;
	char       err[512];
	/* anthropic content-block tracking */
	int        blk_type[64];   /* 0 text, 1 tool_use */
	int        blk_tc[64];     /* tool_use block -> tc slot */
	int        n_blks;
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

static void append_text(pc_resp_t *r, FILE *out, const char *text, size_t al)
{
	size_t ol = r->content ? strlen(r->content) : 0;
	char *nc = realloc(r->content, ol + al + 1);
	if (!nc)
		return;
	r->content = nc;
	memcpy(r->content + ol, text, al + 1);
	if (out) {
		pc_thinking_stop(); /* erase spinner before first token */
		fwrite(text, 1, al, out);
	}
}

/* ---- openai SSE ---- */

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
	cJSON *usage = cJSON_GetObjectItem(j, "usage");
	if (cJSON_IsObject(usage)) {
		cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
		cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
		if (cJSON_IsNumber(pt)) st->resp->prompt_tokens = (int)pt->valuedouble;
		if (cJSON_IsNumber(ct)) st->resp->completion_tokens = (int)ct->valuedouble;
	}
	cJSON *choices = cJSON_GetObjectItem(j, "choices");
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
			if (cJSON_IsString(c) && c->valuestring[0])
				append_text(st->resp, st->out, c->valuestring,
					    strlen(c->valuestring));
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

/* ---- anthropic SSE ---- */

static void on_data_anthropic(const char *payload, void *ud)
{
	sstate_t *st = ud;
	cJSON *j = cJSON_Parse(payload);
	if (!j)
		return;
	cJSON *type = cJSON_GetObjectItem(j, "type");
	const char *ty = cJSON_IsString(type) ? type->valuestring : "";

	if (!strcmp(ty, "content_block_start")) {
		cJSON *ix = cJSON_GetObjectItem(j, "index");
		cJSON *cb = cJSON_GetObjectItem(j, "content_block");
		int idx = cJSON_IsNumber(ix) ? (int)ix->valuedouble : st->n_blks;
		if (idx >= 0 && idx < 64) {
			if (idx >= st->n_blks)
				st->n_blks = idx + 1;
			cJSON *bt = cJSON_GetObjectItem(cb, "type");
			st->blk_type[idx] = (cJSON_IsString(bt) &&
					     !strcmp(bt->valuestring, "tool_use")) ? 1 : 0;
			if (st->blk_type[idx]) {
				acc_add_tc(st->resp, st->resp->n_tcs);
				st->blk_tc[idx] = st->resp->n_tcs - 1;
				pc_toolcall_t *t = &st->resp->tcs[st->blk_tc[idx]];
				cJSON *v;
				if ((v = cJSON_GetObjectItem(cb, "id")) && cJSON_IsString(v))
					t->id = strdup(v->valuestring);
				if ((v = cJSON_GetObjectItem(cb, "name")) && cJSON_IsString(v))
					t->name = strdup(v->valuestring);
				t->arguments = strdup("");
			}
		}
	} else if (!strcmp(ty, "content_block_delta")) {
		cJSON *ix = cJSON_GetObjectItem(j, "index");
		cJSON *dl = cJSON_GetObjectItem(j, "delta");
		int idx = cJSON_IsNumber(ix) ? (int)ix->valuedouble : 0;
		cJSON *dt = cJSON_GetObjectItem(dl, "type");
		if (cJSON_IsString(dt) && !strcmp(dt->valuestring, "text_delta")) {
			cJSON *tx = cJSON_GetObjectItem(dl, "text");
			if (cJSON_IsString(tx))
				append_text(st->resp, st->out, tx->valuestring,
					    strlen(tx->valuestring));
		} else if (cJSON_IsString(dt) &&
			   !strcmp(dt->valuestring, "input_json_delta")) {
			cJSON *pj = cJSON_GetObjectItem(dl, "partial_json");
			if (cJSON_IsString(pj) && idx >= 0 && idx < 64 &&
			    st->blk_type[idx] == 1) {
				pc_toolcall_t *t = &st->resp->tcs[st->blk_tc[idx]];
				size_t ol = t->arguments ? strlen(t->arguments) : 0;
				size_t al = strlen(pj->valuestring);
				char *na = realloc(t->arguments, ol + al + 1);
				if (na) {
					t->arguments = na;
					memcpy(t->arguments + ol, pj->valuestring, al + 1);
				}
			}
		}
	} else if (!strcmp(ty, "message_delta")) {
		cJSON *dl = cJSON_GetObjectItem(j, "delta");
		cJSON *sr = dl ? cJSON_GetObjectItem(dl, "stop_reason") : NULL;
		if (cJSON_IsString(sr) && sr->valuestring[0]) {
			free(st->resp->finish_reason);
			st->resp->finish_reason = strdup(sr->valuestring);
		}
		cJSON *u = cJSON_GetObjectItem(j, "usage");
		if (cJSON_IsObject(u)) {
			cJSON *ot = cJSON_GetObjectItem(u, "output_tokens");
			if (cJSON_IsNumber(ot))
				st->resp->completion_tokens = (int)ot->valuedouble;
		}
	}
	cJSON_Delete(j);
}

static void on_data_dispatch(const char *payload, void *ud)
{
	sstate_t *st = ud;
	if (st->anth) {
		if (!st->stopped)
			on_data_anthropic(payload, ud);
		return;
	}
	on_data(payload, ud);
}

/* ---- openai request building ---- */

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

/* ---- anthropic request building ---- */

static cJSON *anth_build_messages(cJSON *messages, char **system_out)
{
	cJSON *arr = cJSON_CreateArray();
	cJSON *sys_parts = cJSON_CreateArray();
	int n = cJSON_GetArraySize(messages);
	for (int i = 0; i < n; i++) {
		cJSON *m = cJSON_GetArrayItem(messages, i);
		cJSON *r = cJSON_GetObjectItem(m, "role");
		const char *role = cJSON_IsString(r) ? r->valuestring : "user";
		cJSON *c = cJSON_GetObjectItem(m, "content");

		if (!strcmp(role, "system")) {
			if (cJSON_IsString(c) && c->valuestring[0])
				cJSON_AddItemToArray(sys_parts,
						     cJSON_CreateString(c->valuestring));
			continue;
		}
		if (!strcmp(role, "tool")) {
			cJSON *am = cJSON_CreateObject();
			cJSON_AddStringToObject(am, "role", "user");
			cJSON *blocks = cJSON_CreateArray();
			cJSON *b = cJSON_CreateObject();
			cJSON_AddStringToObject(b, "type", "tool_result");
			cJSON *tid = cJSON_GetObjectItem(m, "tool_call_id");
			cJSON_AddStringToObject(b, "tool_use_id",
						cJSON_IsString(tid) ? tid->valuestring : "");
			cJSON_AddStringToObject(b, "content",
						cJSON_IsString(c) ? c->valuestring : "");
			cJSON_AddItemToArray(blocks, b);
			cJSON_AddItemToObject(am, "content", blocks);
			cJSON_AddItemToArray(arr, am);
			continue;
		}

		cJSON *am = cJSON_CreateObject();
		cJSON_AddStringToObject(am, "role",
					!strcmp(role, "assistant") ? "assistant" : "user");
		cJSON *blocks = cJSON_CreateArray();
		int has = 0;
		if (cJSON_IsString(c) && c->valuestring[0]) {
			cJSON *b = cJSON_CreateObject();
			cJSON_AddStringToObject(b, "type", "text");
			cJSON_AddStringToObject(b, "text", c->valuestring);
			cJSON_AddItemToArray(blocks, b);
			has = 1;
		}
		cJSON *tcs = cJSON_GetObjectItem(m, "tool_calls");
		if (cJSON_IsArray(tcs)) {
			int tn = cJSON_GetArraySize(tcs);
			for (int k = 0; k < tn; k++) {
				cJSON *tc = cJSON_GetArrayItem(tcs, k);
				cJSON *fn = cJSON_GetObjectItem(tc, "function");
				cJSON *b = cJSON_CreateObject();
				cJSON_AddStringToObject(b, "type", "tool_use");
				cJSON *tid = cJSON_GetObjectItem(tc, "id");
				cJSON *nm = fn ? cJSON_GetObjectItem(fn, "name") : NULL;
				cJSON *ar = fn ? cJSON_GetObjectItem(fn, "arguments") : NULL;
				cJSON_AddStringToObject(b, "id",
							cJSON_IsString(tid) ? tid->valuestring : "");
				cJSON_AddStringToObject(b, "name",
							cJSON_IsString(nm) ? nm->valuestring : "");
				cJSON *input = cJSON_Parse(cJSON_IsString(ar) ? ar->valuestring : "{}");
				cJSON_AddItemToObject(b, "input",
						      input ? input : cJSON_CreateObject());
				cJSON_AddItemToArray(blocks, b);
				has = 1;
			}
		}
		if (!has) {
			cJSON *b = cJSON_CreateObject();
			cJSON_AddStringToObject(b, "type", "text");
			cJSON_AddStringToObject(b, "text", "");
			cJSON_AddItemToArray(blocks, b);
		}
		cJSON_AddItemToObject(am, "content", blocks);
		cJSON_AddItemToArray(arr, am);
	}
	*system_out = cJSON_PrintUnformatted(sys_parts);
	cJSON_Delete(sys_parts);
	return arr;
}

static cJSON *anth_build_body(const config_t *cfg, cJSON *messages, cJSON *tools)
{
	char *system_json = NULL;
	cJSON *msgs = anth_build_messages(messages, &system_json);
	cJSON *body = cJSON_CreateObject();
	cJSON_AddStringToObject(body, "model", cfg->model ? cfg->model : "");
	if (system_json) {
		cJSON *sys = cJSON_Parse(system_json);
		if (sys) {
			if (cJSON_IsArray(sys) && cJSON_GetArraySize(sys) == 1)
				cJSON_AddItemToObject(body, "system",
						      cJSON_Duplicate(cJSON_GetArrayItem(sys, 0), 1));
			else if (cJSON_IsString(sys))
				cJSON_AddStringToObject(body, "system", sys->valuestring);
			else
				cJSON_AddItemToObject(body, "system", sys);
		}
		free(system_json);
	}
	cJSON_AddItemToObject(body, "messages", msgs);
	cJSON_AddNumberToObject(body, "max_tokens",
				cfg->max_tokens > 0 ? cfg->max_tokens : 4096);
	if (cfg->temperature >= 0)
		cJSON_AddNumberToObject(body, "temperature", cfg->temperature);
	if (tools && cJSON_GetArraySize(tools) > 0) {
		cJSON *tg = cJSON_CreateArray();
		int n = cJSON_GetArraySize(tools);
		for (int i = 0; i < n; i++) {
			cJSON *t = cJSON_GetArrayItem(tools, i);
			cJSON *fn = cJSON_GetObjectItem(t, "function");
			if (!cJSON_IsObject(fn))
				continue;
			cJSON *b = cJSON_CreateObject();
			cJSON *nm = cJSON_GetObjectItem(fn, "name");
			cJSON *ds = cJSON_GetObjectItem(fn, "description");
			cJSON *pa = cJSON_GetObjectItem(fn, "parameters");
			cJSON_AddStringToObject(b, "name",
						cJSON_IsString(nm) ? nm->valuestring : "");
			if (cJSON_IsString(ds))
				cJSON_AddStringToObject(b, "description", ds->valuestring);
			cJSON_AddItemToObject(b, "input_schema",
					      cJSON_IsObject(pa) ? cJSON_Duplicate(pa, 1)
								 : cJSON_CreateObject());
			cJSON_AddItemToArray(tg, b);
		}
		cJSON_AddItemToObject(body, "tools", tg);
	}
	cJSON_AddBoolToObject(body, "stream", cfg->stream ? 1 : 0);
	return body;
}

/* ---- openai non-stream parse ---- */

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

/* ---- anthropic non-stream parse ---- */

static void fill_from_anthropic(cJSON *j, pc_resp_t *out)
{
	cJSON *content = cJSON_GetObjectItem(j, "content");
	if (cJSON_IsArray(content)) {
		int n = cJSON_GetArraySize(content);
		for (int i = 0; i < n; i++) {
			cJSON *b = cJSON_GetArrayItem(content, i);
			cJSON *bt = cJSON_GetObjectItem(b, "type");
			const char *ty = cJSON_IsString(bt) ? bt->valuestring : "";
			if (!strcmp(ty, "text")) {
				cJSON *tx = cJSON_GetObjectItem(b, "text");
				if (cJSON_IsString(tx) && tx->valuestring[0]) {
					size_t ol = out->content ? strlen(out->content) : 0;
					size_t al = strlen(tx->valuestring);
					char *nc = realloc(out->content, ol + al + 1);
					if (nc) {
						out->content = nc;
						memcpy(out->content + ol, tx->valuestring, al + 1);
					}
				}
			} else if (!strcmp(ty, "tool_use")) {
				acc_add_tc(out, out->n_tcs);
				pc_toolcall_t *t = &out->tcs[out->n_tcs - 1];
				cJSON *v;
				if ((v = cJSON_GetObjectItem(b, "id")) && cJSON_IsString(v))
					t->id = strdup(v->valuestring);
				if ((v = cJSON_GetObjectItem(b, "name")) && cJSON_IsString(v))
					t->name = strdup(v->valuestring);
				cJSON *ip = cJSON_GetObjectItem(b, "input");
				char *ar = cJSON_PrintUnformatted(ip ? ip : cJSON_CreateObject());
				t->arguments = ar ? ar : strdup("{}");
			}
		}
	}
	cJSON *sr = cJSON_GetObjectItem(j, "stop_reason");
	if (cJSON_IsString(sr))
		out->finish_reason = strdup(sr->valuestring);
	cJSON *u = cJSON_GetObjectItem(j, "usage");
	if (cJSON_IsObject(u)) {
		cJSON *it = cJSON_GetObjectItem(u, "input_tokens");
		cJSON *ot = cJSON_GetObjectItem(u, "output_tokens");
		if (cJSON_IsNumber(it)) out->prompt_tokens = (int)it->valuedouble;
		if (cJSON_IsNumber(ot)) out->completion_tokens = (int)ot->valuedouble;
	}
}

/* ---- error body ---- */

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

/* ---- main entry ---- */

int provider_chat(const config_t *cfg, cJSON *messages, cJSON *tools_defs,
		  pc_resp_t *out, char *err, size_t errsz, FILE *out_stream)
{
	memset(out, 0, sizeof(*out));
	if (!cfg->api_base || !cfg->api_key || !cfg->model) {
		snprintf(err, errsz, "config missing api_base/api_key/model");
		return -1;
	}
	int anth = (cfg->protocol == PROTO_ANTHROPIC);

	cJSON *body = anth ? anth_build_body(cfg, messages, tools_defs)
			   : build_body(cfg, messages, tools_defs);
	char *reqbody = cJSON_PrintUnformatted(body);
	cJSON_Delete(body);
	if (!reqbody) {
		snprintf(err, errsz, "request build failed");
		return -1;
	}

	char base[900];
	snprintf(base, sizeof(base), "%s", cfg->api_base);
	size_t bl = strlen(base);
	while (bl > 0 && base[bl - 1] == '/')
		base[--bl] = 0;
	char url[1024];
	snprintf(url, sizeof(url), "%s%s", base,
		 anth ? "/v1/messages" : "/chat/completions");

	const char *hdrs[4];
	char auth_bearer[1100];
	char ak[600];
	int nh = 0;
	if (anth) {
		snprintf(ak, sizeof(ak), "x-api-key: %s", cfg->api_key);
		hdrs[nh++] = ak;
		hdrs[nh++] = "anthropic-version: 2023-06-01";
	} else {
		snprintf(auth_bearer, sizeof(auth_bearer), "Bearer %s",
			 cfg->api_key);
		hdrs[nh++] = auth_bearer;
	}

	int ret = -1;
	sstate_t st;
	memset(&st, 0, sizeof(st));
	st.resp = out;
	st.out = out_stream;
	st.anth = anth;
	pc_http_result res;

	for (int attempt = 0; attempt < 2; attempt++) {
		if (attempt > 0) {
			if (out_stream)
				fprintf(out_stream, "[重试 %d/1]...\n", attempt);
			struct timespec ts = {2, 0};
			nanosleep(&ts, NULL);
		}
		memset(&res, 0, sizeof(res));
		pc_thinking_start();
		int rc = anth
			     ? http_post_stream_h(url, hdrs, reqbody, on_data_dispatch,
						  &st, &res)
			     : http_post_stream(url, auth_bearer, reqbody,
						on_data_dispatch, &st, &res);
		pc_thinking_stop();
		if (out_stream)
			fflush(out_stream);

		if (res.curl_err[0]) {
			snprintf(err, errsz, "transport: %s", res.curl_err);
			/* retry transient network errors once */
			if (strstr(res.curl_err, "(curl 7)") ||
			    strstr(res.curl_err, "(curl 28)") ||
			    strstr(res.curl_err, "(curl 56)")) {
				free(res.err_body);
				continue;
			}
			strncat(err, "（运行 clawdget doctor 可诊断）",
				errsz - strlen(err) - 1);
		} else if (res.http_code != 200) {
			if (res.err_body)
				parse_error_body(res.err_body, err, errsz);
			else
				snprintf(err, errsz, "HTTP %ld", res.http_code);
		} else {
			ret = 0;
		}
		free(res.err_body);
		break;
	}
	free(reqbody);
	return ret;
}

/* ---- non-stream entry (buffered POST + full JSON parse) ---- */

int provider_chat_collect(const config_t *cfg, cJSON *messages, cJSON *tools_defs,
			  pc_resp_t *out, char *err, size_t errsz)
{
	memset(out, 0, sizeof(*out));
	if (!cfg->api_base || !cfg->api_key || !cfg->model) {
		snprintf(err, errsz, "config missing api_base/api_key/model");
		return -1;
	}
	int anth = (cfg->protocol == PROTO_ANTHROPIC);

	cJSON *body = anth ? anth_build_body(cfg, messages, tools_defs)
			   : build_body(cfg, messages, tools_defs);
	char *reqbody = cJSON_PrintUnformatted(body);
	cJSON_Delete(body);
	if (!reqbody) {
		snprintf(err, errsz, "request build failed");
		return -1;
	}

	char base[900];
	snprintf(base, sizeof(base), "%s", cfg->api_base);
	size_t bl = strlen(base);
	while (bl > 0 && base[bl - 1] == '/')
		base[--bl] = 0;
	char url[1024];
	snprintf(url, sizeof(url), "%s%s", base,
		 anth ? "/v1/messages" : "/chat/completions");

	const char *hdrs[4];
	char auth_bearer[1100];
	char ak[600];
	int nh = 0;
	if (anth) {
		snprintf(ak, sizeof(ak), "x-api-key: %s", cfg->api_key);
		hdrs[nh++] = ak;
		hdrs[nh++] = "anthropic-version: 2023-06-01";
	} else {
		snprintf(auth_bearer, sizeof(auth_bearer), "Bearer %s", cfg->api_key);
		hdrs[nh++] = auth_bearer;
	}

	int ret = -1;
	char *resp_body = NULL;
	pc_http_result res;
	pc_thinking_start();
	int rc = http_post_collect_h(url, hdrs, reqbody, 8 << 20, &resp_body, &res);
	pc_thinking_stop();
	(void)rc;

	if (res.curl_err[0]) {
		snprintf(err, errsz, "transport: %s", res.curl_err);
	} else if (res.http_code != 200) {
		parse_error_body(resp_body, err, errsz);
	} else if (resp_body) {
		cJSON *j = cJSON_Parse(resp_body);
		if (!j) {
			snprintf(err, errsz, "bad JSON response");
		} else if (anth) {
			fill_from_anthropic(j, out);
			ret = 0;
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
		}
		cJSON_Delete(j);
	} else {
		snprintf(err, errsz, "empty response");
	}
	free(resp_body);
	free(res.err_body);
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
