/* clawdget: config.c - JSON config + env overrides */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cJSON.h"
#include "config.h"
#include "util.h"

static char *xstrdup(const char *s)
{
	return s ? strdup(s) : NULL;
}

static void pick_str(char **dst, const char *env)
{
	if (*dst)
		return;
	const char *e = getenv(env);
	if (e && *e)
		*dst = strdup(e);
}

char *config_default_home(void)
{
	const char *e = getenv("CLAWDGET_HOME");
	if (e && *e)
		return strdup(e);
	/* embedded: prefer persistent /data partition if present */
	if (access("/data", W_OK) == 0) {
		char *p = malloc(32);
		strcpy(p, "/data/.clawdget");
		return p;
	}
	const char *h = getenv("HOME");
	if (!h || !*h)
		h = "/tmp";
	char *p = malloc(strlen(h) + 16);
	sprintf(p, "%s/.clawdget", h);
	return p;
}

static void mkdir_p(const char *path)
{
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

static const char *CONFIG_TEMPLATE =
"{\n"
"  \"model_list\": [\n"
"    {\n"
"      \"api_base\": \"https://api.deepseek.com/v1\",\n"
"      \"api_key\": \"sk-xxx\",\n"
"      \"model\": \"deepseek-chat\",\n"
"      \"temperature\": 0.7,\n"
"      \"max_tokens\": 4096\n"
"    }\n"
"  ],\n"
"  \"agents\": {\n"
"    \"defaults\": {\n"
"      \"max_tool_iterations\": 20\n"
"    }\n"
"  },\n"
"  \"tools\": {\n"
"    \"exec_confirm\": true,\n"
"    \"allow_paths\": []\n"
"  }\n"
"}\n";

int config_load(config_t *cfg, const char *path, int *created_template,
		char *err, size_t errsz)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->temperature = -1;
	cfg->exec_confirm = 1;
	cfg->show_tool_calls = 0;
	cfg->stream = 1;
	cfg->max_tool_iters = 20;

	char *home = config_default_home();
	cfg->home = home;

	if (!path)
		path = getenv("CLAWDGET_CONFIG");
	if (!path) {
		static char buf[1024];
		snprintf(buf, sizeof(buf), "%s/config.json", home);
		path = buf;
	}
	cfg->config_path = strdup(path);

	FILE *fp = fopen(path, "r");
	cJSON *root = NULL;
	if (!fp) {
		/* no config: write template next to it and tell caller */
		FILE *tf = fopen(path, "w");
		if (tf) {
			fputs(CONFIG_TEMPLATE, tf);
			fclose(tf);
		}
		if (created_template)
			*created_template = 1;
	} else {
		if (created_template)
			*created_template = 0;
		fseek(fp, 0, SEEK_END);
		long sz = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		char *text = malloc((size_t)sz + 1);
		size_t rd = fread(text, 1, (size_t)sz, fp);
		text[rd] = 0;
		fclose(fp);

		root = cJSON_Parse(text);
		if (!root) {
			snprintf(err, errsz, "config parse error: %s", path);
			free(text);
			return -1;
		}
		free(text);
	}

	if (root) {
		cJSON *ml = cJSON_GetObjectItem(root, "model_list");
		if (cJSON_IsArray(ml) && cJSON_GetArraySize(ml) > 0) {
			cJSON *m = cJSON_GetArrayItem(ml, 0);
			cJSON *v;
			if ((v = cJSON_GetObjectItem(m, "api_base")) && cJSON_IsString(v))
				cfg->api_base = xstrdup(v->valuestring);
			if ((v = cJSON_GetObjectItem(m, "api_key")) && cJSON_IsString(v))
				cfg->api_key = xstrdup(v->valuestring);
			if ((v = cJSON_GetObjectItem(m, "model")) && cJSON_IsString(v))
				cfg->model = xstrdup(v->valuestring);
			if ((v = cJSON_GetObjectItem(m, "temperature")) && cJSON_IsNumber(v))
				cfg->temperature = v->valuedouble;
			if ((v = cJSON_GetObjectItem(m, "max_tokens")) && cJSON_IsNumber(v))
				cfg->max_tokens = (int)v->valuedouble;
			if ((v = cJSON_GetObjectItem(m, "stream")) && cJSON_IsBool(v))
				cfg->stream = cJSON_IsTrue(v) ? 1 : 0;
		}
		cJSON *ag = cJSON_GetObjectItem(root, "agents");
		cJSON *def = ag ? cJSON_GetObjectItem(ag, "defaults") : NULL;
		if (def) {
			cJSON *v;
			if ((v = cJSON_GetObjectItem(def, "max_tool_iterations")) && cJSON_IsNumber(v))
				cfg->max_tool_iters = (int)v->valuedouble;
			if ((v = cJSON_GetObjectItem(def, "max_history")) && cJSON_IsNumber(v))
				cfg->max_history = (int)v->valuedouble;
		}
			{
			cJSON *v = cJSON_GetObjectItem(root, "ca_info");
			if (cJSON_IsString(v) && v->valuestring[0])
				cfg->ca_info = xstrdup(v->valuestring);
		}
		cJSON *tools = cJSON_GetObjectItem(root, "tools");
		if (tools) {
			cJSON *v;
			if ((v = cJSON_GetObjectItem(tools, "exec_confirm")) && cJSON_IsBool(v))
				cfg->exec_confirm = cJSON_IsTrue(v) ? 1 : 0;
			if ((v = cJSON_GetObjectItem(tools, "auto")) && cJSON_IsTrue(v))
				cfg->exec_confirm = 0; /* auto mode: no confirmations */
			if ((v = cJSON_GetObjectItem(tools, "show_tool_calls")) && cJSON_IsBool(v))
				cfg->show_tool_calls = cJSON_IsTrue(v) ? 1 : 0;
			if ((v = cJSON_GetObjectItem(tools, "allow_paths")) && cJSON_IsArray(v)) {
				int n = cJSON_GetArraySize(v);
				if (n > 0) {
					cfg->allow_paths = calloc((size_t)n, sizeof(char *));
					for (int i = 0; i < n; i++) {
						cJSON *e = cJSON_GetArrayItem(v, i);
						if (cJSON_IsString(e))
							cfg->allow_paths[cfg->n_allow++] = xstrdup(e->valuestring);
					}
				}
			}
		}
			cJSON *ch = cJSON_GetObjectItem(root, "channels");
		cJSON *wx = ch ? cJSON_GetObjectItem(ch, "weixin") : NULL;
		if (cJSON_IsObject(wx)) {
			cJSON *v;
			if ((v = cJSON_GetObjectItem(wx, "token")) && cJSON_IsString(v))
				cfg->wx_token = xstrdup(v->valuestring);
			if ((v = cJSON_GetObjectItem(wx, "allow_from")) && cJSON_IsArray(v)) {
				int n = cJSON_GetArraySize(v);
				if (n > 0) {
					cfg->wx_allow = calloc((size_t)n, sizeof(char *));
					for (int i = 0; i < n; i++) {
						cJSON *e = cJSON_GetArrayItem(v, i);
						if (cJSON_IsString(e))
							cfg->wx_allow[cfg->wx_n_allow++] = xstrdup(e->valuestring);
					}
				}
			}
		}
		cJSON_Delete(root);
	}

	/* env overrides */
	pick_str(&cfg->api_base, "CLAWDGET_API_BASE");
	pick_str(&cfg->api_key, "CLAWDGET_API_KEY");
	pick_str(&cfg->model, "CLAWDGET_MODEL");
	pick_str(&cfg->ca_info, "CLAWDGET_CAINFO");
	pick_str(&cfg->wx_token, "CLAWDGET_WX_TOKEN");

	if (!cfg->workspace) {
		char ws[1024];
		snprintf(ws, sizeof(ws), "%s/workspace", home);
		cfg->workspace = strdup(ws);
	}

	mkdir_p(cfg->home);
	mkdir_p(cfg->workspace);
	return 0;
}

void config_free(config_t *cfg)
{
	free(cfg->api_base);
	free(cfg->api_key);
	free(cfg->model);
	free(cfg->ca_info);
	free(cfg->workspace);
	free(cfg->home);
	free(cfg->config_path);
	for (int i = 0; i < cfg->n_allow; i++)
		free(cfg->allow_paths[i]);
	free(cfg->allow_paths);
	free(cfg->wx_token);
	for (int i = 0; i < cfg->wx_n_allow; i++)
		free(cfg->wx_allow[i]);
	free(cfg->wx_allow);
	memset(cfg, 0, sizeof(*cfg));
}
