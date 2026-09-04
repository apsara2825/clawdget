/* clawdget: session.h - JSONL multi-session persistence
 * Format mirrors clawdget pkg/memory/jsonl.go:
 *   {home}/sessions/{key}.jsonl      - one message JSON per line, append-only
 *   {home}/sessions/{key}.meta.json  - {key,count,created_at,updated_at} */
#ifndef PC_SESSION_H
#define PC_SESSION_H

#include <stdio.h>
#include "cJSON.h"
#include "config.h"

typedef struct {
	char  *key;
	char   path[1024];  /* .jsonl path */
	char   metapath[1024];
	FILE  *fp;
	int    count;
	char   created_at[32];
} session_t;

/* Open (or create) session `key`. If key is NULL, open the most recently
 * updated session, or create a new one (timestamp key) if none exists.
 * Sets *is_new. Returns 0 on success. */
int session_open(session_t *s, const config_t *cfg, const char *key, int *is_new);

/* append one message (role/content/tool_calls/tool_call_id cJSON object) */
int session_append(session_t *s, const cJSON *msg);

/* load all messages of the session into a cJSON array (malloc'd, caller frees) */
cJSON *session_load(session_t *s);

/* create a fresh session with a timestamp key; returns malloc'd key */
char *session_new_key(void);

/* list sessions into an array of {key,count,updated_at}, newest first.
 * Caller frees. n_out set to count. */
cJSON *session_list(const config_t *cfg);

/* delete session files for key; returns 0 if something was removed */
int session_delete(const config_t *cfg, const char *key);

void session_close(session_t *s);

#endif
