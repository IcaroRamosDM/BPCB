/* Test-only BPCB host: answers simulated prompts, or drives an explicitly selected PTY.
 * Not included in release packages. Physical serial/USB configurations are refused. */
#define _POSIX_C_SOURCE 200809L
#include "communication.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5)
        return 2;
    bc_error e = {{0}};
    char folder[4096];
    json_object *m = bc_profile(argv[1], argv[2], folder, &e);
    if (!m)
        return 2;
    json_object *test = bc_test(m, argv[3]), *v = json_object_new_object();
    if (!test) {
        json_object_put(v);
        json_object_put(m);
        return 2;
    }
    if (argc == 5) {
        json_object *raw = bc_read_json(argv[4], &e), *c = NULL;
        if (raw && bc_num(raw, "api") == 2)
            c = bc_comm_resolve(m, test, raw, &e);
        else if (raw)
            c = bc_comm_config(raw, &e);
        json_object_put(raw);
        int safe = c != NULL;
        if (c && bc_num(c, "api") == 2) {
            json_object_object_foreach(bc_get(c, "channels"), id, item) {
                (void)id;
                if (strcmp(bc_str(item, "adapter"), "serial") ||
                    strcmp(bc_str(item, "bus"), "uart") ||
                    strncmp(bc_str(item, "device"), "/dev/pts/", 9))
                    safe = 0;
            }
        } else if (c &&
                   (strcmp(bc_str(c, "adapter"), "serial") || strcmp(bc_str(c, "bus"), "uart") ||
                    strncmp(bc_str(c, "device"), "/dev/pts/", 9)))
            safe = 0;
        if (!safe) {
            json_object_put(c);
            json_object_put(v);
            json_object_put(m);
            return 2;
        }
        json_object_object_add(v, "communication", c);
    } else if (strcmp(bc_str(test, "mode"), "simulation")) {
        json_object_put(v);
        json_object_put(m);
        return 2;
    }
    json_object_put(m);
    bc_job *j = bc_job_start("run", argv[1], argv[2], argv[3], "", v);
    json_object_put(v);
    if (!j)
        return 2;
    int result = 1, finished = 0;
    while (!finished) {
        json_object *ev;
        while ((ev = bc_job_next(j))) {
            puts(json_object_to_json_string_ext(ev, JSON_C_TO_STRING_PLAIN));
            fflush(stdout);
            const char *type = bc_str(ev, "type");
            if (!strcmp(type, "confirm")) {
                if (json_object_get_boolean(bc_get(ev, "number")))
                    bc_job_reply_value(j, bc_str(ev, "id"), bc_get(ev, "sample") != NULL,
                                       bc_num(ev, "sample"));
                else
                    bc_job_reply(j, bc_str(ev, "id"), 1);
            }
            if (!strcmp(type, "finished")) {
                result = strcmp(bc_str(ev, "status"), "ok") ? 1 : 0;
                finished = 1;
            }
            json_object_put(ev);
        }
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    bc_job_free(j);
    return result;
}
