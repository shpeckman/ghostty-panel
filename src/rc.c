// src/rc.c
#include "rc.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "json.h"

extern char **environ;

static const char frame_prefix[] = "\x1bP@kitty-cmd";
static const char frame_suffix[] = "\x1b\\";

typedef enum {
    LISTEN_RC,
    LISTEN_SINGLE_INSTANCE,
} ListenKind;

typedef struct {
    int fd;
    ListenKind kind;
    bool same_user;
    char *unlink_path;
} Listener;

typedef struct {
    RcServer *server;
    int fd;
    ListenKind kind;
    Buf in;
    Buf out;
    bool closing;
    bool dead;
    uint64_t wait_panel;
} Conn;

struct RcServer {
    App *app;
    Listener listeners[4];
    size_t nlisteners;
    Conn **conns;
    size_t nconns;
};

typedef enum {
    CMD_OK,
    CMD_ERROR,
    CMD_DEFERRED,
} CmdResult;

typedef CmdResult (*CmdFn)(RcServer *s, Conn *c, const Json *msg, const Json *payload, Buf *data, char *err, size_t errlen);

char *rc_resolve_address(const char *address)
{
    char *expanded = expand_vars(address);
    Buf out = {0};
    for (const char *p = expanded; *p;) {
        if (strncmp(p, "{kitty_pid}", 11) == 0) {
            buf_appendf(&out, "%d", (int)getpid());
            p += 11;
        } else {
            buf_append(&out, p, 1);
            p++;
        }
    }
    free(expanded);
    char *s = buf_cstr(&out);
    if (strncmp(s, "unix:", 5) == 0 && s[5] && s[5] != '/' && s[5] != '@') {
        const char *tmp = getenv("TMPDIR");
        char *abs = xasprintf("unix:%s/%s", tmp && *tmp ? tmp : "/tmp", s + 5);
        free(s);
        return abs;
    }
    return s;
}

char *rc_service_address(void)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime) return xasprintf("unix:%s/ghostty-panel.sock", runtime);
    return xasprintf("unix:@ghostty-panel-service-%u", (unsigned)getuid());
}

static bool is_unix_address(const char *address)
{
    return strncmp(address, "unix:", 5) == 0;
}

static int tcp_socket(const char *hostport, bool listening, char *err, size_t errlen)
{
    const char *colon = strrchr(hostport, ':');
    if (!colon) {
        snprintf(err, errlen, "invalid tcp address: %s", hostport);
        return -1;
    }
    char *host = xstrndup(hostport, (size_t)(colon - hostport));
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM, .ai_flags = listening ? AI_PASSIVE : 0};
    struct addrinfo *res = NULL;
    int rc = getaddrinfo(*host ? host : NULL, colon + 1, &hints, &res);
    free(host);
    if (rc != 0) {
        snprintf(err, errlen, "cannot resolve %s: %s", hostport, gai_strerror(rc));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *ai = res; ai && fd < 0; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) continue;
        int one = 1;
        if (listening) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        bool ok = listening ? bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 && listen(fd, 16) == 0 : connect(fd, ai->ai_addr, ai->ai_addrlen) == 0;
        if (!ok) {
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);
    if (fd < 0) snprintf(err, errlen, "cannot %s %s: %s", listening ? "listen on" : "connect to", hostport, strerror(errno));
    return fd;
}

static int connect_address(const char *address, char *err, size_t errlen)
{
    if (strncmp(address, "tcp:", 4) == 0) return tcp_socket(address + 4, false, err, errlen);
    if (!is_unix_address(address)) {
        snprintf(err, errlen, "unsupported address: %s", address);
        return -1;
    }
    struct sockaddr_un sa;
    socklen_t len;
    if (!unix_sockaddr(address + 5, &sa, &len)) {
        snprintf(err, errlen, "socket path too long: %s", address);
        return -1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&sa, len) != 0) {
        snprintf(err, errlen, "cannot connect to %s: %s", address, strerror(errno));
        if (fd >= 0) close(fd);
        return -1;
    }
    return fd;
}

