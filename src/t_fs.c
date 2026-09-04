/* clawdget: t_fs.c - file tools: read_file / write_file / edit_file / list_dir
 * All paths restricted to cfg->workspace (plus cfg->allow_paths). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#include "cJSON.h"
#include "tools.h"
#include "util.h"

#define READ_MAX (64 * 1024)
#define FILE_EDIT_MAX (1024 * 1024)

/* resolve tool path: relative paths are interpreted against the workspace */
static const char *abs_path(const config_t *cfg, const char *path,
			    char *buf, size_t bufsz)
{
	if (path[0] == '/') {
		snprintf(buf, bufsz, "%s", path);
		return buf;
	}
	snprintf(buf, bufsz, "%s/%s", cfg->workspace, path);
	return buf;
}

static int path_allowed(const config_t *cfg, const char *path,
			char *err, size_t errsz)
{
	if (util_path_under(path, cfg->workspace, err, errsz))
		return 1;
	for (int i = 0; i < cfg->n_allow; i++) {
		char e2[256];
		if (util_path_under(path, cfg->allow_paths[i], e2, sizeof(e2)))
			return 1;
	}
	return 0;
}

int tool_read_file(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz)
{
	cJSON *p = cJSON_GetObjectItem(args, "path");
	if (!cJSON_IsString(p) || !p->valuestring[0]) {
		snprintf(err, errsz, "missing 'path'");
		return -1;
	}
	char apath[2048];
	const char *path = abs_path(cfg, p->valuestring, apath, sizeof(apath));
	if (!path_allowed(cfg, path, err, errsz))
		return -1;

	cJSON *off = cJSON_GetObjectItem(args, "offset");
	cJSON *len = cJSON_GetObjectItem(args, "length");
	size_t offset = cJSON_IsNumber(off) && off->valuedouble > 0 ? (size_t)off->valuedouble : 0;
	size_t length = cJSON_IsNumber(len) && len->valuedouble > 0 ? (size_t)len->valuedouble : 8192;
	if (length > READ_MAX)
		length = READ_MAX;

	FILE *fp = fopen(path, "rb");
	if (!fp) {
		snprintf(err, errsz, "open failed: %s", strerror(errno));
		return -1;
	}
	if (offset > 0 && fseek(fp, (long)offset, SEEK_SET) != 0) {
		snprintf(err, errsz, "seek failed: %s", strerror(errno));
		fclose(fp);
		return -1;
	}
	char *buf = malloc(length + 64);
	size_t n = fread(buf, 1, length, fp);
	int at_eof = fgetc(fp) == EOF;
	fclose(fp);
	buf[n] = 0;
	if (!at_eof)
		util_trunc_note(buf, length + 64, "file");
	*out = buf;
	return 0;
}

int tool_write_file(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz)
{
	cJSON *p = cJSON_GetObjectItem(args, "path");
	cJSON *c = cJSON_GetObjectItem(args, "content");
	if (!cJSON_IsString(p) || !p->valuestring[0]) {
		snprintf(err, errsz, "missing 'path'");
		return -1;
	}
	if (!cJSON_IsString(c)) {
		snprintf(err, errsz, "missing 'content'");
		return -1;
	}
	char apath[2048];
	const char *path = abs_path(cfg, p->valuestring, apath, sizeof(apath));
	if (!path_allowed(cfg, path, err, errsz))
		return -1;

	cJSON *ow = cJSON_GetObjectItem(args, "overwrite");
	int overwrite = cJSON_IsTrue(ow);
	struct stat st;
	if (!overwrite && stat(path, &st) == 0) {
		snprintf(err, errsz, "file exists (pass overwrite:true to replace)");
		return -1;
	}
	FILE *fp = fopen(path, "wb");
	if (!fp) {
		snprintf(err, errsz, "open for write failed: %s", strerror(errno));
		return -1;
	}
	size_t clen = strlen(c->valuestring);
	fwrite(c->valuestring, 1, clen, fp);
	fclose(fp);
	char res[128];
	snprintf(res, sizeof(res), "wrote %zu bytes to %s", clen, path);
	*out = strdup(res);
	return 0;
}

int tool_edit_file(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz)
{
	cJSON *p = cJSON_GetObjectItem(args, "path");
	cJSON *old_t = cJSON_GetObjectItem(args, "old_text");
	cJSON *new_t = cJSON_GetObjectItem(args, "new_text");
	if (!cJSON_IsString(p) || !cJSON_IsString(old_t) || !cJSON_IsString(new_t)) {
		snprintf(err, errsz, "need 'path', 'old_text', 'new_text'");
		return -1;
	}
	char apath[2048];
	const char *path = abs_path(cfg, p->valuestring, apath, sizeof(apath));
	if (!path_allowed(cfg, path, err, errsz))
		return -1;

	size_t flen;
	int trunc;
	char *text = util_read_file(path, FILE_EDIT_MAX, &flen, &trunc);
	if (!text) {
		snprintf(err, errsz, "read failed: %s", strerror(errno));
		return -1;
	}
	if (trunc) {
		snprintf(err, errsz, "file too large for edit (>%d bytes)", FILE_EDIT_MAX);
		free(text);
		return -1;
	}
	const char *found = strstr(text, old_t->valuestring);
	if (!found) {
		snprintf(err, errsz, "old_text not found");
		free(text);
		return -1;
	}
	size_t ol = strlen(old_t->valuestring);
	size_t nl = strlen(new_t->valuestring);
	size_t pre = (size_t)(found - text);
	size_t total = flen - ol + nl;
	char *nt = malloc(total + 1);
	memcpy(nt, text, pre);
	memcpy(nt + pre, new_t->valuestring, nl);
	memcpy(nt + pre + nl, text + pre + ol, flen - pre - ol);
	nt[total] = 0;
	free(text);

	FILE *fp = fopen(path, "wb");
	if (!fp) {
		snprintf(err, errsz, "write failed: %s", strerror(errno));
		free(nt);
		return -1;
	}
	fwrite(nt, 1, total, fp);
	fclose(fp);
	free(nt);
	char res[128];
	snprintf(res, sizeof(res), "replaced 1 occurrence in %s", path);
	*out = strdup(res);
	return 0;
}

int tool_list_dir(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz)
{
	const char *path = cfg->workspace;
	char apath[2048];
	cJSON *p = cJSON_GetObjectItem(args, "path");
	if (cJSON_IsString(p) && p->valuestring[0]) {
		path = abs_path(cfg, p->valuestring, apath, sizeof(apath));
	}
	if (!path_allowed(cfg, path, err, errsz))
		return -1;

	DIR *d = opendir(path);
	if (!d) {
		snprintf(err, errsz, "opendir failed: %s", strerror(errno));
		return -1;
	}
	size_t cap = 4096, len = 0;
	char *buf = malloc(cap);
	int count = 0;
	struct dirent *e;
	while ((e = readdir(d)) != NULL && count < 500) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		char full[2048];
		struct stat st;
		int is_dir = 0;
		long size = 0;
		snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
		if (stat(full, &st) == 0) {
			is_dir = S_ISDIR(st.st_mode);
			size = (long)st.st_size;
		}
		char line[512];
		snprintf(line, sizeof(line), "%s%s\t%ld\n", e->d_name,
			 is_dir ? "/" : "", size);
		size_t ll = strlen(line);
		if (len + ll + 1 > cap) {
			cap *= 2;
			char *nb = realloc(buf, cap);
			if (!nb)
				break;
			buf = nb;
		}
		memcpy(buf + len, line, ll);
		len += ll;
		count++;
	}
	closedir(d);
	buf[len] = 0;
	*out = buf;
	return 0;
}
