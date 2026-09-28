// src/pty.c
#include "pty.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

extern char **environ;

char *default_shell(const char *configured)
{
    if (configured && *configured && strcmp(configured, ".") != 0) return xstrdup(configured);
    const char *shell = getenv("SHELL");
    if (shell && *shell) return xstrdup(shell);
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_shell && *pw->pw_shell) return xstrdup(pw->pw_shell);
    return xstrdup("/bin/sh");
}

static _Noreturn void child_exec(const PtySpawn *spec, const char *slave_name, int err_fd)
{
    setsid();
    int slave = open(slave_name, O_RDWR);
    if (slave < 0) goto fail;
    ioctl(slave, TIOCSCTTY, 0);
    dup2(slave, 0);
    dup2(slave, 1);
    dup2(slave, 2);
    if (slave > 2) close(slave);
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, NULL);
    for (int sig = 1; sig < NSIG; sig++) signal(sig, SIG_DFL);
    if (spec->env) {
        for (size_t i = 0; i < spec->env->len; i++) {
            const char *kv = spec->env->items[i];
            const char *eq = strchr(kv, '=');
            if (!eq) {
                unsetenv(kv);
                continue;
            }
            char *key = xstrndup(kv, (size_t)(eq - kv));
            setenv(key, eq + 1, 1);
            free(key);
        }
    }
    if (spec->cwd && *spec->cwd && chdir(spec->cwd) != 0 && getenv("HOME")) {
        int ignored = chdir(getenv("HOME"));
        (void)ignored;
    }
    execvp(spec->argv[0], spec->argv);
fail:;
    int e = errno;
    ssize_t w = write(err_fd, &e, sizeof e);
    (void)w;
    _exit(127);
}

int pty_spawn(const PtySpawn *spec, pid_t *pid_out, char *err, size_t errlen)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
        snprintf(err, errlen, "cannot open pty: %s", strerror(errno));
        if (master >= 0) close(master);
        return -1;
    }
    char slave_name[256];
    if (ptsname_r(master, slave_name, sizeof slave_name) != 0) {
        snprintf(err, errlen, "ptsname failed: %s", strerror(errno));
        close(master);
        return -1;
    }
    pty_resize(master, spec->cols, spec->rows, spec->width_px, spec->height_px);
    int slave = open(slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave >= 0) {
        struct termios tio;
        if (tcgetattr(slave, &tio) == 0) {
            tio.c_iflag |= IUTF8;
            tcsetattr(slave, TCSANOW, &tio);
        }
        close(slave);
    }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        snprintf(err, errlen, "pipe failed: %s", strerror(errno));
        close(master);
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, errlen, "fork failed: %s", strerror(errno));
        close(master);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(master);
        close(pipefd[0]);
        child_exec(spec, slave_name, pipefd[1]);
    }
    close(pipefd[1]);
    int child_errno = 0;
    ssize_t n;
    do {
        n = read(pipefd[0], &child_errno, sizeof child_errno);
    } while (n < 0 && errno == EINTR);
    close(pipefd[0]);
    if (n == (ssize_t)sizeof child_errno) {
        snprintf(err, errlen, "cannot run %s: %s", spec->argv[0], strerror(child_errno));
        close(master);
        return -1;
    }
    set_nonblocking(master);
    *pid_out = pid;
    return master;
}

void pty_resize(int fd, uint16_t cols, uint16_t rows, uint32_t width_px, uint32_t height_px)
{
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = (unsigned short)MIN(width_px, 65535u),
        .ws_ypixel = (unsigned short)MIN(height_px, 65535u),
    };
    ioctl(fd, TIOCSWINSZ, &ws);
}
