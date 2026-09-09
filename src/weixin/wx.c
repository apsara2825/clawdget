/* clawdget: wx.c - WeChat (Tencent iLink) channel
 * QR login, long-polling gateway loop, per-user sessions.
 * Compiled only with -DPC_WEIXIN (make WEIXIN=1). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <curl/curl.h>
#include "cJSON.h"
#include "wx.h"
#include "wx_api.h"
#include "config.h"
#include "session.h"
#include "agent.h"
#include "spinner.h"
#include "util.h"
#include "qrcodegen.h"
#include "gateway.h"
#include <pthread.h>

#define WX_STATE_DIR_NAME "weixin"
#define WX_BOT_TYPE       "3"
#define WX_POLL_ERR_BACKOFF 5   /* seconds after a failed poll */
#define WX_RECENT_MAX_TOKENS 64

#define g_stop pc_gateway_stop

/* ---------------- state helpers ---------------- */

static void wx_state_dir(const config_t *cfg, char *buf, size_t sz)
{
	snprintf(buf, sz, "%s/" WX_STATE_DIR_NAME, cfg->home);
}

static char *wx_read_file_line(const char *path)
{
	size_t len;
	int trunc;
	char *s = util_read_file(path, 65536, &len, &trunc);
	if (!s)
		return NULL;
	/* strip trailing newlines */
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
		s[--len] = 0;
	return s;
}

static int wx_write_file_line(const char *path, const char *content)
{
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *fp = fopen(tmp, "w");
	if (!fp)
		return -1;
	fputs(content ? content : "", fp);
	fputc('\n', fp);
	fclose(fp);
	return rename(tmp, path);
}

/* context tokens: userid\ttoken per line, bounded to WX_RECENT_MAX_TOKENS */
typedef struct {
	char user[128];
	char token[512];
} wx_ctoken_t;

static wx_ctoken_t g_ctokens[WX_RECENT_MAX_TOKENS];
static int g_n_ctokens;

static void ctokens_load(const config_t *cfg)
{
	g_n_ctokens = 0;
	char path[1024];
	snprintf(path, sizeof(path), "%s/" WX_STATE_DIR_NAME "/context_tokens.txt",
		 cfg->home);
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	char *line;
	while (g_n_ctokens < WX_RECENT_MAX_TOKENS &&
	       (line = util_read_line(fp)) != NULL) {
		char *tab = strchr(line, '\t');
		if (tab && tab > line && tab[1]) {
			*tab = 0;
			snprintf(g_ctokens[g_n_ctokens].user,
				 sizeof(g_ctokens[0].user), "%s", line);
			snprintf(g_ctokens[g_n_ctokens].token,
				 sizeof(g_ctokens[0].token), "%s", tab + 1);
			g_n_ctokens++;
		}
		free(line);
	}
	fclose(fp);
}

static void ctokens_save(const config_t *cfg)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/" WX_STATE_DIR_NAME, cfg->home);
	util_mkdir_p(path);
	snprintf(path, sizeof(path), "%s/" WX_STATE_DIR_NAME "/context_tokens.txt",
		 cfg->home);
	FILE *fp = fopen(path, "w");
	if (!fp)
		return;
	for (int i = 0; i < g_n_ctokens; i++)
		fprintf(fp, "%s\t%s\n", g_ctokens[i].user, g_ctokens[i].token);
	fclose(fp);
}

static const char *ctoken_get(const char *user)
{
	for (int i = g_n_ctokens - 1; i >= 0; i--)
		if (!strcmp(g_ctokens[i].user, user))
			return g_ctokens[i].token;
	return NULL;
}

