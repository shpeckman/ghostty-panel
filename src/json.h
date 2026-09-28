// src/json.h
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "util.h"

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

typedef struct Json {
    JsonType type;
    bool boolean;
    double number;
    char *string;
    char *key;
    struct Json *items;
    size_t count;
} Json;

Json *json_parse(const char *text, size_t len, char *err, size_t errlen);
void json_free(Json *j);

const Json *json_get(const Json *obj, const char *key);
const char *json_get_string(const Json *obj, const char *key, const char *def);
bool json_get_bool(const Json *obj, const char *key, bool def);
double json_get_number(const Json *obj, const char *key, double def);
void json_get_string_list(const Json *obj, const char *key, StrList *out);

void json_write_string(Buf *b, const char *s);
void json_write_string_list(Buf *b, const StrList *l);