static int listen_address(const char *address, char **unlink_path, char *err, size_t errlen)
{
    *unlink_path = NULL;
    if (strncmp(address, "tcp:", 4) == 0) return tcp_socket(address + 4, true, err, errlen);
    if (!is_unix_address(address)) {
        snprintf(err, errlen, "unsupported address: %s", address);
        return -1;
    }
    const char *path = address + 5;
    struct sockaddr_un sa;
    socklen_t len;
    if (!unix_sockaddr(path, &sa, &len)) {
        snprintf(err, errlen, "socket path too long: %s", address);
        return -1;
    }
    if (path[0] != '@') {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISSOCK(st.st_mode)) {
            char cerr[64];
            int probe = connect_address(address, cerr, sizeof cerr);
            if (probe >= 0) {
                close(probe);
                snprintf(err, errlen, "%s is already in use", address);
                return -1;
            }
            unlink(path);
        }
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&sa, len) != 0 || listen(fd, 16) != 0) {
        snprintf(err, errlen, "cannot listen on %s: %s", address, strerror(errno));
        if (fd >= 0) close(fd);
        return -1;
    }
    if (path[0] != '@') *unlink_path = xstrdup(path);
    return fd;
}

static char *single_instance_address(const char *group)
{
    return xasprintf("unix:@ghostty-panel-ipc-%u-%s", (unsigned)getuid(), group && *group ? group : "default");
}

static void frame(Buf *b, const char *json)
{
    buf_append(b, frame_prefix, sizeof frame_prefix - 1);
    buf_append_str(b, json);
    buf_append(b, frame_suffix, sizeof frame_suffix - 1);
}

static bool extract_frame(Buf *in, char **out)
{
    if (!in->len) return false;
    char *start = memmem(in->data, in->len, frame_prefix, sizeof frame_prefix - 1);
    if (!start) return false;
    size_t body = (size_t)(start - in->data) + sizeof frame_prefix - 1;
    char *end = memmem(in->data + body, in->len - body, frame_suffix, sizeof frame_suffix - 1);
    if (!end) return false;
    *out = xstrndup(in->data + body, (size_t)(end - (in->data + body)));
    buf_consume(in, (size_t)(end - in->data) + sizeof frame_suffix - 1);
    return true;
}

RcServer *rc_server_new(App *app)
{
    RcServer *s = xcalloc(1, sizeof(RcServer));
    s->app = app;
    return s;
}

static void add_listener(RcServer *s, int fd, ListenKind kind, bool same_user, char *unlink_path)
{
    set_nonblocking(fd);
    s->listeners[s->nlisteners++] = (Listener){.fd = fd, .kind = kind, .same_user = same_user, .unlink_path = unlink_path};
}

bool rc_server_listen(RcServer *s, const char *address, bool same_user, char *err, size_t errlen)
{
    if (s->nlisteners >= ARRAY_LEN(s->listeners)) {
        snprintf(err, errlen, "too many listening sockets");
        return false;
    }
    char *path = NULL;
    int fd = listen_address(address, &path, err, errlen);
    if (fd < 0) return false;
    add_listener(s, fd, LISTEN_RC, same_user && is_unix_address(address), path);
    return true;
}

bool rc_server_listen_single_instance(RcServer *s, const char *group, char *err, size_t errlen)
{
    char *address = single_instance_address(group);
    char *path = NULL;
    int fd = listen_address(address, &path, err, errlen);
    free(address);
    if (fd < 0) return false;
    add_listener(s, fd, LISTEN_SINGLE_INSTANCE, true, path);
    return true;
}

static void conn_close(Conn *c)
{
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->dead = true;
}

void rc_server_free(RcServer *s)
{
    if (!s) return;
    for (size_t i = 0; i < s->nlisteners; i++) {
        close(s->listeners[i].fd);
        if (s->listeners[i].unlink_path) unlink(s->listeners[i].unlink_path);
        free(s->listeners[i].unlink_path);
    }
    for (size_t i = 0; i < s->nconns; i++) {
        conn_close(s->conns[i]);
        buf_free(&s->conns[i]->in);
        buf_free(&s->conns[i]->out);
        free(s->conns[i]);
    }
    free(s->conns);
    free(s);
}

static void respond(Conn *c, bool ok, const char *data, const char *error)
{
    Buf json = {0};
    buf_appendf(&json, "{\"ok\":%s", ok ? "true" : "false");
    if (data) {
        buf_append_str(&json, ",\"data\":");
        json_write_string(&json, data);
    }
    if (error) {
        buf_append_str(&json, ",\"error\":");
        json_write_string(&json, error);
    }
    buf_append_str(&json, "}");
    frame(&c->out, buf_cstr(&json));
    buf_free(&json);
}

static Panel *default_panel(App *app)
{
    for (size_t i = 0; i < app->npanels; i++)
        if (app->panels[i] == app->active) return app->active;
    return app->npanels ? app->panels[app->npanels - 1] : NULL;
}

