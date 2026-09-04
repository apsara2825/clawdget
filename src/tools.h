/* clawdget: tools.h - tool registry + JSON schema (names/params mirror clawdget) */
#ifndef PC_TOOLS_H
#define PC_TOOLS_H

#include <stddef.h>
#include "cJSON.h"
#include "config.h"

typedef int (*pc_tool_fn)(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz);

typedef struct {
	const char *name;
	const char *description;
	const char *parameters_json; /* JSON schema text */
	pc_tool_fn   fn;
} pc_tool_t;

/* registry (NULL-terminated) */
extern const pc_tool_t pc_tools[];

/* build "tools" array for the API request; malloc'd JSON text */
cJSON *tools_build_defs(void);

/* find and execute; returns 0 and sets *out (malloc'd) on success */
int tools_execute(const config_t *cfg, const char *name, const char *arguments_json,
		  char **out, char *err, size_t errsz);

/* individual tools (used via registry; exposed for tests) */
int tool_exec_run(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz);
int tool_read_file(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz);
int tool_write_file(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz);
int tool_edit_file(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz);
int tool_list_dir(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz);
int tool_http_fetch(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz);
int tool_sysinfo(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz);

/* confirm prompt on /dev/tty; returns 1 if user said yes */
int tools_confirm(const char *cmd);

#endif
