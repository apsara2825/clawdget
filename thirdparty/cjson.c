/* picoclaw-c: thirdparty/cjson.c - minimal cJSON-compatible JSON library
 * Implements the subset of the cJSON API used by picoclaw-c:
 * parse, print, object/array access, builders, duplicate.
 * (No number formatting corner cases beyond %.17g; no case-insensitive keys.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include "cJSON.h"

/* ---------------- item lifecycle ---------------- */

static cJSON *cjson_new_item(void)
{
	cJSON *it = calloc(1, sizeof(cJSON));
	return it;
}

cJSON *cJSON_CreateObject(void)
{
	cJSON *it = cjson_new_item();
	if (it)
		it->type = cJSON_Object;
	return it;
}

cJSON *cJSON_CreateArray(void)
{
	cJSON *it = cjson_new_item();
	if (it)
		it->type = cJSON_Array;
	return it;
}

cJSON *cJSON_CreateString(const char *s)
{
	cJSON *it = cjson_new_item();
	if (!it)
		return NULL;
	it->type = cJSON_String;
	it->valuestring = strdup(s ? s : "");
	return it;
}

cJSON *cJSON_CreateNumber(double n)
{
	cJSON *it = cjson_new_item();
	if (!it)
		return NULL;
	it->type = cJSON_Number;
	it->valuedouble = n;
	it->valueint = (int)n;
	return it;
}

cJSON *cJSON_CreateBool(int b)
{
	cJSON *it = cjson_new_item();
	if (!it)
		return NULL;
	it->type = b ? cJSON_True : cJSON_False;
	return it;
}

cJSON *cJSON_CreateTrue(void)  { return cJSON_CreateBool(1); }
cJSON *cJSON_CreateFalse(void) { return cJSON_CreateBool(0); }
cJSON *cJSON_CreateNull(void)
{
	cJSON *it = cjson_new_item();
	if (it)
		it->type = cJSON_NULL;
	return it;
}

static void delete_children(cJSON *it)
{
	cJSON *c = it->child;
	while (c) {
		cJSON *next = c->next;
		cJSON_Delete(c);
		c = next;
	}
	it->child = NULL;
}

void cJSON_Delete(cJSON *it)
{
	if (!it)
		return;
	delete_children(it);
	free(it->valuestring);
	free(it->string);
	free(it);
}

/* ---------------- attach / access ---------------- */

static void add_child(cJSON *parent, cJSON *item)
{
	item->prev = parent->child ? parent->child->prev : NULL;
	/* maintain prev as pointer to last child via first child's prev */
	if (!parent->child) {
		parent->child = item;
		item->prev = item; /* last = itself */
	} else {
		cJSON *last = parent->child->prev;
		last->next = item;
		item->prev = last;
		parent->child->prev = item;
	}
}

static cJSON *add_to(cJSON *obj, const char *key, cJSON *item)
{
	if (!item)
		return NULL;
	if (key) {
		item->string = strdup(key);
		if (!item->string) {
			cJSON_Delete(item);
			return NULL;
		}
	}
	if (obj)
		add_child(obj, item);
	return item;
}

cJSON *cJSON_AddItemToObject(cJSON *obj, const char *key, cJSON *item)
{
	return add_to(obj, key, item) ? obj : NULL;
}

cJSON *cJSON_AddStringToObject(cJSON *obj, const char *key, const char *s)
{
	return add_to(obj, key, cJSON_CreateString(s)) ? obj : NULL;
}

cJSON *cJSON_AddNumberToObject(cJSON *obj, const char *key, double n)
{
	return add_to(obj, key, cJSON_CreateNumber(n)) ? obj : NULL;
}

cJSON *cJSON_AddBoolToObject(cJSON *obj, const char *key, int b)
{
	return add_to(obj, key, cJSON_CreateBool(b)) ? obj : NULL;
}

cJSON *cJSON_AddItemToArray(cJSON *arr, cJSON *item)
{
	return add_to(arr, NULL, item) ? arr : NULL;
}

cJSON *cJSON_GetObjectItem(const cJSON *obj, const char *key)
{
	if (!obj)
		return NULL;
	for (cJSON *c = obj->child; c; c = c->next)
		if (c->string && strcmp(c->string, key) == 0)
			return c;
	return NULL;
}

cJSON *cJSON_GetArrayItem(const cJSON *arr, int index)
{
	if (!arr || index < 0)
		return NULL;
	int i = 0;
	for (cJSON *c = arr->child; c; c = c->next, i++)
		if (i == index)
			return c;
	return NULL;
}

int cJSON_GetArraySize(const cJSON *arr)
{
	if (!arr)
		return 0;
	int n = 0;
	for (cJSON *c = arr->child; c; c = c->next)
		n++;
	return n;
}