static size_t match_panels(RcServer *s, const Json *msg, const Json *payload, Panel ***out, char *err, size_t errlen)
{
    App *app = s->app;
    Panel **list = xcalloc(app->npanels + 1, sizeof(Panel *));
    size_t n = 0;
    const char *match = json_get_string(payload, "match", NULL);
    if (json_get_bool(payload, "self", false)) {
        Panel *p = app_find_panel(app, (uint64_t)json_get_number(msg, "kitty_window_id", 0));
        if (p) list[n++] = p;
    } else if (!match || !*match) {
        Panel *p = default_panel(app);
        if (p) list[n++] = p;
    } else if (strcmp(match, "all") == 0) {
        for (size_t i = 0; i < app->npanels; i++) list[n++] = app->panels[i];
    } else if (strncmp(match, "name:", 5) == 0) {
        for (size_t i = 0; i < app->npanels; i++)
            if (app->panels[i]->name && strcmp(app->panels[i]->name, match + 5) == 0) list[n++] = app->panels[i];
    } else if (strncmp(match, "id:", 3) == 0) {
        long id;
        if (!parse_long(match + 3, &id)) {
            snprintf(err, errlen, "invalid match expression: %s", match);
            free(list);
            return 0;
        }
        Panel *p = app_find_panel(app, (uint64_t)id);
        if (p) list[n++] = p;
    } else {
        snprintf(err, errlen, "unsupported match expression: %s (use id:N, name:NAME or all)", match);
        free(list);
        return 0;
    }
    if (!n) {
        snprintf(err, errlen, "no matching panels");
        free(list);
        return 0;
    }
    *out = list;
    return n;
}

static CmdResult cmd_resize_os_window(RcServer *s, Conn *c, const Json *msg, const Json *payload, Buf *data, char *err, size_t errlen)
{
    Panel **panels = NULL;
    size_t n = match_panels(s, msg, payload, &panels, err, errlen);
    if (!n) return CMD_ERROR;
    const char *action = json_get_string(payload, "action", "resize");
    StrList settings = {0};
    json_get_string_list(payload, "os_panel", &settings);
    bool incremental = json_get_bool(payload, "incremental", false);
    CmdResult result = CMD_OK;
    for (size_t i = 0; i < n && result == CMD_OK; i++) {
        Panel *p = panels[i];
        if (strcmp(action, "os-panel") == 0) {
            if (!settings.len) {
                snprintf(err, errlen, "Must specify at least one panel setting");
                result = CMD_ERROR;
                break;
            }
            LayerConfig lsc;
            if (!layer_config_from_settings(&settings, &p->lsc, incremental, &lsc, err, errlen)) {
                result = CMD_ERROR;
                break;
            }
            panel_set_layer_config(p, &lsc);
        } else if (strcmp(action, "toggle-visibility") == 0) {
            panel_toggle(p);
        } else if (strcmp(action, "show") == 0) {
            panel_show(p);
        } else if (strcmp(action, "hide") == 0) {
            panel_hide(p);
        } else {
            snprintf(err, errlen, "The OS Window %llu is a desktop panel, use --action=os-panel to change it", (unsigned long long)p->id);
            result = CMD_ERROR;
        }
    }
    strlist_clear(&settings);
    free(panels);
    return result;
}

static CmdResult cmd_launch(RcServer *s, Conn *c, const Json *msg, const Json *payload, Buf *data, char *err, size_t errlen)
{
    const char *type = json_get_string(payload, "type", "window");
    if (strcmp(type, "os-panel") != 0) {
        snprintf(err, errlen, "ghostty-panel can only launch --type=os-panel, not %s", type);
        return CMD_ERROR;
    }
    StrList settings = {0}, args = {0}, env = {0};
    json_get_string_list(payload, "os_panel", &settings);
    json_get_string_list(payload, "args", &args);
    json_get_string_list(payload, "env", &env);
    LayerConfig lsc;
    CmdResult result = CMD_ERROR;
    if (layer_config_from_settings(&settings, NULL, false, &lsc, err, errlen)) {
        const char *cwd = json_get_string(payload, "cwd", NULL);
        PanelSpec spec = {
            .lsc = lsc,
            .app_id = json_get_string(payload, "os_window_class", NULL),
            .name = json_get_string(payload, "os_window_name", NULL),
            .argv = &args,
            .cwd = cwd && *cwd && strcmp(cwd, "current") != 0 ? cwd : NULL,
            .env = &env,
            .cfg = &s->app->cfg,
            .hold = json_get_bool(payload, "hold", false),
        };
        Panel *p = app_create_panel(s->app, &spec, err, errlen);
        if (p) {
            buf_appendf(data, "%llu", (unsigned long long)p->id);
            result = CMD_OK;
        }
    }
    strlist_clear(&settings);
    strlist_clear(&args);
    strlist_clear(&env);
    return result;
}

