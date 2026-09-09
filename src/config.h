/* clawdget: ultra-light AI agent for MT7628/OpenWrt
 * config.h - JSON config + env overrides */
#ifndef PC_CONFIG_H
#define PC_CONFIG_H

#include <stddef.h>

typedef struct {
	char       *api_base;   /* e.g. https://api.deepseek.com/v1 */
	char       *api_key;
	char       *model;
	double      temperature;      /* -1 = omit */
	int         max_tokens;       /* 0 = omit */
	int         max_tool_iters;   /* default 20 */
	int         exec_confirm;     /* default 1: y/n before each exec */
	int         show_tool_calls;  /* default 1: print [tool] lines */
	int         max_history;      /* sliding window, 0 = unlimited */
	int         stream;           /* default 1 */
	char       *ca_info;          /* path to CA bundle PEM (optional) */
	char       *workspace;        /* default {home}/workspace */
	char       *home;             /* default ~/.clawdget */
	char      **allow_paths;      /* extra paths fs tools may touch */
	int         n_allow;
	char       *wx_token;         /* weixin channel: bot token */
	char      **wx_allow;         /* weixin allowlist (user ids) */
	int         wx_n_allow;
	char       *wx_proxy;         /* weixin: http/socks5 proxy URL */
	char       *tg_token;         /* telegram: bot token */
	char       *tg_proxy;         /* telegram: http/socks5 proxy URL */
	char       *tg_base;          /* telegram: api base override */
	char      **tg_allow;         /* telegram allowlist (chat id or @username) */
	int         tg_n_allow;
	char       *config_path;      /* where it was loaded from */
} config_t;

/* Load config: explicit path, else $CLAWDGET_CONFIG, else {home}/config.json.
 * Returns 0 on success (file found or defaults), -1 on parse error (msg in err).
 * If the file does not exist, *created_template is set to 1 and a template is
 * written; caller should tell the user to edit it and exit. */
int  config_load(config_t *cfg, const char *path, int *created_template, char *err, size_t errsz);
void config_free(config_t *cfg);
/* default home dir (~/.clawdget), malloc'd */
char *config_default_home(void);

#endif
