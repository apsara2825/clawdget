/* clawdget: agent.h - tool-calling loop (mirrors pkg/tools/toolloop.go) */
#ifndef PC_AGENT_H
#define PC_AGENT_H

#include <stdio.h>
#include "cJSON.h"
#include "config.h"
#include "session.h"

/* Run one user turn: append user message, loop LLM<->tools (up to
 * cfg->max_tool_iters), persisting every message to the session.
 * Prints assistant content to `out` (via provider streaming).
 * Returns 0 on success, -1 on error (err filled). */
int agent_turn(const config_t *cfg, session_t *sess, const char *user_prompt,
	       char *err, size_t errsz, char **reply_out);

#endif
