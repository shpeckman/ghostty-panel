// src/rc.h
#pragma once

#include <stdbool.h>

#include "app.h"

RcServer *rc_server_new(App *app);
void rc_server_free(RcServer *s);
bool rc_server_listen(RcServer *s, const char *address, bool same_user, char *err, size_t errlen);
bool rc_server_listen_single_instance(RcServer *s, const char *group, char *err, size_t errlen);
void rc_server_collect_watches(RcServer *s);

char *rc_resolve_address(const char *address);
char *rc_service_address(void);
bool single_instance_forward(const char *group, int argc, char **argv, bool wait, int *exit_code);
int rc_client_main(int argc, char **argv);
