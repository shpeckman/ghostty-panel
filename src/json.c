// src/json.c
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    const char *end;
    const char *error;
    int depth;
} Parser;

static void skip_ws(Parser *ps)
{
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static bool literal(Parser *ps, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n) != 0) return false;
    ps->p += n;
    return true;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_hex4(Parser *ps, uint32_t *out)
{
    if (ps->end - ps->p < 4) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        int h = hexval(ps->p[i]);
        if (h < 0) return false;
        v = v * 16 + (uint32_t)h;
    }
    ps->p += 4;
    *out = v;
    return true;
}

static char *parse_string(Parser *ps)
{
    if (ps->p >= ps->end || *ps->p != '"') {
        ps->error = "expected string";
        return NULL;
    }
    ps->p++;
    Buf b = {0};
    while (ps->p < ps->end && *ps->p != '"') {
        char c = *ps->p++;
        if ((unsigned char)c < 0x20) {
            ps->error = "control character in string";
            buf_free(&b);
            return NULL;
        }
        if (c != '\\') {
            buf_append(&b, &c, 1);
            continue;
        }
        if (ps->p >= ps->end) break;
        char e = *ps->p++;
        char out = 0;
        switch (e) {
        case '"': out = '"'; break;
        case '\\': out = '\\'; break;
        case '/': out = '/'; break;
        case 'b': out = '\b'; break;
        case 'f': out = '\f'; break;
        case 'n': out = '\n'; break;
        case 'r': out = '\r'; break;
        case 't': out = '\t'; break;
        case 'u': {
            uint32_t cp;
            if (!read_hex4(ps, &cp)) {
                ps->error = "invalid unicode escape";
                buf_free(&b);
                return NULL;
            }
            if (cp >= 0xD800 && cp <= 0xDBFF && ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
                ps->p += 2;
                uint32_t lo;
                if (read_hex4(ps, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                else cp = 0xFFFD;
            }
            char u8[4];
            buf_append(&b, u8, utf8_encode(cp, u8));
            continue;
        }
        default:
            ps->error = "invalid escape";
            buf_free(&b);
            return NULL;
        }
        buf_append(&b, &out, 1);
    }
    if (ps->p >= ps->end) {
        ps->error = "unterminated string";
        buf_free(&b);
        return NULL;
    }
    ps->p++;
    return buf_cstr(&b);
}

static bool parse_value(Parser *ps, Json *out);
static void json_free_contents(Json *j);

static bool push_item(Json *container, size_t *cap, Json *item)
{
    if (container->count == *cap) {
        *cap = *cap ? *cap * 2 : 4;
        container->items = xrealloc(container->items, *cap * sizeof(Json));
    }
    container->items[container->count++] = *item;
    return true;
}

static bool parse_value(Parser *ps, Json *out)
{
    *out = (Json){0};
    if (++ps->depth > 64) {
        ps->error = "nesting too deep";
        return false;
    }
    skip_ws(ps);
    if (ps->p >= ps->end) {
        ps->error = "unexpected end of input";
        return false;
    }
    bool ok = true;
    char c = *ps->p;
    if (c == '{') {
        out->type = JSON_OBJECT;
        ps->p++;
        size_t cap = 0;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == '}') {
            ps->p++;
        } else {
            for (;;) {
                skip_ws(ps);
                char *key = parse_string(ps);
                if (!key) {
                    ok = false;
                    break;
                }
                skip_ws(ps);
                if (ps->p >= ps->end || *ps->p != ':') {
                    free(key);
                    ps->error = "expected ':'";
                    ok = false;
                    break;
                }
                ps->p++;
                Json item;
                if (!parse_value(ps, &item)) {
                    free(key);
                    json_free_contents(&item);
                    ok = false;
                    break;
                }
                item.key = key;
                push_item(out, &cap, &item);
                skip_ws(ps);
                if (ps->p < ps->end && *ps->p == ',') {
                    ps->p++;
                    continue;
                }
                if (ps->p < ps->end && *ps->p == '}') {
                    ps->p++;
                    break;
                }
                ps->error = "expected ',' or '}'";
                ok = false;
                break;
            }
        }
    } else if (c == '[') {
        out->type = JSON_ARRAY;
        ps->p++;
        size_t cap = 0;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ']') {
            ps->p++;
        } else {
            for (;;) {
                Json item;
                if (!parse_value(ps, &item)) {
                    json_free_contents(&item);
                    ok = false;
                    break;
                }
                push_item(out, &cap, &item);
                skip_ws(ps);
                if (ps->p < ps->end && *ps->p == ',') {
                    ps->p++;
                    continue;
                }
                if (ps->p < ps->end && *ps->p == ']') {
                    ps->p++;
                    break;
                }
                ps->error = "expected ',' or ']'";
                ok = false;
                break;
            }
        }
    } else if (c == '"') {
        out->type = JSON_STRING;
        out->string = parse_string(ps);
        ok = out->string != NULL;
    } else if (literal(ps, "true")) {
        out->type = JSON_BOOL;
        out->boolean = true;
    } else if (literal(ps, "false")) {
        out->type = JSON_BOOL;
    } else if (literal(ps, "null")) {
        out->type = JSON_NULL;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        const char *start = ps->p;
        while (ps->p < ps->end && strchr("+-0123456789.eE", *ps->p)) ps->p++;
        char *tmp = xstrndup(start, (size_t)(ps->p - start));
        char *endp = NULL;
        out->type = JSON_NUMBER;
        out->number = strtod(tmp, &endp);
        ok = endp && *endp == 0;
        if (!ok) ps->error = "invalid number";
        free(tmp);
    } else {
        ps->error = "unexpected character";
        ok = false;
    }
    ps->depth--;
    return ok;
}

