/* clawdget: tg.c - Telegram channel: long-polling loop, per-chat sessions.
 * Compiled only with -DPC_TELEGRAM (make TELEGRAM=1). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include "cJSON.h"
#include "tg_api.h"
#include "config.h"
#include "session.h"
#include "agent.h"
#include "util.h"
#include "gateway.h"

/* thread entry (called by gateway.c) */
int tg_channel_thread_start(const config_t *cfg, pthread_t *tid);

static long g_offset = 0;

static int tg_send_one(const tg_api_t *api, long chat_id, const char *text,
		       char *err, size_t errsz)
{
	cJSON *body = cJSON_CreateObject();
	cJSON_AddNumberToObject(body, "chat_id", chat_id);
	cJSON_AddStringToObject(body, "text", text);
	char *req = cJSON_PrintUnformatted(body);
	cJSON_Delete(body);
	if (!req) {
		snprintf(err, errsz, "oom");
		return -1;
	}
	char *resp = tg_post(api, "sendMessage", req, err, errsz);
	free(req);
	if (!resp)
		return -1;
	int bad = 0;
	cJSON *j = cJSON_Parse(resp);
	if (j) {
		cJSON *ok = cJSON_GetObjectItem(j, "ok");
		if (ok == NULL || !cJSON_IsTrue(ok))
			bad = 1;
		cJSON_Delete(j);
	} else {
		snprintf(err, errsz, "bad sendMessage response");
		bad = 1;
	}
	free(resp);
	return bad ? -1 : 0;
}

/* Telegram hard limit: 4096 UTF-16 code units per message. Split at a
 * UTF-8 character boundary so multi-byte chars never break. */
static int tg_send_text(const tg_api_t *api, long chat_id, const char *text,
			char *err, size_t errsz)
{
	size_t total = strlen(text);
	if (total <= 4000)
		return tg_send_one(api, chat_id, text, err, errsz);

	const char *p = text;
	int part = 0, rc = 0;
	while (*p && rc == 0) {
		size_t len = 0;
		while (len < 3800 && p[len]) {
			unsigned char c = (unsigned char)p[len];
			size_t cw = (c < 0x80) ? 1 : (c & 0xE0) == 0xC0 ? 2
				  : (c & 0xF0) == 0xE0 ? 3
				  : (c & 0xF8) == 0xF0 ? 4 : 1;
			if (len + cw > 3800)
				break;
			len += cw;
		}
		char chunk[4000];
		memcpy(chunk, p, len);
		chunk[len] = 0;
		part++;
		if (part > 1 || *p != text[0] || len != total)
			; /* multi-part: no marker to keep text clean */
		rc = tg_send_one(api, chat_id, chunk, err, errsz);
		p += len;
	}
	return rc;
}

static int tg_allowed(const config_t *cfg, const char *uid,
		      const char *username)
{
	if (cfg->tg_n_allow == 0)
		return 1;
	for (int i = 0; i < cfg->tg_n_allow; i++) {
		const char *a = cfg->tg_allow[i];
		if (!strcmp(a, uid))
			return 1;
		if (username && *username) {
			if (a[0] == '@' ? !strcmp(a + 1, username)
					: !strcmp(a, username))
				return 1;
		}
	}
	return 0;
}

static void sanitize_session_key(const char *uid, char *out, size_t sz)
{
	size_t j = snprintf(out, sz, "tg-");
	for (const char *p = uid; *p && j < sz - 1; p++)
		out[j++] = ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')
			       ? *p : '_';
	out[j] = 0;
}