cJSON *cJSON_ReplaceItemInArray(cJSON *arr, int index, cJSON *item)
{
	if (!arr || !item || index < 0)
		return NULL;
	cJSON *old = cJSON_GetArrayItem(arr, index);
	if (!old)
		return NULL;
	item->string = old->string ? strdup(old->string) : NULL;
	/* splice */
	item->prev = old->prev;
	item->next = old->next;
	if (old->prev) {
		if (old->prev == old) {
			/* old was only child */
			arr->child = item;
			item->prev = item;
		} else {
			old->prev->next = item;
			/* fix last pointer if old was last */
			if (arr->child->prev == old)
				arr->child->prev = item;
		}
	}
	if (old->next)
		old->next->prev = item;
	old->string = NULL;
	cJSON_Delete(old);
	return arr;
}

const char *cJSON_GetStringValue(const cJSON *item)
{
	if (item && (item->type & 0xFF) == cJSON_String)
		return item->valuestring;
	return NULL;
}

cJSON *cJSON_Duplicate(const cJSON *it, int recurse)
{
	if (!it)
		return NULL;
	cJSON *n = cjson_new_item();
	if (!n)
		return NULL;
	n->type = it->type;
	if (it->string) {
		n->string = strdup(it->string);
		if (!n->string) { cJSON_Delete(n); return NULL; }
	}
	if (it->valuestring) {
		n->valuestring = strdup(it->valuestring);
		if (!n->valuestring) { cJSON_Delete(n); return NULL; }
	}
	n->valuedouble = it->valuedouble;
	n->valueint = it->valueint;
	if (recurse && it->child) {
		n->child = cJSON_Duplicate(it->child, 1);
		if (!n->child) { cJSON_Delete(n); return NULL; }
		/* rebuild sibling links */
		cJSON *prev = n->child;
		prev->prev = NULL;
		for (cJSON *c = it->child->next; c; c = c->next) {
			cJSON *nc = cJSON_Duplicate(c, 1);
			if (!nc) { cJSON_Delete(n); return NULL; }
			prev->next = nc;
			nc->prev = prev;
			prev = nc;
		}
		if (n->child)
			n->child->prev = prev; /* last */
	}
	return n;
}

/* ---------------- parse ---------------- */

typedef struct {
	const char *s;
	size_t pos;
	size_t len;
	int depth;
} pctx_t;

#define CJSON_MAX_DEPTH 256

static cJSON *parse_value(pctx_t *ctx);

static void skip_ws(pctx_t *ctx)
{
	while (ctx->pos < ctx->len) {
		char c = ctx->s[ctx->pos];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			ctx->pos++;
		else
			break;
	}
}

static int hex4(const char *s, unsigned *out)
{
	unsigned v = 0;
	for (int i = 0; i < 4; i++) {
		char c = s[i];
		v <<= 4;
		if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
		else return 0;
	}
	*out = v;
	return 1;
}

static int utf8_encode(char *out, unsigned cp)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	} else if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	} else if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

/* parse a quoted string starting at ctx->pos == '"' ; returns malloc'd text */
static char *parse_string_raw(pctx_t *ctx)
{
	if (ctx->pos >= ctx->len || ctx->s[ctx->pos] != '"')
		return NULL;
	ctx->pos++;
	size_t cap = 16, len = 0;
	char *buf = malloc(cap);
	if (!buf)
		return NULL;
	while (ctx->pos < ctx->len) {
		char c = ctx->s[ctx->pos++];
		if (c == '"') {
			buf[len] = 0;
			return buf;
		}
		if (c == '\\') {
			if (ctx->pos >= ctx->len)
				goto fail;
			char e = ctx->s[ctx->pos++];
			char tmp[8];
			int tl = 0;
			switch (e) {
			case '"': tmp[tl++] = '"'; break;
			case '\\': tmp[tl++] = '\\'; break;
			case '/': tmp[tl++] = '/'; break;
			case 'b': tmp[tl++] = '\b'; break;
			case 'f': tmp[tl++] = '\f'; break;
			case 'n': tmp[tl++] = '\n'; break;
			case 'r': tmp[tl++] = '\r'; break;
			case 't': tmp[tl++] = '\t'; break;
			case 'u': {
				unsigned cp;
				if (!hex4(ctx->s + ctx->pos, &cp))
					goto fail;
				ctx->pos += 4;
				if (cp >= 0xD800 && cp <= 0xDBFF &&
				    ctx->pos + 6 <= ctx->len &&
				    ctx->s[ctx->pos] == '\\' && ctx->s[ctx->pos + 1] == 'u') {
					unsigned lo;
					if (hex4(ctx->s + ctx->pos + 2, &lo) &&
					    lo >= 0xDC00 && lo <= 0xDFFF) {
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						ctx->pos += 6;
					}
				}
				tl = utf8_encode(tmp, cp);
				break;
			}
			default:
				goto fail;
			}
			for (int i = 0; i < tl; i++) {
				if (len + 2 > cap) {
					cap *= 2;
					char *nb = realloc(buf, cap);
					if (!nb)
						goto fail;
					buf = nb;
				}
				buf[len++] = tmp[i];
			}
		} else {
			if (len + 2 > cap) {
				cap *= 2;
				char *nb = realloc(buf, cap);
				if (!nb)
					goto fail;
				buf = nb;
			}
			buf[len++] = c;
		}
	}
fail:
	free(buf);
	return NULL;
}

