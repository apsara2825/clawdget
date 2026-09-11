/* clawdget: doctor.c - step-by-step API connectivity diagnostics.
 * DNS -> TCP -> TLS/HTTP, then CA store status and suggestions. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
#include "doctor.h"
#include "http.h"
#include "util.h"

static size_t sink_cb(char *p, size_t sz, size_t n, void *ud)
{
	(void)p; (void)ud;
	return sz * n;
}

static void extract_host(const char *api_base, char *host, size_t sz,
			 char *scheme, size_t ssz, char *port, size_t psz)
{
	host[0] = 0;
	scheme[0] = 0;
	port[0] = 0;
	const char *p = strstr(api_base, "://");
	if (p) {
		size_t sl = (size_t)(p - api_base);
		if (sl >= ssz)
			sl = ssz - 1;
		memcpy(scheme, api_base, sl);
		scheme[sl] = 0;
		p += 3;
	} else {
		p = api_base;
		snprintf(scheme, ssz, "https");
	}
	const char *slash = strchr(p, '/');
	size_t hl = slash ? (size_t)(slash - p) : strlen(p);
	if (hl >= sz)
		hl = sz - 1;
	memcpy(host, p, hl);
	host[hl] = 0;
	/* strip :port */
	char *colon = strrchr(host, ':');
	if (colon && strspn(colon + 1, "0123456789") == strlen(colon + 1) &&
	    strlen(colon + 1) > 0) {
		snprintf(port, psz, "%s", colon + 1);
		*colon = 0;
	}
	if (!port[0])
		snprintf(port, psz, !strcmp(scheme, "https") ? "443" : "80");
}

