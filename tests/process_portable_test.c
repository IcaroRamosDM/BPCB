#define _POSIX_C_SOURCE 200809L
#include "lab.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#else
#include <windows.h>
#endif
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "PROCESS FAIL %d: %s\n", __LINE__, #x);                                \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv) {
#ifdef _WIN32
    char **unicode = g_win32_get_command_line();
    argv = unicode;
#endif
    if (argc > 1 && !strcmp(argv[1], "--fixture")) {
#ifdef _WIN32
        BOOL inside = FALSE;
        if (!IsProcessInJob(GetCurrentProcess(), NULL, &inside) || !inside)
            return 9;
#endif
        if (getchar() != EOF)
            return 8;
        json_object *args = json_object_new_array();
        for (int i = 2; i < argc; i++)
            json_object_array_add(args, json_object_new_string(argv[i]));
        puts(json_object_to_json_string_ext(args, JSON_C_TO_STRING_PLAIN));
        json_object_put(args);
#ifdef _WIN32
        g_strfreev(unicode);
#endif
        return 0;
    }
    bc_error e = {{0}};
    json_object *args = json_object_new_array();
    char *found = g_find_program_in_path(argv[0]);
    CHECK(found);
    char *executable = g_canonicalize_filename(found, NULL);
    g_free(found);
    json_object_array_add(args, json_object_new_string(executable));
    g_free(executable);
    json_object_array_add(args, json_object_new_string("--fixture"));
    const char *values[] = {"com espaço e ação",          "\"aspas\"", "C:\\pasta\\",
                            "$(exec false); [exec nope]", "",          "barra\\\"aspas"};
    for (unsigned i = 0; i < sizeof values / sizeof *values; i++)
        json_object_array_add(args, json_object_new_string(values[i]));
    json_object *r = bc_external_process(args, 3, NULL, NULL, &e);
    if (!r)
        fprintf(stderr, "%s\n", e.text);
    CHECK(r && json_object_get_boolean(bc_get(r, "ok")));
    json_object *received = json_tokener_parse(bc_str(r, "log"));
    CHECK(received && json_object_array_length(received) == sizeof values / sizeof *values);
    for (unsigned i = 0; i < sizeof values / sizeof *values; i++)
        CHECK(!strcmp(json_object_get_string(json_object_array_get_idx(received, i)), values[i]));
    json_object_put(received);
    json_object_put(r);
    json_object_put(args);
    args = json_object_new_array();
    json_object_array_add(args, json_object_new_int(1));
    CHECK(!bc_external_process(args, 1, NULL, NULL, &e));
    json_object_put(args);
    CHECK(!bc_external_process(NULL, 1, NULL, NULL, &e));
    printf("%u verificações de processo: argumentos literais, Unicode, stdin fechado e isolamento "
           "do processo.\n",
           checks);
#ifdef _WIN32
    g_strfreev(unicode);
#endif
    return 0;
}
