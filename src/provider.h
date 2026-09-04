/* clawdget: provider.h - OpenAI-compatible chat completions client */
#ifndef PC_PROVIDER_H
#define PC_PROVIDER_H

#include <stddef.h>
#include <stdio.h>
#include "cJSON.h"
#include "config.h"

/* a single tool call assembled from stream deltas */
typedef struct {
	char *id;
	char *name;
	char *arguments; /* JSON text, accumulated */
} pc_toolcall_t;

/* result of one chat() round */
typedef struct {
	char *content;        /* assistant text (NULL if none) */
	pc_toolcall_t *tcs;
	int n_tcs;
	char *finish_reason;  /* "stop", "tool_calls", ... */
	int  prompt_tokens, completion_tokens;
} pc_resp_t;

/* messages is a cJSON array of {"role","content","tool_calls",...} objects.
 * tools_defs is cJSON array of {"type":"function","function":{...}} or NULL.
 * Streams content deltas to stdout as they arrive (unless out is NULL).
 * Returns 0 on success; on error returns -1 and fills err (size errsz). */
int provider_chat(const config_t *cfg, cJSON *messages, cJSON *tools_defs,
		  pc_resp_t *out, char *err, size_t errsz, FILE *out_stream);

void pc_resp_free(pc_resp_t *r);

#endif