static void *tg_channel_loop(void *arg)
{
	const config_t *cfg = arg;
	tg_api_t api;
	tg_api_set_abort((volatile int *)&pc_gateway_stop);
	tg_api_init(&api, cfg->tg_base, cfg->tg_token, cfg->tg_proxy);

	char sbuf_path[1024];
	char tdir[1024];
	snprintf(tdir, sizeof(tdir), "%s/telegram", cfg->home);
	util_mkdir_p(tdir);
	snprintf(sbuf_path, sizeof(sbuf_path), "%s/telegram/offset.txt",
		 cfg->home);
	{
		size_t len;
		int trunc;
		char *s = util_read_file(sbuf_path, 64, &len, &trunc);
		if (s) {
			g_offset = strtol(s, NULL, 10);
			free(s);
		}
	}

	fprintf(stderr, "[tg] Telegram 渠道开始轮询\n");
	char err[512] = "";

	while (!gateway_should_stop()) {
		cJSON *body = cJSON_CreateObject();
		cJSON_AddNumberToObject(body, "offset", g_offset);
		cJSON_AddNumberToObject(body, "timeout", 25); /* long poll */
		char *req = cJSON_PrintUnformatted(body);
		cJSON_Delete(body);
		if (!req) {
			sleep(1);
			continue;
		}
		err[0] = 0;
		char *resp = tg_post(&api, "getUpdates", req, err, sizeof(err));
		free(req);
		if (gateway_should_stop()) {
			free(resp);
			break;
		}
		if (!resp) {
			if (strcmp(err, "aborted")) {
				fprintf(stderr, "[tg] poll error: %s\n", err);
				for (int i = 0; i < 10 && !gateway_should_stop(); i++)
					usleep(500 * 1000);
			}
			continue;
		}

		cJSON *j = cJSON_Parse(resp);
		free(resp);
		if (!j)
			continue;
		cJSON *ok = cJSON_GetObjectItem(j, "ok");
		if (cJSON_IsTrue(ok) == NULL) {
			cJSON *desc = cJSON_GetObjectItem(j, "description");
			fprintf(stderr, "[tg] API error: %s\n",
				cJSON_IsString(desc) ? desc->valuestring : "?");
			cJSON_Delete(j);
			for (int i = 0; i < 10 && !gateway_should_stop(); i++)
				usleep(500 * 1000);
			continue;
		}

		cJSON *result = cJSON_GetObjectItem(j, "result");
		if (cJSON_IsArray(result)) {
			int n = cJSON_GetArraySize(result);
			for (int i = 0; i < n && !gateway_should_stop(); i++) {
				cJSON *up = cJSON_GetArrayItem(result, i);
				cJSON *uid = cJSON_GetObjectItem(up, "update_id");
				if (cJSON_IsNumber(uid))
					g_offset = (long)uid->valuedouble + 1;

				cJSON *msg = cJSON_GetObjectItem(up, "message");
				if (!cJSON_IsObject(msg))
					continue;
				/* text or placeholder, mirroring picoclaw:
				 * media messages become [photo]/[voice]/... */
				char mtext[512];
				{
					cJSON *tx = cJSON_GetObjectItem(msg, "text");
					cJSON *cap = cJSON_GetObjectItem(msg, "caption");
					if (cJSON_IsString(tx) && tx->valuestring[0]) {
						snprintf(mtext, sizeof(mtext), "%s", tx->valuestring);
					} else if (cJSON_IsString(cap) && cap->valuestring[0]) {
						snprintf(mtext, sizeof(mtext), "%s", cap->valuestring);
					} else if (cJSON_GetObjectItem(msg, "photo")) {
						snprintf(mtext, sizeof(mtext), "[图片]");
					} else if (cJSON_GetObjectItem(msg, "voice")) {
						snprintf(mtext, sizeof(mtext), "[语音]");
					} else if (cJSON_GetObjectItem(msg, "audio")) {
						snprintf(mtext, sizeof(mtext), "[音频]");
					} else if (cJSON_GetObjectItem(msg, "document")) {
						cJSON *doc = cJSON_GetObjectItem(msg, "document");
						cJSON *fn = cJSON_GetObjectItem(doc, "file_name");
						snprintf(mtext, sizeof(mtext), "[文件: %s]",
							 cJSON_IsString(fn) ? fn->valuestring : "?");
					} else if (cJSON_GetObjectItem(msg, "sticker")) {
						snprintf(mtext, sizeof(mtext), "[贴纸]");
					} else {
						continue; /* unrelated update */
					}
				}
				const char *text = mtext;
				cJSON *chat = cJSON_GetObjectItem(msg, "chat");
				cJSON *cid = cJSON_GetObjectItem(chat, "id");
				if (!cJSON_IsNumber(cid))
					continue;
				long chat_id = (long)cid->valuedouble;
				char chat_str[32];
				snprintf(chat_str, sizeof(chat_str), "%ld",
					 chat_id);

				char who[160];
				cJSON *from = cJSON_GetObjectItem(msg, "from");
				cJSON *uname = from ? cJSON_GetObjectItem(from, "username") : NULL;
				cJSON *fname = from ? cJSON_GetObjectItem(from, "first_name") : NULL;
				snprintf(who, sizeof(who), "%s",
					 cJSON_IsString(uname) && uname->valuestring[0]
					     ? uname->valuestring
					     : (cJSON_IsString(fname) ? fname->valuestring
								      : chat_str));

				if (!tg_allowed(cfg, chat_str,
						cJSON_IsString(uname) ? uname->valuestring : "")) {
					fprintf(stderr, "[tg] %s 不在白名单，忽略\n",
						who);
					continue;
				}

				fprintf(stderr, "[tg] %s: %.80s\n", who,
					text);

				char sesskey[160];
				sanitize_session_key(chat_str, sesskey,
						     sizeof(sesskey));
				session_t sess;
				if (session_open(&sess, cfg, sesskey, NULL) != 0)
					continue;
				char aerr[512] = "";
				char *reply = NULL;
				int arc = agent_turn(cfg, &sess, text,
						     aerr, sizeof(aerr), &reply);
				session_close(&sess);
				if (arc != 0) {
					fprintf(stderr, "[tg] agent error: %s\n",
						aerr);
				} else if (reply && reply[0]) {
					if (tg_send_text(&api, chat_id, reply,
							 err, sizeof(err)) != 0)
						fprintf(stderr,
							"[tg] 发送失败: %s\n", err);
					else
						fprintf(stderr,
							"[tg] -> 已回复\n");
				}
				free(reply);
			}
		}
		cJSON_Delete(j);

		/* persist offset */
		char ob[32];
		snprintf(ob, sizeof(ob), "%ld", g_offset);
		util_write_line(sbuf_path, ob);
		if (!gateway_should_stop())
			usleep(200 * 1000);
	}
	fprintf(stderr, "\n[tg] 渠道退出\n");
	return NULL;
}

int tg_channel_thread_start(const config_t *cfg, pthread_t *tid)
{
	if (!cfg->tg_token || !cfg->tg_token[0])
		return -1;
	return pthread_create(tid, NULL, tg_channel_loop, (void *)cfg);
}
