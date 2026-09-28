// src/pty.h
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "util.h"

typedef struct {
    char *const *argv;
    const char *cwd;
    const StrList *env;
    uint16_t cols;
    uint16_t rows;
    uint32_t width_px;
    uint32_t height_px;
} PtySpawn;

int pty_spawn(const PtySpawn *spec, pid_t *pid_out, char *err, size_t errlen);
void pty_resize(int fd, uint16_t cols, uint16_t rows, uint32_t width_px, uint32_t height_px);
char *default_shell(const char *configured);