static cJSON *parse_string(pctx_t *ctx)
{
	char *s = parse_string_raw(ctx);
	if (!s)
		return NULL;
	cJSON *it = cJSON_CreateString(s);
	free(s);
	return it;
}

static cJSON *parse_number(pctx_t *ctx)
{
	char tmp[64];
	size_t i = 0;
	size_t start = ctx->pos;
	while (ctx->pos < ctx->len && i < sizeof(tmp) - 1) {
		char c = ctx->s[ctx->pos];
		if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
		    c == 'e' || c == 'E') {
			tmp[i++] = c;
			ctx->pos++;
		} else {
			break;
		}
	}
	if (i == 0 || ctx->pos == start)
		return NULL;
	tmp[i] = 0;
	char *end;
	double v = strtod(tmp, &end);
	if (end == tmp || *end != 0)
		return NULL;
	return cJSON_CreateNumber(v);
}

static cJSON *parse_object(pctx_t *ctx)
{
	ctx->pos++; /* skip '{' */
	cJSON *obj = cJSON_CreateObject();
	if (!obj)
		return NULL;
	skip_ws(ctx);
	if (ctx->pos < ctx->len && ctx->s[ctx->pos] == '}') {
		ctx->pos++;
		return obj;
	}
	for (;;) {
		skip_ws(ctx);
		char *key = parse_string_raw(ctx);
		if (!key)
			goto fail;
		skip_ws(ctx);
		if (ctx->pos >= ctx->len || ctx->s[ctx->pos] != ':') {
			free(key);
			goto fail;
		}
		ctx->pos++;
		skip_ws(ctx);
		cJSON *val = parse_value(ctx);
		if (!val) {
			free(key);
			goto fail;
		}
		val->string = key;
		add_child(obj, val);
		skip_ws(ctx);
		if (ctx->pos < ctx->len && ctx->s[ctx->pos] == ',') {
			ctx->pos++;
			continue;
		}
		if (ctx->pos < ctx->len && ctx->s[ctx->pos] == '}') {
			ctx->pos++;
			return obj;
		}
		goto fail;
	}
fail:
	cJSON_Delete(obj);
	return NULL;
}

static cJSON *parse_array(pctx_t *ctx)
{
	ctx->pos++; /* skip '[' */
	cJSON *arr = cJSON_CreateArray();
	if (!arr)
		return NULL;
	skip_ws(ctx);
	if (ctx->pos < ctx->len && ctx->s[ctx->pos] == ']') {
		ctx->pos++;
		return arr;
	}
	for (;;) {
		skip_ws(ctx);
		cJSON *val = parse_value(ctx);
		if (!val)
			goto fail;
		add_child(arr, val);
		skip_ws(ctx);
		if (ctx->pos < ctx->len && ctx->s[ctx->pos] == ',') {
			ctx->pos++;
			continue;
		}
		if (ctx->pos < ctx->len && ctx->s[ctx->pos] == ']') {
			ctx->pos++;
			return arr;
		}
		goto fail;
	}
fail:
	cJSON_Delete(arr);
	return NULL;
}

static cJSON *parse_value(pctx_t *ctx)
{
	if (ctx->depth++ > CJSON_MAX_DEPTH)
		return NULL;
	cJSON *result = NULL;
	skip_ws(ctx);
	if (ctx->pos >= ctx->len)
		goto done;
	char c = ctx->s[ctx->pos];
	if (c == '{') {
		result = parse_object(ctx);
	} else if (c == '[') {
		result = parse_array(ctx);
	} else if (c == '"') {
		result = parse_string(ctx);
	} else if (c == 't' && ctx->pos + 4 <= ctx->len &&
		   !strncmp(ctx->s + ctx->pos, "true", 4)) {
		ctx->pos += 4;
		result = cJSON_CreateTrue();
	} else if (c == 'f' && ctx->pos + 5 <= ctx->len &&
		   !strncmp(ctx->s + ctx->pos, "false", 5)) {
		ctx->pos += 5;
		result = cJSON_CreateFalse();
	} else if (c == 'n' && ctx->pos + 4 <= ctx->len &&
		   !strncmp(ctx->s + ctx->pos, "null", 4)) {
		ctx->pos += 4;
		result = cJSON_CreateNull();
	} else {
		result = parse_number(ctx);
	}
done:
	ctx->depth--;
	return result;
}