static void json_free_contents(Json *j)
{
    free(j->string);
    free(j->key);
    for (size_t i = 0; i < j->count; i++) json_free_contents(&j->items[i]);
    free(j->items);
}

Json *json_parse(const char *text, size_t len, char *err, size_t errlen)
{
    Parser ps = {.p = text, .end = text + len};
    Json *root = xcalloc(1, sizeof(Json));
    if (!parse_value(&ps, root)) {
        snprintf(err, errlen, "invalid JSON: %s", ps.error ? ps.error : "parse error");
        json_free_contents(root);
        free(root);
        return NULL;
    }
    skip_ws(&ps);
    if (ps.p != ps.end) {
        snprintf(err, errlen, "invalid JSON: trailing data");
        json_free_contents(root);
        free(root);
        return NULL;
    }
    return root;
}

void json_free(Json *j)
{
    if (!j) return;
    json_free_contents(j);
    free(j);
}

const Json *json_get(const Json *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->count; i++)
        if (strcmp(obj->items[i].key, key) == 0) return &obj->items[i];
    return NULL;
}

const char *json_get_string(const Json *obj, const char *key, const char *def)
{
    const Json *v = json_get(obj, key);
    return v && v->type == JSON_STRING ? v->string : def;
}

bool json_get_bool(const Json *obj, const char *key, bool def)
{
    const Json *v = json_get(obj, key);
    return v && v->type == JSON_BOOL ? v->boolean : def;
}

double json_get_number(const Json *obj, const char *key, double def)
{
    const Json *v = json_get(obj, key);
    return v && v->type == JSON_NUMBER ? v->number : def;
}

void json_get_string_list(const Json *obj, const char *key, StrList *out)
{
    const Json *v = json_get(obj, key);
    if (!v) return;
    if (v->type == JSON_STRING) {
        strlist_push(out, v->string);
        return;
    }
    if (v->type != JSON_ARRAY) return;
    for (size_t i = 0; i < v->count; i++)
        if (v->items[i].type == JSON_STRING) strlist_push(out, v->items[i].string);
}

void json_write_string(Buf *b, const char *s)
{
    buf_append_str(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': buf_append_str(b, "\\\""); break;
        case '\\': buf_append_str(b, "\\\\"); break;
        case '\n': buf_append_str(b, "\\n"); break;
        case '\r': buf_append_str(b, "\\r"); break;
        case '\t': buf_append_str(b, "\\t"); break;
        default:
            if (*p < 0x20) buf_appendf(b, "\\u%04x", *p);
            else buf_append(b, p, 1);
        }
    }
    buf_append_str(b, "\"");
}

void json_write_string_list(Buf *b, const StrList *l)
{
    buf_append_str(b, "[");
    for (size_t i = 0; i < l->len; i++) {
        if (i) buf_append_str(b, ",");
        json_write_string(b, l->items[i]);
    }
    buf_append_str(b, "]");
}