int doctor_run(const config_t *cfg)
{
	char host[256], scheme[32], port[16];

	fprintf(stderr, "=== clawdget 连接诊断 ===\n");
	fprintf(stderr, "API 地址 : %s\n", cfg->api_base ? cfg->api_base : "(未配置)");
	fprintf(stderr, "模型     : %s\n", cfg->model ? cfg->model : "(未配置)");
	if (!cfg->api_base || !cfg->api_key || !cfg->model) {
		fprintf(stderr, "!! 配置不完整：需要 api_base / api_key / model\n");
		return 1;
	}

	extract_host(cfg->api_base, host, sizeof(host), scheme, sizeof(scheme),
		     port, sizeof(port));
	fprintf(stderr, "主机     : %s (端口 %s)\n", host, port);
	{
		const char *hp = getenv("http_proxy");
		const char *hps = getenv("https_proxy");
		if ((hp && *hp) || (hps && *hps))
			fprintf(stderr, "⚠ 环境代理  : 已设置 http_proxy=%s https_proxy=%s"
					"（代理故障会表现为连接错误/超时）\n",
				hp ? hp : "", hps ? hps : "");
	}

	/* 1. DNS */
	fprintf(stderr, "\n[1/4] DNS 解析... ");
	fflush(stderr);
	struct addrinfo hints, *res = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int gr = getaddrinfo(host, port, &hints, &res);
	if (gr != 0 || !res) {
		fprintf(stderr, "失败 (%s)\n", gai_strerror(gr));
		fprintf(stderr, "   → DNS 无法解析该域名。检查路由器 DNS 设置，"
				"或该域名在你的网络不可用。\n");
		return 1;
	}
	char ip[64] = "?";
	if (res->ai_family == AF_INET)
		inet_ntop(AF_INET,
			  &((struct sockaddr_in *)res->ai_addr)->sin_addr,
			  ip, sizeof(ip));
	else if (res->ai_family == AF_INET6)
		inet_ntop(AF_INET6,
			  &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr,
			  ip, sizeof(ip));
	fprintf(stderr, "成功 → %s\n", ip);
	freeaddrinfo(res);

	/* 2. TCP connect */
	fprintf(stderr, "[2/4] TCP 连接 %s:%s... ", ip, port);
	fflush(stderr);
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int ok = 0;
	if (fd >= 0) {
		/* re-resolve for the connect (res freed below) */
		struct addrinfo *r2 = NULL;
		if (getaddrinfo(host, port, &hints, &r2) == 0 && r2) {
			ok = connect(fd, r2->ai_addr, r2->ai_addrlen) == 0;
			freeaddrinfo(r2);
		}
		close(fd);
	}
	if (!ok) {
		fprintf(stderr, "失败\n");
		fprintf(stderr, "   → TCP 都连不上：设备到该主机的网络不通"
				"（被墙/IP 封禁/需要代理）。\n"
				"   → 若是国内不可达的接口，尝试配置代理或换国内"
				"中转地址。\n");
		return 1;
	}
	fprintf(stderr, "成功\n");

	/* 3. HTTPS round trip via curl (TLS handshake included) */
	fprintf(stderr, "[3/4] HTTPS 请求（含 TLS 握手）... ");
	fflush(stderr);
	CURL *h = curl_easy_init();
	if (!h) {
		fprintf(stderr, "curl 初始化失败\n");
		return 1;
	}
	char probe[1024];
	snprintf(probe, sizeof(probe), "%s://%s:%s%s", scheme, host, port,
		 strcmp(scheme, "https") ? "/" : "/");
	char cerr[CURL_ERROR_SIZE] = "";
	long http_code = 0;
	curl_easy_setopt(h, CURLOPT_URL, probe);
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, sink_cb);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(h, CURLOPT_TIMEOUT, 25L);
	curl_easy_setopt(h, CURLOPT_ERRORBUFFER, cerr);
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	/* same CA logic as the real request path */
	if (cfg->ca_info && *cfg->ca_info) {
		struct stat st;
		if (stat(cfg->ca_info, &st) == 0 && S_ISDIR(st.st_mode))
			curl_easy_setopt(h, CURLOPT_CAPATH, cfg->ca_info);
		else
			curl_easy_setopt(h, CURLOPT_CAINFO, cfg->ca_info);
	}
	CURLcode cr = curl_easy_perform(h);
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(h);

	if (cr != CURLE_OK) {
		fprintf(stderr, "失败\n");
		fprintf(stderr, "   → curl 错误: %s (code %d)\n",
			curl_easy_strerror(cr), cr);
		if (cr == CURLE_SSL_CONNECT_ERROR ||
		    cr == CURLE_SSL_CACERT || cr == 60 || cr == 51 || cr == 35 ||
	    cr == 58 || cr == 59 || cr == 77 || cr == 80 || cr == 90) {
			fprintf(stderr, "   → TLS 层问题，两种常见原因：\n");
			fprintf(stderr, "     a) 设备 TLS 库太老，无法与该服务器握手"
					"（老固件常见，需更换工具链重新编译或换接口）\n");
			fprintf(stderr, "     b) 缺少 CA 证书：当前 %s\n",
				cfg->ca_info ? cfg->ca_info :
				(getenv("CLAWDGET_CAINFO") ? "环境变量指定" : "自动探测（可能为空）"));
			fprintf(stderr, "   → 可尝试：把一个较新的 ca-certificates.crt 放到设备上，"
					"配置 \"ca_info\" 指向它\n");
		} else if (cr == CURLE_OPERATION_TIMEDOUT) {
			fprintf(stderr, "   → 超时：该主机可能被墙或网络被限制。"
					"换国内可达的接口/中转，或配置代理。\n");
		}
		return 1;
	}
	fprintf(stderr, "成功 (HTTP %ld)\n", http_code);

	/* 4. CA store status */
	fprintf(stderr, "[4/4] CA 证书状态... ");
	{
		struct stat st;
		int n = 0;
		if (stat("/etc/ssl/certs", &st) == 0) {
			DIR *d = opendir("/etc/ssl/certs");
			if (d) {
				struct dirent *e;
				while ((e = readdir(d)) != NULL)
					if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
						n++;
				closedir(d);
			}
		}
		if (cfg->ca_info && *cfg->ca_info)
			fprintf(stderr, "使用配置指定的 %s\n", cfg->ca_info);
		else if (n > 0)
			fprintf(stderr, "系统证书目录 /etc/ssl/certs (%d 项)\n", n);
		else
			fprintf(stderr, "未找到可用证书（无 /etc/ssl/certs）——"
					"HTTPS 将失败，请放置证书并用 ca_info 指定\n");
	}

	/* 5. real API endpoint test (with auth header) */
	fprintf(stderr, "\n[最终] 完整 API 请求测试（带鉴权）... ");
	fflush(stderr);
	CURL *h2 = curl_easy_init();
	if (!h2) {
		fprintf(stderr, "curl 初始化失败\n");
		return 1;
	}
	char aerr[CURL_ERROR_SIZE] = "";
	long code2 = 0;
	char auth[1100];
	snprintf(auth, sizeof(auth), "Authorization: Bearer %s",
		 cfg->api_key ? cfg->api_key : "");
	struct curl_slist *hd = NULL;
	hd = curl_slist_append(hd, auth);
	curl_easy_setopt(h2, CURLOPT_URL, cfg->api_base);
	curl_easy_setopt(h2, CURLOPT_HTTPHEADER, hd);
	curl_easy_setopt(h2, CURLOPT_WRITEFUNCTION, sink_cb);
	curl_easy_setopt(h2, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h2, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(h2, CURLOPT_TIMEOUT, 25L);
	curl_easy_setopt(h2, CURLOPT_ERRORBUFFER, aerr);
	curl_easy_setopt(h2, CURLOPT_FOLLOWLOCATION, 1L);
	CURLcode r2 = curl_easy_perform(h2);
	curl_easy_getinfo(h2, CURLINFO_RESPONSE_CODE, &code2);
	curl_slist_free_all(hd);
	curl_easy_cleanup(h2);
	if (r2 != CURLE_OK) {
		fprintf(stderr, "失败 (%s)\n", aerr[0] ? aerr : curl_easy_strerror(r2));
		fprintf(stderr, "\n把以上全部输出发给项目维护者有助于定位问题。\n");
		return 1;
	}
	if (code2 == 401 || code2 == 403) {
		fprintf(stderr, "可达，但 HTTP %ld —— api_key 无效或无权限！检查配置\n",
			code2);
		return 1;
	}
	fprintf(stderr, "成功 (HTTP %ld) —— API 可用 ✓\n", code2);
	fprintf(stderr, "\n诊断结论：网络与证书正常。若对话仍失败，"
			"请把本页输出发给项目维护者。\n");
	return 0;
}
