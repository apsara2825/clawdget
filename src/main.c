/* clawdget: main.c - CLI entry: one-shot prompt / interactive REPL
 *
 * Usage:
 *   clawdget [options] [prompt ...]   one-shot: answer a single prompt, exit
 *   clawdget [options]                no prompt -> interactive REPL
 *   clawdget ls                       list sessions
 *   clawdget rm <key>                 delete a session
 * Options:
 *   -c <path>   config file (default ~/.clawdget/config.json)
 *   -s <key>    session key to resume
 *   -n          start a new session
 *   -h          help
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <dirent.h>
#include <unistd.h>
#include "config.h"
#include "agent.h"
#include "session.h"
#include "util.h"
#include "http.h"
#ifdef PC_WEIXIN
#include "weixin/wx.h"
#endif
#include "prompt.h"

static void print_help(const char *prog)
{
	fprintf(stderr,
		"clawdget: ultra-light AI agent (MT7628/OpenWrt)\n\n"
		"Usage:\n"
		"  %s [options] [prompt ...]   one-shot prompt\n"
		"  %s [options]                interactive REPL\n"
		"  %s skill ls|add <url> [name]|rm <name>  manage skills\n"
#ifdef PC_WEIXIN
		"  %s gateway                 run the WeChat channel daemon\n"
		"  %s auth weixin             WeChat QR login\n"
#endif
		"  %s ls                       list sessions\n"
		"  %s rm <key>                 delete session\n\n"
		"Options:\n"
		"  -c <path>  config file (default ~/.clawdget/config.json)\n"
		"  -s <key>   resume session <key>\n"
		"  -n         start a new session\n"
		"  -y         auto mode: run tools without confirmation\n"
		"  -h         this help\n\n"
		"REPL commands: /new /ls /resume <key> /rm <key> /help /quit\n",
		prog, prog, prog, prog, prog
#ifdef PC_WEIXIN
		, prog, prog
#endif
		);
}

static void print_sessions(const config_t *cfg)
{
	cJSON *list = session_list(cfg);
	int n = cJSON_GetArraySize(list);
	if (n == 0) {
		printf("(no sessions)\n");
		cJSON_Delete(list);
		return;
	}
	for (int i = 0; i < n; i++) {
		cJSON *m = cJSON_GetArrayItem(list, i);
		cJSON *k = cJSON_GetObjectItem(m, "key");
		cJSON *c = cJSON_GetObjectItem(m, "count");
		cJSON *u = cJSON_GetObjectItem(m, "updated_at");
		printf("%-24s %4d msgs  %s\n",
		       cJSON_IsString(k) ? k->valuestring : "?",
		       cJSON_IsNumber(c) ? (int)c->valuedouble : 0,
		       cJSON_IsString(u) ? u->valuestring : "");
	}
	cJSON_Delete(list);
}

/* print numbered session list, read a choice, copy key into out.
 * returns 0 on success, -1 on cancel/invalid. */
static int choose_session(const config_t *cfg, char *out, size_t sz)
{
	cJSON *list = session_list(cfg);
	int n = cJSON_GetArraySize(list);
	if (n == 0) {
		printf("(no sessions)\n");
		cJSON_Delete(list);
		return -1;
	}
	for (int i = 0; i < n; i++) {
		cJSON *m = cJSON_GetArrayItem(list, i);
		cJSON *k = cJSON_GetObjectItem(m, "key");
		cJSON *c = cJSON_GetObjectItem(m, "count");
		cJSON *u = cJSON_GetObjectItem(m, "updated_at");
		printf("  %d) %-24s %4d msgs  %s\n", i + 1,
		       cJSON_IsString(k) ? k->valuestring : "?",
		       cJSON_IsNumber(c) ? (int)c->valuedouble : 0,
		       cJSON_IsString(u) ? u->valuestring : "");
	}
	fprintf(stderr, "select session [1-%d]: ", n);
	fflush(stderr);
	char *line = util_read_line(stdin);
	if (!line) {
		fputc('\n', stderr);
		return -1;
	}
	int pick = atoi(line);
	free(line);
	if (pick < 1 || pick > n) {
		fprintf(stderr, "invalid choice\n");
		return -1;
	}
	cJSON *m = cJSON_GetArrayItem(list, pick - 1);
	cJSON *k = cJSON_GetObjectItem(m, "key");
	snprintf(out, sz, "%s", cJSON_IsString(k) ? k->valuestring : "");
	cJSON_Delete(list);
	return 0;
}