static CmdResult cmd_close_window(RcServer *s, Conn *c, const Json *msg, const Json *payload, Buf *data, char *err, size_t errlen)
{
    Panel **panels = NULL;
    size_t n = match_panels(s, msg, payload, &panels, err, errlen);
    if (!n) return CMD_ERROR;
    for (size_t i = 0; i < n; i++) app_remove_panel(s->app, panels[i]);
    free(panels);
    return CMD_OK;
}

static void write_panel(Buf *out, const Panel *p)
{
    buf_appendf(out, "{\"id\":%llu,\"name\":", (unsigned long long)p->id);
    if (p->name) json_write_string(out, p->name);
    else buf_append_str(out, "null");
    buf_append_str(out, ",\"app_id\":");
    json_write_string(out, p->app_id);
    buf_appendf(out, ",\"pid\":%d,\"visible\":%s,\"focused\":%s,\"columns\":%u,\"lines\":%u,\"os_panel\":", (int)p->pid,
                p->visible ? "true" : "false", p->focused ? "true" : "false", p->cols, p->rows);
    StrList settings = {0};
    layer_config_to_settings(&p->lsc, &settings);
    json_write_string_list(out, &settings);
    strlist_clear(&settings);
    buf_append_str(out, "}");
}

static CmdResult cmd_ls(RcServer *s, Conn *c, const Json *msg, const Json *payload, Buf *data, char *err, size_t errlen)
{
    App *app = s->app;
    const char *match = json_get_string(payload, "match", NULL);
    Panel **panels = NULL;
    size_t n = 0;
    if (match && *match) {
        n = match_panels(s, msg, payload, &panels, err, errlen);
        if (!n) return CMD_ERROR;
    } else {
        panels = xcalloc(app->npanels + 1, sizeof(Panel *));
        for (size_t i = 0; i < app->npanels; i++) panels[n++] = app->panels[i];
    }
    buf_append_str(data, "[");
    for (size_t i = 0; i < n; i++) {
        if (i) buf_append_str(data, ",");
        write_panel(data, panels[i]);
    }
    buf_append_str(data, "]");
    free(panels);
    return CMD_OK;
}

static void waiter_panel_closed(void *ctx, Panel *p)
{
    RcServer *s = ctx;
    for (size_t i = 0; i < s->nconns; i++) {
        Conn *c = s->conns[i];
        if (c->dead || c->wait_panel != p->id) continue;
        c->wait_panel = 0;
        respond(c, true, NULL, NULL);
        c->closing = true;
    }
}

static CmdResult cmd_single_instance(RcServer *s, Conn *c, const Json *msg, const Json *payload, Buf *data, char *err, size_t errlen)
{
    App *app = s->app;
    StrList argv = {0}, env = {0};
    json_get_string_list(payload, "argv", &argv);
    json_get_string_list(payload, "env", &env);
    PanelOptions opts;
    cli_defaults(&panel_options_table, &opts);
    char perr[256] = {0};
    int first = cli_parse(&panel_options_table, &opts, (int)argv.len, argv.items, NULL, perr, sizeof perr);
    CmdResult result = CMD_ERROR;
    Config cfg;
    bool have_cfg = false;
    if (first < 0) {
        snprintf(err, errlen, "invalid arguments: %s", first == CLI_HELP ? "--help" : perr);
        goto done;
    }
    if (opts.config.len || opts.override.len) {
        config_defaults(&cfg);
        config_load(&cfg, &opts.config, &opts.override);
    } else {
        config_copy(&cfg, &app->cfg);
    }
    have_cfg = true;
    LayerConfig lsc = layer_config_from_options(&opts);
    if (opts.toggle_visibility && app->npanels) {
        for (size_t i = 0; i < app->npanels; i++) {
            Panel *p = app->panels[i];
            bool changed = !layer_config_equal(&p->lsc, &lsc);
            panel_toggle(p);
            if (changed) panel_set_layer_config(p, &lsc);
        }
        result = CMD_OK;
        goto done;
    }
    StrList items = {0};
    for (size_t i = (size_t)first; i < argv.len; i++) strlist_push(&items, argv.items[i]);
    PanelSpec spec = {
        .lsc = lsc,
        .app_id = opts.app_id,
        .name = opts.name,
        .argv = &items,
        .cwd = json_get_string(payload, "cwd", NULL),
        .env = &env,
        .cfg = &cfg,
        .start_hidden = opts.start_as_hidden,
        .grab_keyboard = opts.grab_keyboard,
    };
    Panel *p = app_create_panel(app, &spec, err, errlen);
    strlist_clear(&items);
    if (!p) goto done;
    buf_appendf(data, "%llu", (unsigned long long)p->id);
    result = CMD_OK;
    if (json_get_bool(payload, "wait", false)) {
        c->wait_panel = p->id;
        p->on_close = waiter_panel_closed;
        p->on_close_ctx = s;
        result = CMD_DEFERRED;
    }
done:
    if (have_cfg) config_free(&cfg);
    cli_free(&panel_options_table, &opts);
    strlist_clear(&argv);
    strlist_clear(&env);
    return result;
}

