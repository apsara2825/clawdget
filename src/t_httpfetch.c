/* clawdget: t_httpfetch.c - http_fetch tool + sysinfo tool */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include "cJSON.h"
#include "tools.h"
#include "http.h"
#include "util.h"

int tool_http_fetch(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz)
{
	(void)cfg;
	cJSON *u = cJSON_GetObjectItem(args, "url");
	if (!cJSON_IsString(u) || !u->valuestring[0]) {
		snprintf(err, errsz, "missing 'url'");
		return -1;
	}
	cJSON *mb = cJSON_GetObjectItem(args, "max_bytes");
	size_t max_bytes = 64 * 1024;
	if (cJSON_IsNumber(mb) && mb->valuedouble > 0)
		max_bytes = (size_t)mb->valuedouble;
	if (max_bytes > 256 * 1024)
		max_bytes = 256 * 1024;

	char *body = NULL;
	pc_http_result res;
	int rc = http_get(u->valuestring, NULL, max_bytes, &body, &res);
	if (rc == -1) {
		snprintf(err, errsz, "transport: %s", res.curl_err);
		return -1;
	}
	if (rc == -2) {
		snprintf(err, errsz, "HTTP %ld: %.400s", res.http_code, body ? body : "");
		free(body);
		return -1;
	}
	size_t bl = body ? strlen(body) : 0;
	char *resp = malloc(bl + 64);
	snprintf(resp, bl + 64, "[HTTP %ld, %zu bytes]\n%s", res.http_code, bl, body ? body : "");
	free(body);
	*out = resp;
	return 0;
}

int tool_sysinfo(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz)
{
	(void)args;
	char buf[8192];
	size_t len = 0;

	char hostname[128] = "?";
	gethostname(hostname, sizeof(hostname));
	len += snprintf(buf + len, sizeof(buf) - len, "host: %s\n", hostname);

	/* uptime + load */
	char line[256];
	FILE *fp = fopen("/proc/loadavg", "r");
	if (fp) {
		if (fgets(line, sizeof(line), fp))
			len += snprintf(buf + len, sizeof(buf) - len, "load: %s", line);
		fclose(fp);
	}
	fp = fopen("/proc/uptime", "r");
	if (fp) {
		if (fgets(line, sizeof(line), fp))
			len += snprintf(buf + len, sizeof(buf) - len, "uptime: %s", line);
		fclose(fp);
	}
	/* memory: MemTotal/MemFree/MemAvailable */
	fp = fopen("/proc/meminfo", "r");
	if (fp) {
		len += snprintf(buf + len, sizeof(buf) - len, "mem:\n");
		while (fgets(line, sizeof(line), fp)) {
			if (!strncmp(line, "MemTotal", 8) || !strncmp(line, "MemFree", 7) ||
			    !strncmp(line, "MemAvailable", 12))
				len += snprintf(buf + len, sizeof(buf) - len, "  %s", line);
		}
		fclose(fp);
	}
	/* filesystems */
	struct statvfs vfs;
	if (statvfs("/", &vfs) == 0) {
		unsigned long total = (unsigned long)vfs.f_blocks * vfs.f_frsize / 1024;
		unsigned long avail = (unsigned long)vfs.f_bavail * vfs.f_frsize / 1024;
		len += snprintf(buf + len, sizeof(buf) - len,
				"fs /: total %luKB, available %luKB\n", total, avail);
	}
	if (statvfs(cfg->workspace, &vfs) == 0) {
		unsigned long total = (unsigned long)vfs.f_blocks * vfs.f_frsize / 1024;
		unsigned long avail = (unsigned long)vfs.f_bavail * vfs.f_frsize / 1024;
		len += snprintf(buf + len, sizeof(buf) - len,
				"fs %s: total %luKB, available %luKB\n",
				cfg->workspace, total, avail);
	}
	/* network interfaces */
	fp = fopen("/proc/net/dev", "r");
	if (fp) {
		len += snprintf(buf + len, sizeof(buf) - len, "net:\n");
		int skip = 2;
		while (fgets(line, sizeof(line), fp)) {
			if (skip-- > 0)
				continue;
			char ifname[64];
			unsigned long rbytes, tbytes;
			if (sscanf(line, " %63[^:]: %lu %*s %*s %*s %*s %*s %*s %*s %lu",
				   ifname, &rbytes, &tbytes) >= 3)
				len += snprintf(buf + len, sizeof(buf) - len,
						"  %s rx=%luKB tx=%luKB\n",
						ifname, rbytes / 1024, tbytes / 1024);
		}
		fclose(fp);
	}
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;
	buf[len] = 0;
	*out = strdup(buf);
	return 0;
}
