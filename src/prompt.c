/* clawdget: prompt.c - system prompt assembly */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
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

/* extract a one-line description from a SKILL.md:
 * frontmatter "description:" line, else first "# " heading, else first line */
void prompt_skill_desc(const char *dir, const char *name, char *out, size_t sz)
{
	char path[2048];
	snprintf(path, sizeof(path), "%s/%s/SKILL.md", dir, name);
	out[0] = 0;
	size_t len;
	int trunc;
	char *text = util_read_file(path, 4096, &len, &trunc);
	if (!text)
		return;
	const char *p = text;
	if (strncmp(p, "---", 3) == 0) {
		const char *fm_end = strstr(p + 3, "\n---");
		const char *scan = p + 3;
		while (scan && (!fm_end || scan < fm_end)) {
			const char *le = strchr(scan, '\n');
			if (!le || (fm_end && le > fm_end))
				le = fm_end;
			if (!le)
				break;
			if ((size_t)(le - scan) > 12 &&
			    strncmp(scan, "description:", 12) == 0) {
				const char *v = scan + 12;
				while (*v == ' ')
					v++;
				size_t l = (size_t)(le - v);
				if (l >= sz)
					l = sz - 1;
				memcpy(out, v, l);
				out[l] = 0;
				free(text);
				return;
			}
			scan = le + 1;
		}
	}
	while (*p) {
		const char *le = strchr(p, '\n');
		size_t ll = le ? (size_t)(le - p) : strlen(p);
		const char *src = p;
		size_t sl = ll;
		if (ll >= 2 && p[0] == '#' && p[1] == ' ') {
			src = p + 2;
			sl = ll - 2;
		}
		if (sl > 0 && strncmp(src, "---", 3) != 0) {
			if (sl >= sz)
				sl = sz - 1;
			memcpy(out, src, sl);
			out[sl] = 0;
			free(text);
			return;
		}
		if (!le)
			break;
		p = le + 1;
	}
	free(text);
}

#define MAX_SKILLS 32

char *prompt_build_system(const config_t *cfg)
{
	size_t cap = strlen(IDENTITY) + 256;

	/* collect skills: {workspace}/skills/<name>/SKILL.md */
	char names[MAX_SKILLS][128];
	char descs[MAX_SKILLS][256];
	int n_sk = 0;
	char skills_dir[1024];
	snprintf(skills_dir, sizeof(skills_dir), "%s/skills", cfg->workspace);
	DIR *sd = opendir(skills_dir);
	if (sd) {
		struct dirent *e;
		while ((e = readdir(sd)) != NULL && n_sk < MAX_SKILLS) {
			if (e->d_name[0] == '.')
				continue;
			char p[2048];
			struct stat st;
			snprintf(p, sizeof(p), "%s/%s/SKILL.md",
				 skills_dir, e->d_name);
			if (stat(p, &st) != 0 || !S_ISREG(st.st_mode))
				continue;
			snprintf(names[n_sk], sizeof(names[n_sk]), "%s", e->d_name);
			prompt_skill_desc(skills_dir, e->d_name,
					  descs[n_sk], sizeof(descs[n_sk]));
			n_sk++;
		}
		closedir(sd);
	}
	cap += (size_t)n_sk * 400 + 512;
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
	if (n_sk > 0) {
		strncat(out,
			"\n\n---\n\n# Skills\n\n"
			"When a task matches one of these skills, first use "
			"read_file to load workspace/skills/<name>/SKILL.md "
			"and follow its instructions:\n",
			cap - strlen(out) - 1);
		for (int i = 0; i < n_sk; i++) {
			char line[420];
			snprintf(line, sizeof(line), "- %s: %s\n",
				 names[i],
				 descs[i][0] ? descs[i] : "(no description)");
			strncat(out, line, cap - strlen(out) - 1);
		}
	}
	return out;
}
