#pragma once
#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/prctl.h>
#endif
#include <stdatomic.h>
#include <unistd.h>
static _Atomic int worker_stopped;

static void worker_signal(int sig) {
    (void)sig;
    worker_stopped = 1;
}

static void worker_event(const char *type, const char *text) {
    json_object *o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string(type));
    json_object_object_add(o, "message", json_object_new_string(text));
    puts(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN));
    fflush(stdout);
    json_object_put(o);
}

static json_object *worker_begin(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    if (bc_win_watch_parent(worker_signal) < 0)
        return NULL;
#else
    pid_t parent = getppid();
    signal(SIGTERM, worker_signal);
    signal(SIGINT, worker_signal);
    signal(SIGPIPE, SIG_IGN);
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0 || getppid() != parent)
        return NULL;
#endif
    char *line = calloc(1024 * 1024, 1);
    if (!line)
        return NULL;
    if (!fgets(line, 1024 * 1024, stdin)) {
        free(line);
        return NULL;
    }
    json_object *o = json_tokener_parse(line);
    free(line);
    if (!o || bc_num(o, "api") != BC_API) {
        if (o)
            json_object_put(o);
        return NULL;
    }
    return o;
}

static void worker_measure(const char *label, double value, const char *unit, const char *source) {
    json_object *o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string("measurement"));
    json_object_object_add(o, "label", json_object_new_string(label));
    json_object_object_add(o, "value", json_object_new_double(value));
    json_object_object_add(o, "unit", json_object_new_string(unit));
    json_object_object_add(o, "source", json_object_new_string(source));
    puts(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN));
    fflush(stdout);
    json_object_put(o);
}
