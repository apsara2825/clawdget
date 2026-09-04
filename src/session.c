/* clawdget: session.c - JSONL multi-session persistence */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "cJSON.h"
#include "session.h"
#include "util.h"

static void sessions_dir(const config_t *cfg, char *buf, size_t sz)
{
	snprintf(buf, sz, "%s/sessions", cfg->home);
}

static void session_paths(session_t *s, const char *dir, const char *key)
{
	snprintf(s->path, sizeof(s->path), "%s/%s.jsonl", dir, key);
	snprintf(s->metapath, sizeof(s->metapath), "%s/%s.meta.json", dir, key);
}

static void now_str(char *buf, size_t sz)
{
	time_t t = time(NULL);
	struct tm tm;
	gmtime_r(&t, &tm);
	strftime(buf, sz, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

char *session_new_key(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	char buf[64];
	strftime(buf, sizeof(buf), "s-%Y%m%d-%H%M%S", &tm);
	return strdup(buf);
}

static int write_meta(session_t *s)
{
	cJSON *m = cJSON_CreateObject();
	cJSON_AddStringToObject(m, "key", s->key);
	cJSON_AddNumberToObject(m, "count", s->count);
	cJSON_AddStringToObject(m, "created_at", s->created_at);
	char upd[32];
	now_str(upd, sizeof(upd));
	cJSON_AddStringToObject(m, "updated_at", upd);
	char *txt = cJSON_PrintUnformatted(m);
	cJSON_Delete(m);
	if (!txt)
		return -1;
	char tmp[1100];
	snprintf(tmp, sizeof(tmp), "%s.tmp", s->metapath);
	FILE *fp = fopen(tmp, "w");
	if (!fp) {
		free(txt);
		return -1;
	}
	fputs(txt, fp);
	fclose(fp);
	free(txt);
	rename(tmp, s->metapath);
	return 0;
}

static void load_meta(session_t *s)
{
	size_t len;
	int trunc;
	char *txt = util_read_file(s->metapath, 16384, &len, &trunc);
	if (!txt)
		return;
	cJSON *m = cJSON_Parse(txt);
	free(txt);
	if (!m)
		return;
	cJSON *v;
	if ((v = cJSON_GetObjectItem(m, "count")) && cJSON_IsNumber(v))
		s->count = (int)v->valuedouble;
	if ((v = cJSON_GetObjectItem(m, "created_at")) && cJSON_IsString(v))
		snprintf(s->created_at, sizeof(s->created_at), "%s", v->valuestring);
	cJSON_Delete(m);
}

int session_open(session_t *s, const config_t *cfg, const char *key, int *is_new)
{
	memset(s, 0, sizeof(*s));
	char dir[900];
	sessions_dir(cfg, dir, sizeof(dir));
	util_mkdir_p(dir);

	char chosen[256];
	int fresh = 0;
	if (key && *key) {
		snprintf(chosen, sizeof(chosen), "%s", key);
	} else {
		/* pick newest by mtime of *.meta.json */
		DIR *d = opendir(dir);
		char best[256] = "";
		long best_mtime = -1;
		if (d) {
			struct dirent *e;
			while ((e = readdir(d))) {
				size_t l = strlen(e->d_name);
				if (l < 10 || strcmp(e->d_name + l - 10, ".meta.json") != 0)
					continue;
				char p[1100];
				snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
				struct stat st;
				if (stat(p, &st) == 0 && st.st_mtime > best_mtime) {
					best_mtime = st.st_mtime;
					snprintf(best, sizeof(best), "%.*s",
						 (int)(l - 10), e->d_name);
				}
			}
			closedir(d);
		}
		if (best[0]) {
			snprintf(chosen, sizeof(chosen), "%s", best);
		} else {
			char *nk = session_new_key();
			snprintf(chosen, sizeof(chosen), "%s", nk);
			free(nk);
			fresh = 1;
		}
	}

	s->key = strdup(chosen);
	session_paths(s, dir, chosen);

	struct stat st;
	int exists = stat(s->path, &st) == 0;
	if (is_new)
		*is_new = !exists;

	s->fp = fopen(s->path, "a");
	if (!s->fp)
		return -1;
	now_str(s->created_at, sizeof(s->created_at));
	load_meta(s);
	if (fresh || !exists)
		write_meta(s);
	return 0;
}

int session_append(session_t *s, const cJSON *msg)
{
	if (!s->fp)
		return -1;
	char *line = cJSON_PrintUnformatted(msg);
	if (!line)
		return -1;
	fputs(line, s->fp);
	fputc('\n', s->fp);
	free(line);
	fflush(s->fp);
	fsync(fileno(s->fp));
	s->count++;
	write_meta(s);
	return 0;
}

cJSON *session_load(session_t *s)
{
	cJSON *arr = cJSON_CreateArray();
	FILE *fp = fopen(s->path, "r");
	if (!fp)
		return arr;
	char *line;
	while ((line = util_read_line(fp)) != NULL) {
		if (*line) {
			cJSON *m = cJSON_Parse(line);
			if (m)
				cJSON_AddItemToArray(arr, m);
		}
		free(line);
	}
	fclose(fp);
	return arr;
}

cJSON *session_list(const config_t *cfg)
{
	cJSON *out = cJSON_CreateArray();
	char dir[900];
	sessions_dir(cfg, dir, sizeof(dir));
	DIR *d = opendir(dir);
	if (!d)
		return out;
	struct dirent *e;
	while ((e = readdir(d))) {
		size_t l = strlen(e->d_name);
		if (l < 10 || strcmp(e->d_name + l - 10, ".meta.json") != 0)
			continue;
		char p[1100];
		snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
		size_t len;
		int trunc;
		char *txt = util_read_file(p, 16384, &len, &trunc);
		if (!txt)
			continue;
		cJSON *m = cJSON_Parse(txt);
		free(txt);
		if (!m)
			continue;
		cJSON_AddItemToArray(out, m);
	}
	closedir(d);
	/* sort newest first by updated_at */
	int n = cJSON_GetArraySize(out);
	for (int i = 0; i < n - 1; i++) {
		for (int j = 0; j < n - 1 - i; j++) {
			cJSON *a = cJSON_GetArrayItem(out, j);
			cJSON *b = cJSON_GetArrayItem(out, j + 1);
			const char *ua = cJSON_GetStringValue(cJSON_GetObjectItem(a, "updated_at"));
			const char *ub = cJSON_GetStringValue(cJSON_GetObjectItem(b, "updated_at"));
			if (ua && ub && strcmp(ua, ub) < 0)
				cJSON_ReplaceItemInArray(out, j, cJSON_Duplicate(b, 1)),
				cJSON_ReplaceItemInArray(out, j + 1, cJSON_Duplicate(a, 1));
		}
	}
	return out;
}

int session_delete(const config_t *cfg, const char *key)
{
	char dir[900];
	sessions_dir(cfg, dir, sizeof(dir));
	char p1[1100], p2[1100];
	snprintf(p1, sizeof(p1), "%s/%s.jsonl", dir, key);
	snprintf(p2, sizeof(p2), "%s/%s.meta.json", dir, key);
	int removed = 0;
	if (unlink(p1) == 0)
		removed = 1;
	if (unlink(p2) == 0)
		removed = 1;
	return removed ? 0 : -1;
}

void session_close(session_t *s)
{
	if (s->fp)
		fclose(s->fp);
	free(s->key);
	memset(s, 0, sizeof(*s));
}