static void ctoken_put(const config_t *cfg, const char *user, const char *token)
{
	for (int i = 0; i < g_n_ctokens; i++) {
		if (!strcmp(g_ctokens[i].user, user)) {
			snprintf(g_ctokens[i].token,
				 sizeof(g_ctokens[0].token), "%s", token);
			ctokens_save(cfg);
			return;
		}
	}
	if (g_n_ctokens < WX_RECENT_MAX_TOKENS) {
		snprintf(g_ctokens[g_n_ctokens].user,
			 sizeof(g_ctokens[0].user), "%s", user);
		snprintf(g_ctokens[g_n_ctokens].token,
			 sizeof(g_ctokens[0].token), "%s", token);
		g_n_ctokens++;
	} else {
		memmove(&g_ctokens[0], &g_ctokens[1],
			sizeof(wx_ctoken_t) * (WX_RECENT_MAX_TOKENS - 1));
		snprintf(g_ctokens[WX_RECENT_MAX_TOKENS - 1].user,
			 sizeof(g_ctokens[0].user), "%s", user);
		snprintf(g_ctokens[WX_RECENT_MAX_TOKENS - 1].token,
			 sizeof(g_ctokens[0].token), "%s", token);
	}
	ctokens_save(cfg);
}

/* ---------------- send ---------------- */

static char *rand_hex(int n)
{
	static const char hexd[] = "0123456789abcdef";
	char *s = malloc((size_t)n + 1);
	unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(long)s;
	for (int i = 0; i < n; i++) {
		seed = seed * 1103515245u + 12345u;
		s[i] = hexd[(seed >> 16) & 0xf];
	}
	s[n] = 0;
	return s;
}

static int wx_send_text(const wx_api_t *api, const char *to_user,
			const char *context_token, const char *text,
			char *err, size_t errsz)
{
	char *cid = rand_hex(16);
	cJSON *body = cJSON_CreateObject();
	cJSON *msg = cJSON_CreateObject();
	cJSON_AddStringToObject(msg, "to_user_id", to_user);
	{
		char cidbuf[64];
		snprintf(cidbuf, sizeof(cidbuf), "clawdget-%s", cid);
		cJSON_AddStringToObject(msg, "client_id", cidbuf);
	}
	free(cid);
	cJSON_AddNumberToObject(msg, "message_type", WX_MSG_BOT);
	cJSON_AddNumberToObject(msg, "message_state", 2 /* finish */);
	cJSON *items = cJSON_CreateArray();
	cJSON *item = cJSON_CreateObject();
	cJSON_AddNumberToObject(item, "type", WX_ITEM_TEXT);
	cJSON *ti = cJSON_CreateObject();
	cJSON_AddStringToObject(ti, "text", text);
	cJSON_AddItemToObject(item, "text_item", ti);
	cJSON_AddItemToArray(items, item);
	cJSON_AddItemToObject(msg, "item_list", items);
	if (context_token && *context_token)
		cJSON_AddStringToObject(msg, "context_token", context_token);
	cJSON_AddItemToObject(body, "msg", msg);
	cJSON *bi = cJSON_CreateObject();
	cJSON_AddStringToObject(bi, "channel_version", WX_CHANNEL_VERSION);
	cJSON_AddItemToObject(body, "base_info", bi);

	char *req = cJSON_PrintUnformatted(body);
	cJSON_Delete(body);
	if (!req) {
		snprintf(err, errsz, "oom");
		return -1;
	}
	char *resp = wx_post(api, "ilink/bot/sendmessage", req, err, errsz, 0);
	free(req);
	if (!resp)
		return -1;
	int bad = 0;
	cJSON *j = cJSON_Parse(resp);
	if (j) {
		cJSON *ret = cJSON_GetObjectItem(j, "ret");
		cJSON *ec = cJSON_GetObjectItem(j, "errcode");
		if ((cJSON_IsNumber(ret) && ret->valuedouble != 0) ||
		    (cJSON_IsNumber(ec) && ec->valuedouble != 0)) {
			cJSON *em = cJSON_GetObjectItem(j, "errmsg");
			snprintf(err, errsz, "sendmessage: ret=%s errcode=%s",
				 cJSON_IsNumber(ret) ? "!" : "!",
				 cJSON_IsString(em) ? em->valuestring : "?");
			bad = 1;
		}
		cJSON_Delete(j);
	} else {
		snprintf(err, errsz, "bad sendmessage response");
		bad = 1;
	}
	free(resp);
	return bad ? -1 : 0;
}

