/* clawdget: util.h - small shared helpers */
#ifndef PC_UTIL_H
#define PC_UTIL_H

#include <stddef.h>
#include <stdio.h>

/* read one line of arbitrary length from fp (without '\n').
 * Returns malloc'd string, NULL on EOF with nothing read. */
char *util_read_line(FILE *fp);

/* read entire file (up to max_bytes); returns malloc'd buffer (NUL
 * terminated) and sets *out_len. NULL if unreadable. *truncated set if hit cap. */
char *util_read_file(const char *path, size_t max_bytes, size_t *out_len, int *truncated);

/* mkdir -p */
void util_mkdir_p(const char *path);

/* check path is under base (path traversal guard). both must be absolute-ish;
 * returns 1 if ok */
int util_path_under(const char *path, const char *base, char *err, size_t errsz);

/* replace invalid UTF-8 bytes in-place with '?' (for JSON safety) */
void util_utf8_sanitize(char *s);

/* truncation notice suffix appended to tool output */
void util_trunc_note(char *buf, size_t bufsz, const char *what);

#endif
