// src/main.c
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "dynload.h"
#include "image.h"
#include "rc.h"

static void detach(const char *log_path)
{
    pid_t pid = fork();
    if (pid < 0) {
        log_msg("fork failed, not detaching");
        return;
    }
    if (pid > 0) _exit(0);
    setsid();
    int in = open("/dev/null", O_RDONLY | O_CLOEXEC);
    int out = open(log_path && *log_path ? log_path : "/dev/null", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (out < 0) out = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (in >= 0) dup2(in, 0);
    if (out >= 0) {
        dup2(out, 1);
        dup2(out, 2);
    }
    if (in > 2) close(in);
    if (out > 2) close(out);
}

static bool start_listening(App *app, const PanelOptions *opts)
{
    const char *address = opts->listen_on;
    if (!address || !*address) address = strcmp(app->cfg.listen_on, "none") != 0 ? app->cfg.listen_on : NULL;
    if (!opts->single_instance && !address) return true;
    app->rc = rc_server_new(app);
    char err[512];
    if (opts->single_instance && !rc_server_listen_single_instance(app->rc, opts->instance_group, err, sizeof err))
        log_msg("single instance: %s", err);
    if (!address) return true;
    if (app->cfg.allow_remote_control == REMOTE_CONTROL_NO) {
        log_msg("ignoring --listen-on because allow_remote_control is not enabled, use -o allow_remote_control=socket-only");
        return true;
    }
    char *resolved = rc_resolve_address(address);
    if (!rc_server_listen(app->rc, resolved, err, sizeof err)) {
        log_msg("%s", err);
        free(resolved);
        return false;
    }
    app->listen_on = resolved;
    return true;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "@") == 0) return rc_client_main(argc - 2, argv + 2);

    PanelOptions opts;
    cli_defaults(&panel_options_table, &opts);
    char err[512] = {0};
    int first = cli_parse(&panel_options_table, &opts, argc - 1, argv + 1, NULL, err, sizeof err);
    if (first == CLI_HELP) {
        cli_print_help(&panel_options_table, "ghostty-panel", stdout);
        return 0;
    }
    if (first == CLI_ERROR) {
        fprintf(stderr, "ghostty-panel: %s\n", err);
        return 1;
    }
    log_set_debug(opts.debug_rendering, opts.debug_input);

    if (!dynload_init(err, sizeof err)) {
        fprintf(stderr, "ghostty-panel: %s\n", err);
        return 1;
    }
    if (opts.output_name && (strcmp(opts.output_name, "list") == 0 || strcmp(opts.output_name, "listjson") == 0))
        return app_list_outputs(strcmp(opts.output_name, "listjson") == 0);

    if (opts.single_instance) {
        int code = 0;
        if (single_instance_forward(opts.instance_group, argc - 1, argv + 1, opts.wait_for_single_instance_window_close, &code)) {
            cli_free(&panel_options_table, &opts);
            return code;
        }
    }
    if (opts.detach) detach(opts.detached_log);

    image_install_png_decoder();
    App app = {0};
    config_defaults(&app.cfg);
    config_load(&app.cfg, &opts.config, &opts.override);

    int code = 1;
    if (!app_connect(&app, err, sizeof err) || !app_init_egl(&app, err, sizeof err)) {
        log_msg("%s", err);
        goto out;
    }
    if (!start_listening(&app, &opts)) goto out;

    StrList items = {0};
    for (int i = 1 + first; i < argc; i++) strlist_push(&items, argv[i]);
    PanelSpec spec = {
        .lsc = layer_config_from_options(&opts),
        .app_id = opts.app_id,
        .argv = &items,
        .cwd = NULL,
        .env = NULL,
        .cfg = &app.cfg,
        .start_hidden = opts.start_as_hidden,
        .grab_keyboard = opts.grab_keyboard,
    };
    Panel *p = app_create_panel(&app, &spec, err, sizeof err);
    strlist_clear(&items);
    if (!p) {
        log_msg("%s", err);
        goto out;
    }
    code = app_run(&app);
out:
    app_disconnect(&app);
    cli_free(&panel_options_table, &opts);
    return code;
}