/* print the last n user/assistant messages so resuming has context */
static void print_recent(session_t *sess, int n)
{
	cJSON *hist = session_load(sess);
	int total = cJSON_GetArraySize(hist);
	int shown = 0;
	int start = total - n;
	if (start < 0)
		start = 0;
	for (int i = start; i < total; i++) {
		cJSON *m = cJSON_GetArrayItem(hist, i);
		cJSON *r = cJSON_GetObjectItem(m, "role");
		cJSON *c = cJSON_GetObjectItem(m, "content");
		const char *role = cJSON_IsString(r) ? r->valuestring : "?";
		if (!cJSON_IsString(c) || !c->valuestring[0])
			continue; /* tool_calls carriers / tool outputs */
		char buf[80];
		size_t j = 0;
		for (const char *p = c->valuestring; *p && j < sizeof(buf) - 4; p++)
			buf[j++] = (*p == '\n' || *p == '\r') ? ' ' : *p;
		buf[j] = 0;
		fprintf(stderr, "  %s: %s%s\n", role, buf,
			strlen(c->valuestring) > (size_t)j ? "..." : "");
		shown++;
	}
	cJSON_Delete(hist);
	if (shown > 0)
		fprintf(stderr, "--- (%d recent message%s) ---\n", shown,
			shown > 1 ? "s" : "");
}

/* run one prompt through the agent; exits on error with message */
static void run_prompt(const config_t *cfg, session_t *sess, const char *prompt)
{
	char err[1024] = "";
	if (agent_turn(cfg, sess, prompt, err, sizeof(err), NULL) != 0) {
		fprintf(stderr, "error: %s\n", err);
	}
}

