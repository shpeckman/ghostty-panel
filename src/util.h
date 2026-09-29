// src/util.h
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) MIN(MAX((v), (lo)), (hi))

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StrList;

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void buf_append(Buf *b, const void *data, size_t len);
void buf_append_str(Buf *b, const char *s);
void buf_appendf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void buf_consume(Buf *b, size_t n);
char *buf_cstr(Buf *b);
void buf_free(Buf *b);

void strlist_push(StrList *l, const char *s);
void strlist_push_owned(StrList *l, char *s);
void strlist_copy(StrList *dst, const StrList *src);
void strlist_clear(StrList *l);

size_t utf8_encode(uint32_t cp, char out[4]);
uint64_t now_ms(void);
uint64_t hash_bytes(const void *data, size_t len, uint64_t seed);

bool parse_long(const char *s, long *out);
bool parse_double(const char *s, double *out);
char *trim(char *s);
char *expand_vars(const char *s);

void log_set_debug(bool rendering, bool input);
extern bool debug_rendering;
extern bool debug_input;
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_debug(bool enabled, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

bool set_nonblocking(int fd);
bool write_all(int fd, const void *data, size_t len);
bool unix_sockaddr(const char *path, struct sockaddr_un *sa, socklen_t *len);
