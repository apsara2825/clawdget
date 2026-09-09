/* clawdget: util.c */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include "util.h"

char *util_read_line(FILE *fp)
{
	size_t cap = 256, len = 0;
	char *buf = malloc(cap);
	int c;
	while ((c = fgetc(fp)) != EOF) {
		if (c == '\n')
			break;
		if (len + 2 > cap) {
			cap *= 2;
			char *nb = realloc(buf, cap);
			if (!nb) { free(buf); return NULL; }
			buf = nb;
		}
		buf[len++] = (char)c;
	}
	if (len == 0 && c == EOF) {
		free(buf);
		return NULL;
	}
	buf[len] = 0;
	return buf;
}

char *util_read_file(const char *path, size_t max_bytes, size_t *out_len, int *truncated)
{
	if (truncated)
		*truncated = 0;
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	size_t cap = max_bytes < 8192 ? 8192 : max_bytes + 1;
	char *buf = malloc(cap);
	size_t len = 0;
	size_t n;
	while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
		len += n;
		if (len >= max_bytes) {
			if (truncated)
				*truncated = 1;
			break;
		}
		if (cap - len - 1 == 0) {
			if (len >= max_bytes) {
				if (truncated)
					*truncated = 1;
				break;
			}
			cap = cap * 2 > max_bytes + 1 ? max_bytes + 1 : cap * 2;
			char *nb = realloc(buf, cap);
			if (!nb) { break; }
			buf = nb;
		}
	}
	buf[len] = 0;
	fclose(fp);
	if (out_len)
		*out_len = len;
	return buf;
}

int util_write_line(const char *path, const char *content)
{
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *fp = fopen(tmp, "w");
	if (!fp)
		return -1;
	fputs(content ? content : "", fp);
	fputc('\n', fp);
	fclose(fp);
	return rename(tmp, path);
}

void util_mkdir_p(const char *path)
{
	char tmp[PATH_MAX];
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

/* resolve and ensure `path` stays under `base` */
int util_path_under(const char *path, const char *base, char *err, size_t errsz)
{
	char rp[PATH_MAX], rb[PATH_MAX];
	if (!realpath(path, rp)) {
		/* file may not exist yet (write_file): check parent dir */
		char tmp[PATH_MAX], tmp2[PATH_MAX];
		snprintf(tmp, sizeof(tmp), "%s", path);
		char *dir = dirname(tmp);
		if (!realpath(dir, rp)) {
			snprintf(err, errsz, "cannot resolve path: %s", path);
			return 0;
		}
		snprintf(tmp2, sizeof(tmp2), "%s", path);
		char *b = basename(tmp2);
		size_t dl = strlen(rp), bl = strlen(b);
		if (dl + bl + 2 >= sizeof(rp)) {
			snprintf(err, errsz, "path too long: %s", path);
			return 0;
		}
		rp[dl] = '/';
		memcpy(rp + dl + 1, b, bl + 1);
	}
	if (!realpath(base, rb)) {
		snprintf(err, errsz, "cannot resolve base: %s", base);
		return 0;
	}
	size_t bl = strlen(rb);
	if (strncmp(rp, rb, bl) != 0 || (rp[bl] != 0 && rp[bl] != '/')) {
		snprintf(err, errsz, "path outside workspace: %s", path);
		return 0;
	}
	return 1;
}

/* replace invalid UTF-8 bytes with '?' so output is safe to embed in JSON */
void util_utf8_sanitize(char *s)
{
	for (; *s; ) {
		unsigned char c = (unsigned char)*s;
		if (c < 0x80) {
			s++;
			continue;
		}
		int len = 0;
		if ((c & 0xE0) == 0xC0) len = 2;
		else if ((c & 0xF0) == 0xE0) len = 3;
		else if ((c & 0xF8) == 0xF0) len = 4;
		int ok = len > 0;
		for (int i = 1; ok && i < len; i++) {
			if (((unsigned char)s[i] & 0xC0) != 0x80)
				ok = 0;
		}
		if (ok && s[len - 1] != 0) {
			s += len;
		} else {
			*s++ = '?';
		}
	}
}

void util_trunc_note(char *buf, size_t bufsz, const char *what)
{
	char note[128];
	snprintf(note, sizeof(note), "\n...[%s truncated]\n", what);
	size_t l = strlen(buf);
	size_t n = strlen(note);
	if (l + n < bufsz)
		memcpy(buf + l, note, n + 1);
}
