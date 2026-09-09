/* clawdget: tg_api.h - Telegram Bot API client (with proxy support) */
#ifdef PC_TELEGRAM
#ifndef PC_TG_API_H
#define PC_TG_API_H

#include <stddef.h>

typedef struct {
	char base_url[256];   /* default https://api.telegram.org */
	char token[128];      /* bot token from BotFather */
	char proxy[256];      /* "" = direct; e.g. socks5://127.0.0.1:1080 */
} tg_api_t;

void tg_api_init(tg_api_t *api, const char *base_url, const char *token,
		 const char *proxy);

/* abort support: point at a stop flag (Ctrl-C interruptible long polling) */
void tg_api_set_abort(volatile int *flag);

/* POST <base>/bot<token>/<method> with JSON body.
 * Returns malloc'd response body or NULL (err filled). ret code -2 = aborted. */
char *tg_post(const tg_api_t *api, const char *method, const char *json_body,
	      char *err, size_t errsz);

#endif /* PC_TG_API_H */
#endif /* PC_TELEGRAM */
