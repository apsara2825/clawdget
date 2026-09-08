/* clawdget: wx_api.h - Tencent iLink bot REST API (WeChat channel) */
#ifdef PC_WEIXIN
#ifndef PC_WX_API_H
#define PC_WX_API_H

#include <stddef.h>

#define WX_DEFAULT_BASE     "https://ilinkai.weixin.qq.com/"
#define WX_CHANNEL_VERSION  "2.1.1"
#define WX_APP_ID           "bot"
#define WX_CLIENT_VERSION   131329

/* message types (subset used by clawdget) */
#define WX_MSG_USER 1
#define WX_MSG_BOT  2
#define WX_ITEM_TEXT 1

typedef struct {
	char base_url[256];
	char token[512];
} wx_api_t;

void wx_api_init(wx_api_t *api, const char *base_url, const char *token);

/* POST endpoint with iLink headers; body is JSON; returns malloc'd response
 * body or NULL. err filled on failure. skip_auth: QR endpoints need no
 * Authorization headers. */
char *wx_post(const wx_api_t *api, const char *endpoint, const char *body,
	      char *err, size_t errsz, int skip_auth);

/* GET endpoint (query string appended by caller). Same conventions. */
char *wx_get(const wx_api_t *api, const char *endpoint, char *err, size_t errsz,
	     int skip_auth);

#endif /* PC_WX_API_H */
#endif /* PC_WEIXIN */