typedef struct {
    const char *name;
    ListenKind kind;
    CmdFn fn;
} Command;

static const Command commands[] = {
    {"resize-os-window", LISTEN_RC, cmd_resize_os_window},
    {"launch", LISTEN_RC, cmd_launch},
    {"close-window", LISTEN_RC, cmd_close_window},
    {"ls", LISTEN_RC, cmd_ls},
    {"single-instance", LISTEN_SINGLE_INSTANCE, cmd_single_instance},
};

static void handle_message(Conn *c, const char *text)
{
    char err[512] = {0};
    Json *msg = json_parse(text, strlen(text), err, sizeof err);
    if (!msg) {
        respond(c, false, NULL, err);
        return;
    }
    const char *name = json_get_string(msg, "cmd", "");
    bool no_response = json_get_bool(msg, "no_response", false);
    const Json *payload = json_get(msg, "payload");
    const Command *cmd = NULL;
    for (size_t i = 0; i < ARRAY_LEN(commands); i++)
        if (strcmp(commands[i].name, name) == 0 && commands[i].kind == c->kind) cmd = &commands[i];
    log_debug(debug_input, "remote command: %s", name);
    if (!cmd) {
        snprintf(err, sizeof err, "unknown command: %s", name);
        if (!no_response) respond(c, false, NULL, err);
        json_free(msg);
        return;
    }
    Buf data = {0};
    CmdResult r = cmd->fn(c->server, c, msg, payload, &data, err, sizeof err);
    if (r != CMD_DEFERRED && !no_response) respond(c, r == CMD_OK, r == CMD_OK && data.len ? buf_cstr(&data) : NULL, r == CMD_ERROR ? err : NULL);
    buf_free(&data);
    json_free(msg);
}

static void on_conn(void *ctx, int fd, short revents)
{
    Conn *c = ctx;
    if (c->dead) return;
    if (revents & POLLIN) {
        char buf[16384];
        for (;;) {
            ssize_t n = read(c->fd, buf, sizeof buf);
            if (n > 0) {
                buf_append(&c->in, buf, (size_t)n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && errno == EAGAIN) break;
            c->closing = true;
            break;
        }
        char *text;
        while (!c->dead && extract_frame(&c->in, &text)) {
            handle_message(c, text);
            free(text);
        }
        if (c->in.len > 16u * 1024 * 1024) {
            conn_close(c);
            return;
        }
    }
    if (revents & (POLLERR | POLLHUP) && !(revents & POLLIN)) {
        conn_close(c);
        return;
    }
    while (c->out.len) {
        ssize_t n = write(c->fd, c->out.data, c->out.len);
        if (n > 0) {
            buf_consume(&c->out, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno != EAGAIN) {
            conn_close(c);
            return;
        }
        break;
    }
    if (c->closing && !c->out.len && !c->wait_panel) conn_close(c);
}

static void on_listener(void *ctx, int fd, short revents)
{
    RcServer *s = ctx;
    const Listener *listener = NULL;
    for (size_t i = 0; i < s->nlisteners; i++)
        if (s->listeners[i].fd == fd) listener = &s->listeners[i];
    if (!listener) return;
    ListenKind kind = listener->kind;
    for (;;) {
        int cfd = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (cfd < 0) break;
        if (listener->same_user) {
            struct ucred cred;
            socklen_t len = sizeof cred;
            if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || cred.uid != getuid()) {
                close(cfd);
                continue;
            }
        }
        Conn *c = xcalloc(1, sizeof(Conn));
        *c = (Conn){.server = s, .fd = cfd, .kind = kind};
        s->conns = xrealloc(s->conns, (s->nconns + 1) * sizeof(Conn *));
        s->conns[s->nconns++] = c;
    }
}

void rc_server_collect_watches(RcServer *s)
{
    size_t out = 0;
    for (size_t i = 0; i < s->nconns; i++) {
        Conn *c = s->conns[i];
        if (c->dead) {
            for (size_t k = 0; k < s->app->npanels; k++)
                if (s->app->panels[k]->on_close_ctx == s && s->app->panels[k]->id == c->wait_panel) s->app->panels[k]->on_close = NULL;
            buf_free(&c->in);
            buf_free(&c->out);
            free(c);
            continue;
        }
        s->conns[out++] = c;
    }
    s->nconns = out;
    for (size_t i = 0; i < s->nlisteners; i++) app_watch(s->app, s->listeners[i].fd, POLLIN, on_listener, s);
    for (size_t i = 0; i < s->nconns; i++) {
        Conn *c = s->conns[i];
        app_watch(s->app, c->fd, (short)((c->closing ? 0 : POLLIN) | (c->out.len ? POLLOUT : 0)), on_conn, c);
    }
}

static bool send_and_receive(int fd, const char *json, bool wait_response, int timeout_ms, char **response, char *err, size_t errlen)
{
    Buf out = {0};
    frame(&out, json);
    bool ok = write_all(fd, out.data, out.len);
    buf_free(&out);
    if (!ok) {
        snprintf(err, errlen, "write failed: %s", strerror(errno));
        return false;
    }
    *response = NULL;
    if (!wait_response) return true;
    Buf in = {0};
    for (;;) {
        if (extract_frame(&in, response)) break;
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int r = poll(&pfd, 1, timeout_ms);
        if (r == 0) {
            snprintf(err, errlen, "timed out waiting for a response");
            buf_free(&in);
            return false;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            snprintf(err, errlen, "poll failed: %s", strerror(errno));
            buf_free(&in);
            return false;
        }
        char buf[16384];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            snprintf(err, errlen, "connection closed without a response");
            buf_free(&in);
            return false;
        }
        buf_append(&in, buf, (size_t)n);
    }
    buf_free(&in);
    return true;
}

