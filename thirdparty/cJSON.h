/* picoclaw-c: thirdparty/cJSON.h - minimal cJSON-compatible API subset */
#ifndef CJSON_H
#define CJSON_H

#include <stddef.h>

typedef int cJSON_bool;

#define cJSON_False 0
#define cJSON_True  1
#define cJSON_NULL  2
#define cJSON_Number 3
#define cJSON_String 4
#define cJSON_Array  5
#define cJSON_Object 6

typedef struct cJSON {
	struct cJSON *next;
	struct cJSON *prev;   /* in arrays/objects: prev of first child = last child */
	struct cJSON *child;

	int    type;
	char  *valuestring;
	int    valueint;
	double valuedouble;

	char *string;         /* key */
} cJSON;

cJSON *cJSON_Parse(const char *text);
void   cJSON_Delete(cJSON *it);
char  *cJSON_PrintUnformatted(const cJSON *it);
void   cJSON_free(void *p);

cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *s);
cJSON *cJSON_CreateNumber(double n);
cJSON *cJSON_CreateBool(int b);
cJSON *cJSON_CreateTrue(void);
cJSON *cJSON_CreateFalse(void);
cJSON *cJSON_CreateNull(void);

cJSON *cJSON_AddItemToObject(cJSON *obj, const char *key, cJSON *item);
cJSON *cJSON_AddStringToObject(cJSON *obj, const char *key, const char *s);
cJSON *cJSON_AddNumberToObject(cJSON *obj, const char *key, double n);
cJSON *cJSON_AddBoolToObject(cJSON *obj, const char *key, int b);
cJSON *cJSON_AddItemToArray(cJSON *arr, cJSON *item);

cJSON *cJSON_GetObjectItem(const cJSON *obj, const char *key);
cJSON *cJSON_GetArrayItem(const cJSON *arr, int index);
int    cJSON_GetArraySize(const cJSON *arr);
cJSON *cJSON_ReplaceItemInArray(cJSON *arr, int index, cJSON *item);
const char *cJSON_GetStringValue(const cJSON *item);
cJSON *cJSON_Duplicate(const cJSON *it, int recurse);

#define cJSON_IsString(it)  ((it) && ((it)->type & 0xFF) == cJSON_String)
#define cJSON_IsNumber(it)  ((it) && ((it)->type & 0xFF) == cJSON_Number)
#define cJSON_IsBool(it)    ((it) && (((it)->type & 0xFF) == cJSON_True || \
                                      ((it)->type & 0xFF) == cJSON_False))
#define cJSON_IsTrue(it)    ((it) && ((it)->type & 0xFF) == cJSON_True)
#define cJSON_IsFalse(it)   ((it) && ((it)->type & 0xFF) == cJSON_False)
#define cJSON_IsNull(it)    ((it) && ((it)->type & 0xFF) == cJSON_NULL)
#define cJSON_IsArray(it)   ((it) && ((it)->type & 0xFF) == cJSON_Array)
#define cJSON_IsObject(it)  ((it) && ((it)->type & 0xFF) == cJSON_Object)

#endif
