/* clawdget: prompt.h */
#ifndef PC_PROMPT_H
#define PC_PROMPT_H

#include "config.h"

/* build system prompt: identity template + {workspace}/AGENTS.md (if present)
 * + skills catalog from {workspace}/skills/<name>/SKILL.md. malloc'd string. */
char *prompt_build_system(const config_t *cfg);

/* extract one-line description from {dir}/{name}/SKILL.md */
void prompt_skill_desc(const char *dir, const char *name, char *out, size_t sz);

#endif