static int report_response(const char *response, bool print_data)
{
    char err[256];
    Json *r = json_parse(response, strlen(response), err, sizeof err);
    if (!r) {
        fprintf(stderr, "Invalid response: %s\n", err);
        return 1;
    }
    int code = 0;
    if (!json_get_bool(r, "ok", false)) {
        fprintf(stderr, "Error: %s\n", json_get_string(r, "error", "unknown error"));
        code = 1;
    } else if (print_data) {
        const char *data = json_get_string(r, "data", NULL);
        if (data && *data) printf("%s\n", data);
    }
    json_free(r);
    return code;
}

bool single_instance_forward(const char *group, int argc, char **argv, bool wait, int *exit_code)
{
    char *address = single_instance_address(group);
    char err[256];
    int fd = connect_address(address, err, sizeof err);
    free(address);
    if (fd < 0) return false;
    Buf json = {0};
    buf_append_str(&json, "{\"cmd\":\"single-instance\",\"version\":[0,42,0],\"payload\":{\"argv\":[");
    for (int i = 0; i < argc; i++) {
        if (i) buf_append_str(&json, ",");
        json_write_string(&json, argv[i]);
    }
    buf_append_str(&json, "],\"env\":[");
    for (char **e = environ; *e; e++) {
        if (e != environ) buf_append_str(&json, ",");
        json_write_string(&json, *e);
    }
    char cwd[4096];
    buf_append_str(&json, "],\"cwd\":");
    json_write_string(&json, getcwd(cwd, sizeof cwd) ? cwd : "");
    buf_appendf(&json, ",\"wait\":%s}}", wait ? "true" : "false");
    char *response = NULL;
    bool ok = send_and_receive(fd, buf_cstr(&json), true, -1, &response, err, sizeof err);
    buf_free(&json);
    close(fd);
    if (!ok) {
        fprintf(stderr, "ghostty-panel: single instance: %s\n", err);
        *exit_code = 1;
        return true;
    }
    *exit_code = report_response(response, false);
    free(response);
    return true;
}

typedef struct {
    char *match;
    bool self;
    int action;
    int unit;
    long width;
    long height;
    bool incremental;
    bool no_response;
} ResizeOptions;

typedef struct {
    char *match;
    bool self;
    bool no_response;
} CloseOptions;

typedef struct {
    char *match;
} LsOptions;

typedef struct {
    char *match;
    int type;
    StrList os_panel;
    char *cwd;
    StrList env;
    bool hold;
    char *os_window_class;
    char *os_window_name;
    bool self;
    bool no_response;
} LaunchOptions;

static const char *const resize_actions[] = {"resize", "toggle-fullscreen", "toggle-maximized", "toggle-visibility", "hide", "show", "os-panel", NULL};
static const char *const resize_units[] = {"cells", "pixels", NULL};
static const char *const launch_types[] = {"window", "tab", "os-window", "os-panel", "overlay", "overlay-main", "background", "clipboard", "primary", NULL};

