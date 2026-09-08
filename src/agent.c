/* clawdget: agent.c - LLM <-> tools loop, per turn */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "agent.h"
#include "prompt.h"
#include "provider.h"
#include "tools.h"
#include "util.h"

/* load session history + this turn's system prompt into a messages array */
static cJSON *build_messages(const config_t *cfg, session_t *sess,
			     const char *system_prompt)
{
	cJSON *msgs = cJSON_CreateArray();
	cJSON *sys = cJSON_CreateObject();
	cJSON_AddStringToObject(sys, "role", "system");
	cJSON_AddStringToObject(sys, "content", system_prompt);
	cJSON_AddItemToArray(msgs, sys);

	cJSON *hist = session_load(sess);
	int n = cJSON_GetArraySize(hist);
	int start = (cfg->max_history > 0 && n > cfg->max_history)
			? n - cfg->max_history : 0;
	for (int i = start; i < n; i++)
		cJSON_AddItemToArray(msgs, cJSON_Duplicate(cJSON_GetArrayItem(hist, i), 1));
	cJSON_Delete(hist);
	(void)cfg;
	return msgs;
}

int agent_turn(const config_t *cfg, session_t *sess, const char *user_prompt,
	       char *err, size_t errsz, char **reply_out)
{
	if (reply_out)
		*reply_out = NULL;
	/* persist + append user message */
	cJSON *user_msg = cJSON_CreateObject();
	cJSON_AddStringToObject(user_msg, "role", "user");
	cJSON_AddStringToObject(user_msg, "content", user_prompt);
	session_append(sess, user_msg);

	cJSON *tools = tools_build_defs();

	/* live messages array: system + history (incl. the new user msg) */
	char *system_prompt = prompt_build_system(cfg);
	if (!system_prompt) {
		snprintf(err, errsz, "out of memory");
		cJSON_Delete(tools);
		return -1;
	}
	cJSON *msgs = build_messages(cfg, sess, system_prompt);
	free(system_prompt);

	int ret = -1;
	for (int iter = 0; iter < cfg->max_tool_iters; iter++) {
		pc_resp_t resp;
		int rc = provider_chat(cfg, msgs, tools, &resp, err, errsz, stdout);
		if (rc != 0)
			goto out;

		if (resp.n_tcs == 0) {
			/* final answer */
			cJSON *am = cJSON_CreateObject();
			cJSON_AddStringToObject(am, "role", "assistant");
			cJSON_AddStringToObject(am, "content",
						resp.content ? resp.content : "");
			session_append(sess, am);
			cJSON_Delete(am);
			if (!cfg->stream && resp.content) {
				/* non-stream: content not yet printed */
				fputs(resp.content, stdout);
				fputc('\n', stdout);
				fflush(stdout);
			} else {
				fputc('\n', stdout);
				fflush(stdout);
			}
			if (reply_out)
				*reply_out = resp.content
						 ? strdup(resp.content) : NULL;
			ret = 0;
			pc_resp_free(&resp);
			goto out;
		}

		/* assistant message carrying tool_calls */
		cJSON *am = cJSON_CreateObject();
		cJSON_AddStringToObject(am, "role", "assistant");
		cJSON_AddStringToObject(am, "content", resp.content ? resp.content : "");
		cJSON *tcs_json = cJSON_CreateArray();
		for (int i = 0; i < resp.n_tcs; i++) {
			cJSON *t = cJSON_CreateObject();
			cJSON_AddStringToObject(t, "id",
						resp.tcs[i].id ? resp.tcs[i].id : "");
			cJSON_AddStringToObject(t, "type", "function");
			cJSON *fn = cJSON_CreateObject();
			cJSON_AddStringToObject(fn, "name",
						resp.tcs[i].name ? resp.tcs[i].name : "");
			cJSON_AddStringToObject(fn, "arguments",
						resp.tcs[i].arguments ? resp.tcs[i].arguments : "{}");
			cJSON_AddItemToObject(t, "function", fn);
			cJSON_AddItemToArray(tcs_json, t);
		}
		cJSON_AddItemToObject(am, "tool_calls", tcs_json);
		session_append(sess, am);
		cJSON_AddItemToArray(msgs, am); /* takes ownership */

		/* execute each tool call, append role:"tool" results */
		for (int i = 0; i < resp.n_tcs; i++) {
			const char *tname = resp.tcs[i].name ? resp.tcs[i].name : "?";
			if (cfg->show_tool_calls)
				fprintf(stderr, "[tool] %s\n", tname);
			char *result = NULL;
			char terr[512] = "";
			int trc = tools_execute(cfg, tname, resp.tcs[i].arguments,
						&result, terr, sizeof(terr));
			cJSON *tm = cJSON_CreateObject();
			cJSON_AddStringToObject(tm, "role", "tool");
			cJSON_AddStringToObject(tm, "tool_call_id",
						resp.tcs[i].id ? resp.tcs[i].id : "");
			if (trc == 0) {
				cJSON_AddStringToObject(tm, "content",
							result ? result : "");
			} else {
				char ebuf[600];
				snprintf(ebuf, sizeof(ebuf), "ERROR: %s", terr);
				cJSON_AddStringToObject(tm, "content", ebuf);
			}
			if (result) {
				util_utf8_sanitize(result);
				cJSON *cj = cJSON_GetObjectItem(tm, "content");
				if (cj && cj->valuestring)
					util_utf8_sanitize(cj->valuestring);
			}
			session_append(sess, tm);
			cJSON_AddItemToArray(msgs, tm); /* ownership */
			free(result);
		}
		pc_resp_free(&resp);
	}
	/* exhausted iterations */
	fprintf(stderr, "[max tool iterations (%d) reached]\n", cfg->max_tool_iters);
	ret = 0;

out:
	cJSON_Delete(msgs);
	cJSON_Delete(tools);
	return ret;
}
