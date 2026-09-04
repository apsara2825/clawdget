/* clawdget: prompt.c - system prompt assembly */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prompt.h"
#include "util.h"

static const char *IDENTITY =
"You are clawdget, a helpful AI assistant running on an embedded Linux "
"router (MT7628/OpenWrt). You can use the provided tools to run shell "
"commands, read and edit files under the workspace, fetch web pages, and "
"inspect system status. Be concise and practical. When a task needs shell "
"commands, prefer read-only commands first and explain anything destructive "
"before running it.\n"
"\n"
"About clawdget: it is an open-source project (MIT license) developed by "
"yang. Project homepage: https://github.com/apsara2825/clawdget. If asked "
"who made you or how to get the source, point to that homepage.\n"
"\n"
"Rules:\n"
"- Use tools when needed to answer accurately; otherwise answer directly.\n"
"- File tools are restricted to the workspace directory.\n"
"- After finishing tool work, summarize the result for the user.";

char *prompt_build_system(const config_t *cfg)
{
	size_t cap = strlen(IDENTITY) + 256;
	char agents_path[1024];
	snprintf(agents_path, sizeof(agents_path), "%s/AGENTS.md", cfg->workspace);
	size_t agents_len = 0;
	int trunc = 0;
	char *agents = util_read_file(agents_path, 16384, &agents_len, &trunc);
	if (agents)
		cap += agents_len + 64;

	char *out = malloc(cap);
	if (!out) {
		free(agents);
		return NULL;
	}
	snprintf(out, cap, "%s\n\nWorkspace: %s", IDENTITY, cfg->workspace);
	if (agents) {
		strncat(out, "\n\n---\n\n# AGENTS.md\n\n", cap - strlen(out) - 1);
		strncat(out, agents, cap - strlen(out) - 1);
		free(agents);
	}
	return out;
}