#define RO(field) offsetof(ResizeOptions, field)
#define LO(field) offsetof(LaunchOptions, field)
#define CO(field) offsetof(CloseOptions, field)
#define LSO(field) offsetof(LsOptions, field)

static const OptSpec resize_specs[] = {
    {"--match -m", OPT_STR, RO(match), NULL, NULL, "match", 0, "The panel to act on: id:N, name:NAME or all. Defaults to the active panel."},
    {"--self", OPT_FLAG, RO(self), NULL, NULL, "self", 0, "Act on the panel this command is run in."},
    {"--action", OPT_CHOICE, RO(action), resize_actions, "resize", "action", 0,
     "The action to perform. Panels support toggle-visibility, hide, show and os-panel."},
    {"--unit", OPT_CHOICE, RO(unit), resize_units, "cells", "unit", 0, "Accepted for compatibility with kitty."},
    {"--width", OPT_INT, RO(width), NULL, "0", "width", 0, "Accepted for compatibility with kitty."},
    {"--height", OPT_INT, RO(height), NULL, "0", "height", 0, "Accepted for compatibility with kitty."},
    {"--incremental", OPT_FLAG, RO(incremental), NULL, NULL, "incremental", 0,
     "With --action=os-panel, only change the specified panel settings."},
    {"--no-response", OPT_FLAG, RO(no_response), NULL, NULL, NULL, 0, "Do not wait for a response."},
};

static const OptSpec launch_specs[] = {
    {"--match -m", OPT_STR, LO(match), NULL, NULL, "match", 0, "Accepted for compatibility with kitty."},
    {"--type", OPT_CHOICE, LO(type), launch_types, "window", "type", 0, "The type of window to open. Only os-panel is supported."},
    {"--os-panel", OPT_LIST, LO(os_panel), NULL, NULL, "os_panel", 0,
     "A panel setting in the same syntax as the panel options without the leading dashes, e.g. edge=top. Can be given multiple times."},
    {"--cwd", OPT_STR, LO(cwd), NULL, NULL, "cwd", 0, "The working directory for the new panel's program."},
    {"--env", OPT_LIST, LO(env), NULL, NULL, "env", 0, "An environment variable NAME=VALUE for the program. Can be given multiple times."},
    {"--hold", OPT_FLAG, LO(hold), NULL, NULL, "hold", 0, "Keep the panel open after the program exits."},
    {"--os-window-class", OPT_STR, LO(os_window_class), NULL, NULL, "os_window_class", 0, "The layer shell namespace of the new panel."},
    {"--os-window-name", OPT_STR, LO(os_window_name), NULL, NULL, "os_window_name", 0,
     "A name for the new panel. Other commands select it with --match name:NAME."},
    {"--self", OPT_FLAG, LO(self), NULL, NULL, "self", 0, "Accepted for compatibility with kitty."},
    {"--no-response", OPT_FLAG, LO(no_response), NULL, NULL, NULL, 0, "Do not wait for a response."},
};

static const OptSpec close_specs[] = {
    {"--match -m", OPT_STR, CO(match), NULL, NULL, "match", 0, "The panels to close: id:N, name:NAME or all. Defaults to the active panel."},
    {"--self", OPT_FLAG, CO(self), NULL, NULL, "self", 0, "Close the panel this command is run in."},
    {"--no-response", OPT_FLAG, CO(no_response), NULL, NULL, NULL, 0, "Do not wait for a response."},
};

static const OptSpec ls_specs[] = {
    {"--match -m", OPT_STR, LSO(match), NULL, NULL, "match", 0, "The panels to list: id:N, name:NAME or all. Defaults to all panels."},
};

static const OptTable resize_table = {
    .specs = resize_specs,
    .count = ARRAY_LEN(resize_specs),
    .usage = "resize-os-window [options] [panel settings ...]",
    .help = "Show, hide or change panels. Use --action=os-panel with settings like edge=bottom lines=4.",
    .positional_json = "os_panel",
};

static const OptTable launch_table = {
    .specs = launch_specs,
    .count = ARRAY_LEN(launch_specs),
    .usage = "launch --type=os-panel [options] [program ...]",
    .help = "Create a new panel running program in the existing instance. Prints the id of the new panel.",
    .positional_json = "args",
};

static const OptTable close_table = {
    .specs = close_specs,
    .count = ARRAY_LEN(close_specs),
    .usage = "close-window [options]",
    .help = "Close panels and terminate the programs running in them.",
};