/* ---------------- QR login ---------------- */

static void print_qr_terminal(const char *text)
{
	uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
	uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
	if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW,
				  1, 40, qrcodegen_Mask_AUTO, true)) {
		fprintf(stderr, "qr encode failed\n");
		return;
	}
	int size = qrcodegen_getSize(qr);
	/* pure background-color rendering (spaces only): no glyph/shape
	 * dependence, identical on every terminal and font.
	 * two text rows per QR row keep the cells square. */
	const char *B = "\033[48;5;0m  ";
	const char *W = "\033[48;5;15m  ";
	for (int yy = -4; yy < size + 4; yy++) {
		for (int rep = 0; rep < 2; rep++) {
			for (int x = -4; x < size + 4; x++) {
				int dark = (x >= 0 && x < size && yy >= 0 && yy < size)
					       ? qrcodegen_getModule(qr, x, yy) : 0;
				fputs(dark ? B : W, stdout);
			}
			fputs("\033[0m\n", stdout);
		}
	}
	fflush(stdout);
}

static char *wx_url_escape(const char *s)
{
	return curl_easy_escape(NULL, s, 0);
}

int wx_auth_login(const config_t *cfg)
{
	wx_api_t api;
	wx_api_set_abort(&g_stop);
	wx_api_init(&api, NULL, "");

	fprintf(stderr, "==> requesting WeChat QR code...\n");
	char err[512] = "";
	char *resp = wx_get(&api, "ilink/bot/get_bot_qrcode?bot_type=" WX_BOT_TYPE,
			    err, sizeof(err), 1);
	if (!resp) {
		fprintf(stderr, "error: %s\n", err);
		return 1;
	}
	cJSON *j = cJSON_Parse(resp);
	free(resp);
	if (!j) {
		fprintf(stderr, "error: bad qrcode response\n");
		return 1;
	}
	cJSON *qr = cJSON_GetObjectItem(j, "qrcode");
	cJSON *img = cJSON_GetObjectItem(j, "qrcode_img_content");
	if (!cJSON_IsString(qr) || !cJSON_IsString(img)) {
		fprintf(stderr, "error: qrcode response missing fields\n");
		cJSON_Delete(j);
		return 1;
	}
	char qrcode[1024], img_content[2048];
	snprintf(qrcode, sizeof(qrcode), "%s", qr->valuestring);
	snprintf(img_content, sizeof(img_content), "%s", img->valuestring);
	cJSON_Delete(j);

	fprintf(stderr, "\n用微信扫描下面的二维码登录：\n\n");
	print_qr_terminal(img_content);
	fprintf(stderr, "\n二维码链接（手机浏览器打开也行）: %s\n\n", img_content);

	char base_url[256] = "";
	char poll_base[256];
	snprintf(poll_base, sizeof(poll_base), "%s", api.base_url);
	time_t deadline = time(NULL) + 300;
	int scanned_printed = 0;
	while (time(NULL) < deadline && !g_stop) {
		sleep(2);
		char *esc = wx_url_escape(qrcode);
		char ep[1200];
		snprintf(ep, sizeof(ep), "ilink/bot/get_qrcode_status?qrcode=%s",
			 esc ? esc : qrcode);
		curl_free(esc);
		resp = wx_get(&api, ep, err, sizeof(err), 1);
		if (!resp)
			continue;
		j = cJSON_Parse(resp);
		free(resp);
		if (!j)
			continue;
		cJSON *st = cJSON_GetObjectItem(j, "status");
		const char *status = cJSON_IsString(st) ? st->valuestring : "";
		if (!strcmp(status, "scaned") && !scanned_printed) {
			fprintf(stderr, "已扫码，请在微信上确认登录...\n");
			scanned_printed = 1;
		} else if (!strcmp(status, "scaned_but_redirect")) {
			cJSON *rh = cJSON_GetObjectItem(j, "redirect_host");
			if (cJSON_IsString(rh) && rh->valuestring[0]) {
				snprintf(poll_base, sizeof(poll_base),
					 "https://%s/", rh->valuestring);
				wx_api_init(&api, poll_base, "");
				fprintf(stderr, "切换二维码轮询主机: %s\n",
					rh->valuestring);
			}
		} else if (!strcmp(status, "confirmed")) {
			cJSON *tk = cJSON_GetObjectItem(j, "bot_token");
			cJSON *uid = cJSON_GetObjectItem(j, "ilink_bot_id");
			cJSON *bu = cJSON_GetObjectItem(j, "baseurl");
			if (!cJSON_IsString(tk) || !tk->valuestring[0]) {
				fprintf(stderr, "error: 缺少 bot_token\n");
				cJSON_Delete(j);
				return 1;
			}
			char sdir[1024];
			snprintf(sdir, sizeof(sdir), "%s/" WX_STATE_DIR_NAME,
				 cfg->home);
			util_mkdir_p(sdir);
			char tpath[1200];
			snprintf(tpath, sizeof(tpath), "%s/token.txt", sdir);
			wx_write_file_line(tpath, tk->valuestring);
			if (cJSON_IsString(bu) && bu->valuestring[0]) {
				char bpath[1200];
				snprintf(bpath, sizeof(bpath), "%s/base_url.txt",
					 sdir);
				wx_write_file_line(bpath, bu->valuestring);
				snprintf(base_url, sizeof(base_url), "%s",
					 bu->valuestring);
			}
			fprintf(stderr,
				"\n✅ 登录成功！token 已保存到 %s\n"
				"机器人 ID: %s\n"
				"现在可以运行: clawdget gateway\n",
				tpath, cJSON_IsString(uid) ? uid->valuestring : "?");
			cJSON_Delete(j);
			return 0;
		} else if (!strcmp(status, "expired")) {
			fprintf(stderr, "二维码已过期，请重新运行登录命令\n");
			cJSON_Delete(j);
			return 1;
		}
		cJSON_Delete(j);
	}
	fprintf(stderr, "登录超时\n");
	return 1;
}

