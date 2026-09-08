/* clawdget: wx.h - WeChat (iLink) channel: QR login + gateway daemon */
#ifdef PC_WEIXIN
#ifndef PC_WX_H
#define PC_WX_H

#include "config.h"

/* interactive QR login; saves token to {home}/weixin/token.txt.
 * returns 0 on success. */
int wx_auth_login(const config_t *cfg);

/* long-running gateway: poll messages, dispatch to agent per user, reply.
 * blocks until SIGINT/SIGTERM. returns 0. */
int wx_gateway(const config_t *cfg);

#endif /* PC_WX_H */
#endif /* PC_WEIXIN */