cJSON *cJSON_Parse(const char *text)
{
	if (!text)
		return NULL;
	pctx_t ctx = { text, 0, strlen(text), 0 };
	cJSON *it = parse_value(&ctx);
	if (!it)
		return NULL;
	skip_ws(&ctx);
	if (ctx.pos != ctx.len) {
		cJSON_Delete(it);
		return NULL; /* trailing garbage */
	}
	return it;
}

/* ---------------- print ---------------- */

/* growable string buffer (uClibc 0.9.33 has no open_memstream) */
typedef struct { char *buf; size_t len, cap; } sbuf_t;

static int sb_grow(sbuf_t *sb, size_t need)
{
	if (sb->len + need + 1 > sb->cap) {
		size_t nc = sb->cap ? sb->cap : 256;
		while (nc < sb->len + need + 1)
			nc *= 2;
		char *nb = realloc(sb->buf, nc);
		if (!nb)
			return 0;
		sb->buf = nb;
		sb->cap = nc;
	}
	return 1;
}

static void sb_putc(sbuf_t *sb, char c)
{
	if (!sb_grow(sb, 1))
		return;
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = 0;
}

static void sb_puts(sbuf_t *sb, const char *s)
{
	size_t l = strlen(s);
	if (!sb_grow(sb, l))
		return;
	memcpy(sb->buf + sb->len, s, l);
	sb->len += l;
	sb->buf[sb->len] = 0;
}

static void sb_printf(sbuf_t *sb, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int need = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (need < 0 || !sb_grow(sb, (size_t)need))
		return;
	va_start(ap, fmt);
	vsnprintf(sb->buf + sb->len, (size_t)need + 1, fmt, ap);
	va_end(ap);
	sb->len += (size_t)need;
}

static void print_escaped(const char *s, sbuf_t *sb)
{
	sb_putc(sb, '"');
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		switch (c) {
		case '"': sb_puts(sb, "\\\""); break;
		case '\\': sb_puts(sb, "\\\\"); break;
		case '\b': sb_puts(sb, "\\b"); break;
		case '\f': sb_puts(sb, "\\f"); break;
		case '\n': sb_puts(sb, "\\n"); break;
		case '\r': sb_puts(sb, "\\r"); break;
		case '\t': sb_puts(sb, "\\t"); break;
		default:
			if (c < 0x20)
				sb_printf(sb, "\\u%04x", c);
			else
				sb_putc(sb, (char)c);
		}
	}
	sb_putc(sb, '"');
}

static void print_value(const cJSON *it, sbuf_t *sb)
{
	if (!it)
		return;
	switch (it->type & 0xFF) {
	case cJSON_Object:
		sb_putc(sb, '{');
		for (cJSON *c = it->child; c; c = c->next) {
			if (c != it->child)
				sb_putc(sb, ',');
			if (c->string) {
				print_escaped(c->string, sb);
				sb_putc(sb, ':');
			}
			print_value(c, sb);
		}
		sb_putc(sb, '}');
		break;
	case cJSON_Array:
		sb_putc(sb, '[');
		for (cJSON *c = it->child; c; c = c->next) {
			if (c != it->child)
				sb_putc(sb, ',');
			print_value(c, sb);
		}
		sb_putc(sb, ']');
		break;
	case cJSON_String:
		print_escaped(it->valuestring ? it->valuestring : "", sb);
		break;
	case cJSON_Number: {
		double v = it->valuedouble;
		if (v == (double)(long long)v && v < 1e15 && v > -1e15)
			sb_printf(sb, "%lld", (long long)v);
		else
			sb_printf(sb, "%.17g", v);
		break;
	}
	case cJSON_True:
		sb_puts(sb, "true");
		break;
	case cJSON_False:
		sb_puts(sb, "false");
		break;
	default:
		sb_puts(sb, "null");
	}
}

char *cJSON_PrintUnformatted(const cJSON *it)
{
	if (!it)
		return NULL;
	sbuf_t sb = { NULL, 0, 0 };
	sb_grow(&sb, 256);
	sb.buf[0] = 0;
	print_value(it, &sb);
	if (!sb.buf)
		return NULL;
	return sb.buf;
}

void cJSON_free(void *p)
{
	free(p);
}