/* ---------------- gateway ---------------- */

static int wx_allowed(const config_t *cfg, const char *user)
{
	if (cfg->wx_n_allow == 0)
		return 1;
	for (int i = 0; i < cfg->wx_n_allow; i++)
		if (!strcmp(cfg->wx_allow[i], user))
			return 1;
	return 0;
}

static void sanitize_session_key(const char *user, char *out, size_t sz)
{
	size_t j = 0;
	snprintf(out, sz, "wx-");
	j = strlen(out);
	for (const char *p = user; *p && j < sz - 1; p++)
		out[j++] = ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')
			       ? *p : '_';
	out[j] = 0;
}

static int wx_have_saved_token(const config_t *cfg)
{
	char tpath[1024];
	snprintf(tpath, sizeof(tpath), "%s/" WX_STATE_DIR_NAME "/token.txt",
		 cfg->home);
	size_t len;
	int trunc;
	char *t = util_read_file(tpath, 4096, &len, &trunc);
	if (!t)
		return 0;
	free(t);
	return 1;
}

static void *wx_channel_loop(void *arg);

int wx_channel_thread_start(const config_t *cfg, pthread_t *tid)
{
	if ((!cfg->wx_token || !cfg->wx_token[0]) && !wx_have_saved_token(cfg))
		return -1;
	return pthread_create(tid, NULL, wx_channel_loop, (void *)cfg);
}