int main(int argc, char **argv)
{
	const char *cfg_path = NULL;
	const char *sess_key = NULL;
	int force_new = 0;
	int pick_mode = 0;
	int nargs = 0;
	int auto_mode = 0;
	const char **promptv = calloc((size_t)argc, sizeof(char *));

	/* pre-scan: -y works in any position (gateway branch returns early) */
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-y") || !strcmp(argv[i], "--auto"))
			auto_mode = 1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-c") && i + 1 < argc) {
			cfg_path = argv[++i];
		} else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
			sess_key = argv[++i];
		} else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--resume")) {
			/* optional following key; interactive pick otherwise */
			if (i + 1 < argc && argv[i + 1][0] != '-')
				sess_key = argv[++i];
			else
				pick_mode = 1;
		} else if (!strcmp(argv[i], "-n")) {
			force_new = 1;
		} else if (!strcmp(argv[i], "-y") || !strcmp(argv[i], "--auto")) {
			auto_mode = 1;
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			print_help(basename(argv[0]));
			free(promptv);
			return 0;
		} else if (!strcmp(argv[i], "skill") && i + 1 < argc) {
			const char *sub = argv[i + 1];
			config_t cfg;
			char serr[256];
			int created;
			if (config_load(&cfg, cfg_path, &created, serr, sizeof(serr)) != 0) {
				fprintf(stderr, "error: %s\n", serr);
				return 1;
			}
			char sdir[1024];
			snprintf(sdir, sizeof(sdir), "%s/skills", cfg.workspace);
			if (!strcmp(sub, "ls")) {
				DIR *d = opendir(sdir);
				if (!d) {
					printf("(no skills; add one with: clawdget skill add <url>)\n");
				} else {
					struct dirent *e;
					int n = 0;
					while ((e = readdir(d))) {
						if (e->d_name[0] == '.')
							continue;
						char desc[256];
						prompt_skill_desc(sdir, e->d_name, desc, sizeof(desc));
						printf("%-20s %s\n", e->d_name,
						       desc[0] ? desc : "(no description)");
						n++;
					}
					closedir(d);
					if (n == 0)
						printf("(no skills installed)\n");
				}
			} else if (!strcmp(sub, "add") && i + 2 < argc) {
				const char *url = argv[i + 2];
				char name[128];
				if (i + 3 < argc) {
					snprintf(name, sizeof(name), "%s", argv[i + 3]);
				} else {
					const char *b = strrchr(url, '/');
					b = b ? b + 1 : url;
					size_t bl = strlen(b);
					if (bl > 3 && !strcmp(b + bl - 3, ".md"))
						bl -= 3;
					if (bl == 0 || bl >= sizeof(name)) {
						fprintf(stderr, "error: cannot derive skill name from url (pass a name)\n");
						return 1;
					}
					memcpy(name, b, bl);
					name[bl] = 0;
				}
				char *body = NULL;
				pc_http_result res;
				int rc = http_get(url, NULL, 1 << 20, &body, &res);
				if (rc == -1) {
					fprintf(stderr, "error: %s\n", res.curl_err);
					return 1;
				}
				if (rc == -2) {
					fprintf(stderr, "error: HTTP %ld\n", res.http_code);
					free(body);
					return 1;
				}
				char dpath[1200];
				snprintf(dpath, sizeof(dpath), "%s/%s", sdir, name);
				util_mkdir_p(dpath);
				char fpath[1400];
				snprintf(fpath, sizeof(fpath), "%s/SKILL.md", dpath);
				FILE *fp = fopen(fpath, "w");
				if (!fp) {
					fprintf(stderr, "error: cannot write %s\n", fpath);
					return 1;
				}
				size_t bl2 = body ? strlen(body) : 0;
				fwrite(body, 1, bl2, fp);
				fclose(fp);
				printf("installed skill '%s' (%zu bytes) -> %s\n", name, bl2, fpath);
				free(body);
			} else if (!strcmp(sub, "rm") && i + 2 < argc) {
				char fpath[1400], dpath[1200];
				snprintf(dpath, sizeof(dpath), "%s/%s", sdir, argv[i + 2]);
				snprintf(fpath, sizeof(fpath), "%s/SKILL.md", dpath);
				if (unlink(fpath) == 0 && rmdir(dpath) == 0)
					printf("removed skill %s\n", argv[i + 2]);
				else
					printf("skill not found: %s\n", argv[i + 2]);
			} else {
				fprintf(stderr, "usage: clawdget skill ls | add <url> [name] | rm <name>\n");
			}
			config_free(&cfg);
			free(promptv);
			return 0;
#ifdef PC_WEIXIN
		} else if (!strcmp(argv[i], "gateway")) {
			config_t gcfg;
			char gerr[256];
			int gcreated;
			if (config_load(&gcfg, cfg_path, &gcreated, gerr, sizeof(gerr)) != 0) {
				fprintf(stderr, "error: %s\n", gerr);
				return 1;
			}
			if (auto_mode)
				gcfg.exec_confirm = 0;
			int grc = wx_gateway(&gcfg);
			config_free(&gcfg);
			free(promptv);
			return grc;
		} else if (!strcmp(argv[i], "auth") && i + 1 < argc &&
			   !strcmp(argv[i + 1], "weixin")) {
			config_t gcfg;
			char gerr[256];
			int gcreated;
			if (config_load(&gcfg, cfg_path, &gcreated, gerr, sizeof(gerr)) != 0) {
				fprintf(stderr, "error: %s\n", gerr);
				return 1;
			}
			int grc = wx_auth_login(&gcfg);
			config_free(&gcfg);
			free(promptv);
			return grc;
#endif
		} else if (!strcmp(argv[i], "ls") && i + 1 == argc) {
			/* subcommand: list sessions */
			config_t cfg;
			char err[256];
			int created;
			if (config_load(&cfg, cfg_path, &created, err, sizeof(err)) != 0) {
				fprintf(stderr, "error: %s\n", err);
				return 1;
			}
			print_sessions(&cfg);
			config_free(&cfg);
			free(promptv);
			return 0;
		} else if (!strcmp(argv[i], "rm") && i + 1 < argc) {
			config_t cfg;
			char err[256];
			int created;
			if (config_load(&cfg, cfg_path, &created, err, sizeof(err)) != 0) {
				fprintf(stderr, "error: %s\n", err);
				return 1;
			}
			int rc = session_delete(&cfg, argv[i + 1]);
			printf("%s: %s\n", argv[i + 1], rc == 0 ? "deleted" : "not found");
			config_free(&cfg);
			free(promptv);
			return rc == 0 ? 0 : 1;
		} else {
			promptv[nargs++] = argv[i];
		}
	}

	config_t cfg;
	char err[1024];
	int created = 0;
	if (config_load(&cfg, cfg_path, &created, err, sizeof(err)) != 0) {
		fprintf(stderr, "error: %s\n", err);
		return 1;
	}
	if (auto_mode)
		cfg.exec_confirm = 0;
	if (!cfg.api_key || !cfg.api_base || !cfg.model) {
		if (created)
			fprintf(stderr,
				"Created config template at %s\n"
				"Edit it (api_base/api_key/model) and run again.\n",
				cfg.config_path);
		else
			fprintf(stderr,
				"error: config needs api_base, api_key and model "
				"(see %s)\n", cfg.config_path);
		return 1;
	}

	pc_http_ca_info = cfg.ca_info;

	/* open session */
	char picked[256];
	if (pick_mode && !sess_key) {
		if (choose_session(&cfg, picked, sizeof(picked)) != 0)
			return 1;
		sess_key = picked;
	}
	session_t sess;
	if (force_new) {
		char *nk = session_new_key();
		if (session_open(&sess, &cfg, nk, NULL) != 0) {
			fprintf(stderr, "error: cannot open session\n");
			return 1;
		}
		free(nk);
	} else {
		int is_new = 0;
		if (session_open(&sess, &cfg, sess_key, &is_new) != 0) {
			fprintf(stderr, "error: cannot open session\n");
			return 1;
		}
		if (!is_new)
			print_recent(&sess, 3);
	}
	fprintf(stderr, "[session %s]\n", sess.key);

	if (nargs > 0) {
		/* one-shot: join argv words into one prompt */
		size_t total = 1;
		for (int i = 0; i < nargs; i++)
			total += strlen(promptv[i]) + 1;
		char *prompt = malloc(total);
		prompt[0] = 0;
		for (int i = 0; i < nargs; i++) {
			strcat(prompt, promptv[i]);
			if (i + 1 < nargs)
				strcat(prompt, " ");
		}
		run_prompt(&cfg, &sess, prompt);
		free(prompt);
		session_close(&sess);
		config_free(&cfg);
		free(promptv);
		return 0;
	}

	/* REPL */
	fprintf(stderr, "interactive mode; /help for commands\n");
	for (;;) {
		fputs("clawdget> ", stderr);
		fflush(stderr);
		char *line = util_read_line(stdin);
		if (!line) {
			fputc('\n', stderr);
			break;
		}
		if (!*line) {
			free(line);
			continue;
		}
		if (line[0] == '/' &&
		    (!strcmp(line, "/quit") || !strcmp(line, "/exit") ||
		     !strcmp(line, "/help") || !strcmp(line, "/new") ||
		     !strcmp(line, "/ls") || !strcmp(line, "/resume") ||
		     !strncmp(line, "/resume ", 8) || !strncmp(line, "/rm ", 4))) {
			if (!strcmp(line, "/quit") || !strcmp(line, "/exit")) {
				free(line);
				break;
			} else if (!strcmp(line, "/help")) {
				fprintf(stderr, "/new /ls /resume [key] /rm <key> /quit\n");
			} else if (!strcmp(line, "/new")) {
				session_close(&sess);
				char *nk = session_new_key();
				session_open(&sess, &cfg, nk, NULL);
				free(nk);
				fprintf(stderr, "[session %s]\n", sess.key);
			} else if (!strcmp(line, "/ls")) {
				print_sessions(&cfg);
			} else if (!strcmp(line, "/resume")) {
				char key[256];
				if (choose_session(&cfg, key, sizeof(key)) == 0) {
					session_t ns;
					if (session_open(&ns, &cfg, key, NULL) == 0) {
						session_close(&sess);
						sess = ns;
						fprintf(stderr, "[session %s]\n", sess.key);
						print_recent(&sess, 3);
					}
				}
			} else if (!strncmp(line, "/resume ", 8)) {
				const char *key = line + 8;
				session_t ns;
				if (session_open(&ns, &cfg, key, NULL) == 0) {
					session_close(&sess);
					sess = ns;
					fprintf(stderr, "[session %s]\n", sess.key);
					print_recent(&sess, 3);
				} else {
					fprintf(stderr, "cannot open session %s\n", key);
				}
			} else if (!strncmp(line, "/rm ", 4)) {
				session_delete(&cfg, line + 4);
			}
			free(line);
			continue;
		}
		/* anything else - including paths like /etc/config - is a prompt */
		run_prompt(&cfg, &sess, line);
		free(line);
	}

	session_close(&sess);
	config_free(&cfg);
	free(promptv);
	return 0;
}
