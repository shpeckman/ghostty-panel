// src/util.c
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

bool debug_rendering;
bool debug_input;

static _Noreturn void oom(void)
{
    fputs("ghostty-panel: out of memory\n", stderr);
    abort();
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) oom();
    return q;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *out = NULL;
    if (vasprintf(&out, fmt, ap) < 0) oom();
    va_end(ap);
    return out;
}

static void buf_reserve(Buf *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) cap *= 2;
    b->data = xrealloc(b->data, cap);
    b->cap = cap;
}

void buf_append(Buf *b, const void *data, size_t len)
{
    buf_reserve(b, len);
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
}

void buf_append_str(Buf *b, const char *s)
{
    buf_append(b, s, strlen(s));
}

void buf_appendf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        buf_reserve(b, (size_t)n);
        vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
        b->len += (size_t)n;
    }
    va_end(ap2);
}

void buf_consume(Buf *b, size_t n)
{
    if (n >= b->len) {
        b->len = 0;
        if (b->data) b->data[0] = 0;
        return;
    }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    b->data[b->len] = 0;
}

char *buf_cstr(Buf *b)
{
    buf_reserve(b, 0);
    b->data[b->len] = 0;
    return b->data;
}

void buf_free(Buf *b)
{
    free(b->data);
    *b = (Buf){0};
}

void strlist_push_owned(StrList *l, char *s)
{
    if (l->len + 2 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = xrealloc(l->items, l->cap * sizeof(char *));
    }
    l->items[l->len++] = s;
    l->items[l->len] = NULL;
}

void strlist_push(StrList *l, const char *s)
{
    strlist_push_owned(l, xstrdup(s));
}

void strlist_copy(StrList *dst, const StrList *src)
{
    for (size_t i = 0; i < src->len; i++) strlist_push(dst, src->items[i]);
}

void strlist_clear(StrList *l)
{
    for (size_t i = 0; i < l->len; i++) free(l->items[i]);
    free(l->items);
    *l = (StrList){0};
}

size_t utf8_encode(uint32_t cp, char out[4])
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
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

uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint64_t hash_bytes(const void *data, size_t len, uint64_t seed)
{
    const uint8_t *p = data;
    uint64_t h = 0xcbf29ce484222325ull ^ seed;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return h;
}

bool parse_long(const char *s, long *out)
{
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *end) return false;
    *out = v;
    return true;
}

bool parse_double(const char *s, double *out)
{
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || *end) return false;
    *out = v;
    return true;
}

char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}

char *expand_vars(const char *s)
{
    Buf out = {0};
    for (const char *p = s; *p;) {
        if (*p == '$' && (p[1] == '{' || isalpha((unsigned char)p[1]) || p[1] == '_')) {
            const char *start = p + 1;
            bool braced = *start == '{';
            if (braced) start++;
            const char *end = start;
            while (isalnum((unsigned char)*end) || *end == '_') end++;
            char *name = xstrndup(start, (size_t)(end - start));
            const char *val = getenv(name);
            free(name);
            if (val) buf_append_str(&out, val);
            p = braced && *end == '}' ? end + 1 : end;
            continue;
        }
        buf_append(&out, p, 1);
        p++;
    }
    return buf_cstr(&out);
}

void log_set_debug(bool rendering, bool input)
{
    debug_rendering = rendering;
    debug_input = input;
}

void log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("ghostty-panel: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void log_debug(bool enabled, const char *fmt, ...)
{
    if (!enabled) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%llu] ", (unsigned long long)now_ms());
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

bool set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool write_all(int fd, const void *data, size_t len)
{
    const char *p = data;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

bool unix_sockaddr(const char *path, struct sockaddr_un *sa, socklen_t *len)
{
    memset(sa, 0, sizeof *sa);
    sa->sun_family = AF_UNIX;
    size_t n = strlen(path);
    if (!n || n + 1 > sizeof sa->sun_path) return false;
    if (path[0] == '@') {
        memcpy(sa->sun_path + 1, path + 1, n - 1);
        *len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + n);
    } else {
        memcpy(sa->sun_path, path, n);
        *len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + n + 1);
    }
    return true;
}