static void *wx_channel_loop(void *arg)
{
	const config_t *cfg = arg;
	char token[512] = "";
	if (cfg->wx_token && cfg->wx_token[0]) {
		snprintf(token, sizeof(token), "%s", cfg->wx_token);
	} else {
		char tpath[1024];
		snprintf(tpath, sizeof(tpath), "%s/" WX_STATE_DIR_NAME "/token.txt",
			 cfg->home);
		char *t = wx_read_file_line(tpath);
		if (t) {
			snprintf(token, sizeof(token), "%s", t);
			free(t);
		}
	}
	if (!token[0]) {
		fprintf(stderr,
			"error: 微信未登录。先运行: clawdget auth weixin\n");
		return 1;
	}

	char base_url[256] = "";
	{
		char bpath[1024];
		snprintf(bpath, sizeof(bpath),
			 "%s/" WX_STATE_DIR_NAME "/base_url.txt", cfg->home);
		char *b = wx_read_file_line(bpath);
		if (b) {
			snprintf(base_url, sizeof(base_url), "%s", b);
			free(b);
		}
	}

	wx_api_t api;
	wx_api_init(&api, base_url, token);
	ctokens_load(cfg);

	char sbuf_path[1024];
	snprintf(sbuf_path, sizeof(sbuf_path),
		 "%s/" WX_STATE_DIR_NAME "/sync_buf.txt", cfg->home);
	char *sync_buf = wx_read_file_line(sbuf_path);

	fprintf(stderr, "[gateway] WeChat channel started (polling). Ctrl-C 退出\n");

	char err[512] = "";
	long counter = 0;
	while (!g_stop) {
		counter++;
		cJSON *body = cJSON_CreateObject();
		if (sync_buf && sync_buf[0])
			cJSON_AddStringToObject(body, "get_updates_buf", sync_buf);
		cJSON *bi = cJSON_CreateObject();
		cJSON_AddStringToObject(bi, "channel_version", WX_CHANNEL_VERSION);
		cJSON_AddItemToObject(body, "base_info", bi);
		char *req = cJSON_PrintUnformatted(body);
		cJSON_Delete(body);
		if (!req) {
			sleep(1);
			continue;
		}
		err[0] = 0;
		char *resp = wx_post(&api, "ilink/bot/getupdates", req,
				     err, sizeof(err), 0);
		free(req);
		if (!resp) {
			fprintf(stderr, "[gateway] poll error: %s\n", err);
			for (int i = 0; i < WX_POLL_ERR_BACKOFF * 2 && !g_stop; i++)
				usleep(500 * 1000);
			continue;
		}

		cJSON *j = cJSON_Parse(resp);
		free(resp);
		if (!j) {
			free(sync_buf);
			sync_buf = NULL;
			usleep(300 * 1000);
			continue;
		}
		cJSON *ub = cJSON_GetObjectItem(j, "get_updates_buf");
		if (cJSON_IsString(ub) && ub->valuestring[0]) {
			free(sync_buf);
			sync_buf = strdup(ub->valuestring);
			wx_write_file_line(sbuf_path, sync_buf);
		}
		cJSON *ret = cJSON_GetObjectItem(j, "ret");
		cJSON *ec = cJSON_GetObjectItem(j, "errcode");
		if ((cJSON_IsNumber(ret) && ret->valuedouble != 0) ||
		    (cJSON_IsNumber(ec) && ec->valuedouble != 0)) {
			cJSON *em = cJSON_GetObjectItem(j, "errmsg");
			fprintf(stderr, "[gateway] API error: %s\n",
				cJSON_IsString(em) ? em->valuestring : "?");
			cJSON_Delete(j);
			for (int i = 0; i < WX_POLL_ERR_BACKOFF * 4 && !g_stop; i++)
				usleep(500 * 1000);
			continue;
		}

		cJSON *msgs = cJSON_GetObjectItem(j, "msgs");
		if (cJSON_IsArray(msgs)) {
			int n = cJSON_GetArraySize(msgs);
			for (int i = 0; i < n && !g_stop; i++) {
				cJSON *m = cJSON_GetArrayItem(msgs, i);
				cJSON *mt = cJSON_GetObjectItem(m, "message_type");
				if (!cJSON_IsNumber(mt) ||
				    (int)mt->valuedouble != WX_MSG_USER)
					continue;
				cJSON *fu = cJSON_GetObjectItem(m, "from_user_id");
				if (!cJSON_IsString(fu) || !fu->valuestring[0])
					continue;
				const char *from = fu->valuestring;
				if (!wx_allowed(cfg, from)) {
					fprintf(stderr,
						"[gateway] %s 不在白名单，忽略\n",
						from);
					continue;
				}
				/* build text from item_list */
				char text[4096];
				size_t tl = 0;
				text[0] = 0;
				cJSON *items = cJSON_GetObjectItem(m, "item_list");
				if (cJSON_IsArray(items)) {
					int in = cJSON_GetArraySize(items);
					for (int k = 0; k < in; k++) {
						cJSON *it = cJSON_GetArrayItem(items, k);
						cJSON *it_t = cJSON_GetObjectItem(it, "type");
						int ity = cJSON_IsNumber(it_t)
							      ? (int)it_t->valuedouble : 0;
						const char *piece = NULL;
						char fb[128];
						if (ity == WX_ITEM_TEXT) {
							cJSON *ti = cJSON_GetObjectItem(it, "text_item");
							cJSON *tx = ti ? cJSON_GetObjectItem(ti, "text") : NULL;
							if (cJSON_IsString(tx))
								piece = tx->valuestring;
						} else if (ity == 3) {
							cJSON *vi = cJSON_GetObjectItem(it, "voice_item");
							cJSON *tx = vi ? cJSON_GetObjectItem(vi, "text") : NULL;
							piece = cJSON_IsString(tx) && tx->valuestring[0]
								? tx->valuestring : "[audio]";
						} else if (ity == 2) {
							piece = "[image]";
						} else if (ity == 4) {
							cJSON *fi = cJSON_GetObjectItem(it, "file_item");
							cJSON *fn = fi ? cJSON_GetObjectItem(fi, "file_name") : NULL;
							snprintf(fb, sizeof(fb), "[file: %s]",
								 cJSON_IsString(fn) ? fn->valuestring : "?");
							piece = fb;
						} else if (ity == 5) {
							piece = "[video]";
						}
						if (!piece || !*piece)
							continue;
						size_t pl = strlen(piece);
						if (tl + pl + 2 >= sizeof(text))
							break;
						if (tl)
							text[tl++] = '\n';
						memcpy(text + tl, piece, pl + 1);
						tl += pl;
					}
				}
				if (!text[0])
					continue;

				cJSON *ct = cJSON_GetObjectItem(m, "context_token");
				if (cJSON_IsString(ct) && ct->valuestring[0])
					ctoken_put(cfg, from, ct->valuestring);

				fprintf(stderr, "[gateway] %s: %.80s\n", from, text);

				char sesskey[160];
				sanitize_session_key(from, sesskey, sizeof(sesskey));
				session_t sess;
				if (session_open(&sess, cfg, sesskey, NULL) != 0)
					continue;
				char aerr[512] = "";
				char *reply = NULL;
				int arc = agent_turn(cfg, &sess, text, aerr,
						     sizeof(aerr), &reply);
				session_close(&sess);
				if (arc != 0) {
					fprintf(stderr, "[gateway] agent error: %s\n",
						aerr);
				} else if (reply && reply[0]) {
					char send_err[512] = "";
					const char *ctk = ctoken_get(from);
					if (wx_send_text(&api, from, ctk ? ctk : "",
							 reply, send_err,
							 sizeof(send_err)) != 0)
						fprintf(stderr,
							"[gateway] 发送失败: %s\n",
							send_err);
					else
						fprintf(stderr, "[gateway] -> 已回复\n");
				}
				free(reply);
			}
		}
		cJSON_Delete(j);
		if (!g_stop)
			usleep(300 * 1000);
	}

	free(sync_buf);
	ctokens_save(cfg);
	fprintf(stderr, "\n[wx] 渠道退出\n");
	return NULL;
}
