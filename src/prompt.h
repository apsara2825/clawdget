/* clawdget: prompt.h */
#ifndef PC_PROMPT_H
#define PC_PROMPT_H

#include "config.h"

/* build system prompt: identity template + {workspace}/AGENTS.md (if present).
 * Returns malloc'd string. */
char *prompt_build_system(const config_t *cfg);

#endif