static const OptTable ls_table = {
    .specs = ls_specs,
    .count = ARRAY_LEN(ls_specs),
    .usage = "ls [options]",
    .help = "Print the panels as JSON. The os_panel field of each panel holds its settings in --os-panel syntax.",
};

typedef struct {
    const char *name;
    const OptTable *table;
    size_t size;
    ptrdiff_t no_response;
} ClientCommand;

static const ClientCommand client_commands[] = {
    {"resize-os-window", &resize_table, sizeof(ResizeOptions), (ptrdiff_t)RO(no_response)},
    {"launch", &launch_table, sizeof(LaunchOptions), (ptrdiff_t)LO(no_response)},
    {"close-window", &close_table, sizeof(CloseOptions), (ptrdiff_t)CO(no_response)},
    {"ls", &ls_table, sizeof(LsOptions), -1},
};

static void client_usage(FILE *f)
{
    fputs("Usage: ghostty-panel @ [--to ADDRESS] COMMAND [options] [args ...]\n\n"
          "Control running panels. ADDRESS defaults to $GHOSTTY_PANEL_LISTEN_ON,\n"
          "or to the socket of ghostty-panel-service when that is not set.\n"
          "Messages use the kitty remote control protocol.\n\nCommands:\n",
          f);
    for (size_t i = 0; i < ARRAY_LEN(client_commands); i++) fprintf(f, "  %s\n", client_commands[i].name);
    fputs("\nRun ghostty-panel @ COMMAND --help for the options of a command.\n", f);
}

int rc_client_main(int argc, char **argv)
{
    const char *to = getenv("GHOSTTY_PANEL_LISTEN_ON");
    int i = 0;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            client_usage(stdout);
            return 0;
        }
        if (strncmp(argv[i], "--to=", 5) == 0) {
            to = argv[i] + 5;
            i++;
        } else if (strcmp(argv[i], "--to") == 0 && i + 1 < argc) {
            to = argv[i + 1];
            i += 2;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    if (i >= argc) {
        client_usage(stderr);
        return 1;
    }
    const ClientCommand *cmd = NULL;
    for (size_t k = 0; k < ARRAY_LEN(client_commands); k++)
        if (strcmp(client_commands[k].name, argv[i]) == 0) cmd = &client_commands[k];
    if (!cmd) {
        fprintf(stderr, "Unknown command: %s\n", argv[i]);
        return 1;
    }
    char prog[64];
    snprintf(prog, sizeof prog, "ghostty-panel @");
    void *opts = xcalloc(1, cmd->size);
    cli_defaults(cmd->table, opts);
    char err[512] = {0};
    int first = cli_parse(cmd->table, opts, argc - i - 1, argv + i + 1, NULL, err, sizeof err);
    if (first == CLI_HELP) {
        cli_print_help(cmd->table, prog, stdout);
        cli_free(cmd->table, opts);
        free(opts);
        return 0;
    }
    if (first == CLI_ERROR) {
        fprintf(stderr, "%s\n", err);
        cli_free(cmd->table, opts);
        free(opts);
        return 1;
    }
    StrList positional = {0};
    for (int k = i + 1 + first; k < argc; k++) strlist_push(&positional, argv[k]);
    bool no_response = cmd->no_response >= 0 && *(bool *)((char *)opts + cmd->no_response);
    Buf json = {0};
    buf_append_str(&json, "{\"cmd\":");
    json_write_string(&json, cmd->name);
    buf_appendf(&json, ",\"version\":[0,42,0],\"no_response\":%s", no_response ? "true" : "false");
    long wid;
    if (parse_long(getenv("GHOSTTY_PANEL_WINDOW_ID"), &wid)) buf_appendf(&json, ",\"kitty_window_id\":%ld", wid);
    buf_append_str(&json, ",\"payload\":");
    cli_payload(cmd->table, opts, &positional, &json);
    buf_append_str(&json, "}");
    strlist_clear(&positional);
    cli_free(cmd->table, opts);
    free(opts);

    char *address = to && *to ? rc_resolve_address(to) : rc_service_address();
    int fd = connect_address(address, err, sizeof err);
    free(address);
    int code = 1;
    if (fd < 0) {
        fprintf(stderr, "%s\n", err);
    } else {
        char *response = NULL;
        if (!send_and_receive(fd, buf_cstr(&json), !no_response, 10000, &response, err, sizeof err)) {
            fprintf(stderr, "%s\n", err);
        } else {
            code = response ? report_response(response, true) : 0;
        }
        free(response);
        close(fd);
    }
    buf_free(&json);
    return code;
}
