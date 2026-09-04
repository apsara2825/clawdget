/* clawdget: tools.c - registry + schema definitions (names mirror clawdget) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "tools.h"

const pc_tool_t pc_tools[] = {
	{
		"exec",
		"Run a shell command on this device (/bin/sh -c) and return stdout+stderr. "
		"Ask before destructive operations. Prefer read-only commands for inspection.",
		"{\"type\":\"object\",\"properties\":{"
		"\"command\":{\"type\":\"string\",\"description\":\"shell command to run\"},"
		"\"timeout\":{\"type\":\"integer\",\"description\":\"timeout seconds (default 60, max 300)\"}"
		"},\"required\":[\"command\"]}",
		tool_exec_run,
	},
	{
		"read_file",
		"Read a file inside the workspace, optionally from a byte offset.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\"},"
		"\"offset\":{\"type\":\"integer\",\"description\":\"byte offset\"},"
		"\"length\":{\"type\":\"integer\",\"description\":\"max bytes to read (default 8192, max 65536)\"}"
		"},\"required\":[\"path\"]}",
		tool_read_file,
	},
	{
		"write_file",
		"Write text content to a file in the workspace. Refuses to overwrite unless overwrite:true.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\"},"
		"\"content\":{\"type\":\"string\"},"
		"\"overwrite\":{\"type\":\"boolean\",\"default\":false}"
		"},\"required\":[\"path\",\"content\"]}",
		tool_write_file,
	},
	{
		"edit_file",
		"Replace the first exact occurrence of old_text with new_text in a workspace file.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\"},"
		"\"old_text\":{\"type\":\"string\"},"
		"\"new_text\":{\"type\":\"string\"}"
		"},\"required\":[\"path\",\"old_text\",\"new_text\"]}",
		tool_edit_file,
	},
	{
		"list_dir",
		"List a directory in the workspace: name, '/' suffix for dirs, size in bytes.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\",\"description\":\"defaults to workspace root\"}"
		"}}",
		tool_list_dir,
	},
	{
		"http_fetch",
		"Fetch a URL with HTTP GET and return the response body (truncated).",
		"{\"type\":\"object\",\"properties\":{"
		"\"url\":{\"type\":\"string\"},"
		"\"max_bytes\":{\"type\":\"integer\",\"description\":\"max body bytes (default 65536, max 262144)\"}"
		"},\"required\":[\"url\"]}",
		tool_http_fetch,
	},
	{
		"sysinfo",
		"Get device status: hostname, load, uptime, memory, filesystems, network interfaces.",
		"{\"type\":\"object\",\"properties\":{}}",
		tool_sysinfo,
	},
	{ NULL, NULL, NULL, NULL }
};

cJSON *tools_build_defs(void)
{
	cJSON *arr = cJSON_CreateArray();
	for (int i = 0; pc_tools[i].name; i++) {
		cJSON *t = cJSON_CreateObject();
		cJSON_AddStringToObject(t, "type", "function");
		cJSON *fn = cJSON_CreateObject();
		cJSON_AddStringToObject(fn, "name", pc_tools[i].name);
		cJSON_AddStringToObject(fn, "description", pc_tools[i].description);
		cJSON *params = cJSON_Parse(pc_tools[i].parameters_json);
		if (params)
			cJSON_AddItemToObject(fn, "parameters", params);
		cJSON_AddItemToObject(t, "function", fn);
		cJSON_AddItemToArray(arr, t);
	}
	return arr;
}

int tools_execute(const config_t *cfg, const char *name, const char *arguments_json,
		  char **out, char *err, size_t errsz)
{
	*out = NULL;
	for (int i = 0; pc_tools[i].name; i++) {
		if (strcmp(pc_tools[i].name, name) != 0)
			continue;
		cJSON *args = cJSON_Parse(arguments_json ? arguments_json : "{}");
		if (!args) {
			snprintf(err, errsz, "invalid tool arguments JSON");
			return -1;
		}
		int rc = pc_tools[i].fn(cfg, args, out, err, errsz);
		cJSON_Delete(args);
		return rc;
	}
	snprintf(err, errsz, "unknown tool: %s", name);
	return -1;
}
