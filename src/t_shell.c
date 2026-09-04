/* clawdget: t_shell.c - exec tool: one-shot fork+exec with timeout */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>
#include "cJSON.h"
#include "tools.h"
#include "util.h"

#define EXEC_OUT_CAP    (32 * 1024)
#define EXEC_TIMEOUT_DFL 60    /* seconds */
#define EXEC_TIMEOUT_MAX 300

int tools_confirm(const char *cmd)
{
	fprintf(stderr, "\n[exec confirm] %s\nProceed? (y/N) ", cmd);
	fflush(stderr);
	FILE *tty = fopen("/dev/tty", "r+");
	if (!tty)
		return 0;
	int c = fgetc(tty);
	fgetc(tty); /* consume newline if present */
	fclose(tty);
	return c == 'y' || c == 'Y';
}

int tool_exec_run(const config_t *cfg, cJSON *args, char **out, char *err, size_t errsz)
{
	cJSON *cmd_j = cJSON_GetObjectItem(args, "command");
	if (!cJSON_IsString(cmd_j) || !cmd_j->valuestring[0]) {
		snprintf(err, errsz, "missing 'command'");
		return -1;
	}
	const char *cmd = cmd_j->valuestring;

	cJSON *to = cJSON_GetObjectItem(args, "timeout");
	int timeout = cJSON_IsNumber(to) ? (int)to->valuedouble : EXEC_TIMEOUT_DFL;
	if (timeout <= 0)
		timeout = EXEC_TIMEOUT_DFL;
	if (timeout > EXEC_TIMEOUT_MAX)
		timeout = EXEC_TIMEOUT_MAX;

	if (cfg->exec_confirm && !tools_confirm(cmd)) {
		snprintf(err, errsz, "execution denied by user");
		return -1;
	}

	int pipefd[2];
	if (pipe(pipefd) != 0) {
		snprintf(err, errsz, "pipe: %s", strerror(errno));
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		snprintf(err, errsz, "fork: %s", strerror(errno));
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		/* detach from terminal signals while running */
		setpgid(0, 0);
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
	close(pipefd[1]);

	/* read output with timeout */
	char *buf = malloc(EXEC_OUT_CAP + 128);
	size_t len = 0, truncated = 0;
	struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
	time_t deadline = time(NULL) + timeout;
	int timed_out = 0;

	for (;;) {
		int remaining = (int)(deadline - time(NULL));
		if (remaining <= 0) {
			timed_out = 1;
			break;
		}
		int pr = poll(&pfd, 1, remaining * 1000);
		if (pr < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (pr == 0) {
			timed_out = 1;
			break;
		}
		char tmp[4096];
		ssize_t n = read(pipefd[0], tmp, sizeof(tmp));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;
		size_t room = len < EXEC_OUT_CAP ? EXEC_OUT_CAP - len : 0;
		size_t take = (size_t)n < room ? (size_t)n : room;
		memcpy(buf + len, tmp, take);
		len += take;
		if ((size_t)n > room)
			truncated = 1;
	}

	int status = 0;
	if (timed_out) {
		kill(-pid, SIGKILL);
		kill(pid, SIGKILL);
	}
	waitpid(pid, &status, 0);
	close(pipefd[0]);

	buf[len] = 0;
	if (truncated)
		util_trunc_note(buf, EXEC_OUT_CAP + 128, "output");

	char head[256];
	if (timed_out)
		snprintf(head, sizeof(head), "[timed out after %ds]\n", timeout);
	else if (WIFEXITED(status))
		snprintf(head, sizeof(head), "[exit %d]\n", WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		snprintf(head, sizeof(head), "[killed by signal %d]\n", WTERMSIG(status));
	else
		head[0] = 0;

	size_t total = strlen(head) + len + 1;
	char *res = malloc(total + 1);
	snprintf(res, total + 1, "%s%s", head, buf);
	free(buf);
	*out = res;
	return 0;
}
