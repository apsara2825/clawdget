/* clawdget: gateway.h - multi-channel gateway daemon */
#ifndef PC_GATEWAY_H
#define PC_GATEWAY_H

#include <signal.h>
#include "config.h"

/* shared stop flag: set by SIGINT/SIGTERM handler, read by channel loops */
extern volatile sig_atomic_t pc_gateway_stop;

void pc_gateway_install_signals(void);
int gateway_should_stop(void);

/* run all enabled (compiled-in) channels in threads; blocks until stop. */
int gateway_run(const config_t *cfg, int auto_mode);

#endif
