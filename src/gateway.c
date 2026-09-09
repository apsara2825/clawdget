/* clawdget: gateway.c - multi-channel gateway: one thread per enabled channel */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "gateway.h"
#include "spinner.h"

#ifdef PC_WEIXIN
int wx_channel_thread_start(const config_t *cfg, pthread_t *tid);
#endif
#ifdef PC_TELEGRAM
int tg_channel_thread_start(const config_t *cfg, pthread_t *tid);
#endif

volatile sig_atomic_t pc_gateway_stop = 0;

static void on_signal(int sig)
{
	(void)sig;
	pc_gateway_stop = 1;
}

void pc_gateway_install_signals(void)
{
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
}

int gateway_should_stop(void)
{
	return pc_gateway_stop;
}

typedef struct {
	const char *name;
	int (*start)(const config_t *, pthread_t *);
} channel_entry_t;

int gateway_run(const config_t *cfg, int auto_mode)
{
	(void)auto_mode;
	pc_gateway_install_signals();
	pc_thinking_set_enabled(0);

	channel_entry_t chans[4];
	int n = 0;
#ifdef PC_WEIXIN
	chans[n++] = (channel_entry_t){"weixin", wx_channel_thread_start};
#endif
#ifdef PC_TELEGRAM
	if (cfg->tg_token && cfg->tg_token[0])
		chans[n++] = (channel_entry_t){"telegram", tg_channel_thread_start};
#endif
	if (n == 0) {
		fprintf(stderr,
			"error: 没有启用的渠道。配置 channels.weixin/channels.telegram"
			"（含 token），或使用对应渠道的 auth 登录。\n");
		return 1;
	}

	pthread_t tids[4];
	int started = 0;
	for (int i = 0; i < n; i++) {
		if (chans[i].start(cfg, &tids[started]) == 0) {
			fprintf(stderr, "[gateway] %s 渠道已启动\n", chans[i].name);
			started++;
		} else {
			fprintf(stderr, "[gateway] %s 渠道启动失败\n", chans[i].name);
		}
	}
	if (started == 0)
		return 1;

	for (int i = 0; i < started; i++)
		pthread_join(tids[i], NULL);
	fprintf(stderr, "\n[gateway] 已退出\n");
	return 0;
}
