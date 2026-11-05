#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "communication.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "TERMINAL FAIL %d: %s (%s)\n", __LINE__, #x, e.text);                  \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void pause_(void) {
    struct timespec t = {0, 10000000};
    nanosleep(&t, NULL);
}

int main(void) {
    bc_error e = {{0}};
    char root[] = "/tmp/bpcb-terminal-test-XXXXXX";
    CHECK(mkdtemp(root));
    CHECK(bc_boot(root, "", &e) == 0);
    for (unsigned scenario = 0; scenario < 4; scenario++) {
        int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
        char *port = ptsname(master);
        CHECK(port);
        json_object *config = json_object_new_object();
        json_object_object_add(config, "adapter", json_object_new_string("serial"));
        json_object_object_add(config, "bus", json_object_new_string("uart"));
        json_object_object_add(config, "device", json_object_new_string(port));
        json_object_object_add(config, "connections_checked", json_object_new_boolean(1));
        json_object_object_add(config, "timeout_ms", json_object_new_int(500));
        json_object *v = json_object_new_object(),
                    *op = json_tokener_parse("{\"delimiter\":\"crlf\",\"seconds\":1}");
        json_object_object_add(v, "communication", config);
        json_object_object_add(v, "operation", op);
        bc_job *j = bc_job_start("communication_terminal", root, "", "", "", v);
        json_object_put(v);
        CHECK(j);
        double start = now();
        while (now() - start < 0.12) {
            CHECK(!bc_job_finished(j));
            pause_();
        }
        unsigned rx = 0, tx = 0;
        int finished = 0, status = -1;
        char got[8] = {0};
        size_t used = 0;
        if (!scenario) {
            json_object *cmd = bc_comm_text_operation("AT", 0, "crlf", 0, &e);
            CHECK(cmd && bc_job_command(j, cmd) == 0);
            json_object_put(cmd);
            while (used < 4 && now() - start < 0.5) {
                ssize_t n = read(master, got + used, 4 - used);
                if (n > 0)
                    used += (size_t)n;
                pause_();
            }
            CHECK(used == 4 && !memcmp(got, "AT\r\n", 4));
            CHECK(write(master, "OK\r", 3) == 3);
            for (unsigned n = 0; n < 3; n++)
                pause_();
            CHECK(write(master, "\nNEXT\r\n", 7) == 7);
        } else if (scenario == 1) {
            CHECK(write(master, "partial", 7) == 7);
        } else if (scenario == 2)
            bc_job_cancel(j);
        else {
            close(master);
            master = -1;
        }
        while (!finished && now() - start < 3) {
            json_object *ev;
            while ((ev = bc_job_next(j))) {
                const char *type = bc_str(ev, "type");
                if (!strcmp(type, "communication_terminal_data")) {
                    CHECK(bc_num(ev, "elapsed") >= 0 && bc_num(ev, "elapsed") < 3);
                    if (!strcmp(bc_str(ev, "direction"), "tx"))
                        tx++;
                    else {
                        rx++;
                        if (!scenario) {
                            CHECK(json_object_get_boolean(bc_get(ev, "complete")));
                            CHECK(
                                !strcmp(bc_str(ev, "ascii"), rx == 1 ? "OK\\r\\n" : "NEXT\\r\\n"));
                        } else if (scenario == 1)
                            CHECK(!json_object_get_boolean(bc_get(ev, "complete")) &&
                                  !strcmp(bc_str(ev, "ascii"), "partial"));
                    }
                }
                if (!strcmp(type, "finished")) {
                    finished = 1;
                    status = !strcmp(bc_str(ev, "status"), "ok");
                }
                json_object_put(ev);
            }
            pause_();
        }
        CHECK(finished);
        if (!scenario)
            CHECK(rx == 2 && tx == 1 && status == 1);
        if (scenario == 1)
            CHECK(rx == 1 && status == 1);
        if (scenario >= 2)
            CHECK(status == 0 && now() - start < 1);
        bc_job_free(j);
        if (master >= 0)
            close(master);
    }
    char path[4096];
    CHECK(bc_path(path, sizeof path, root, "results", &e) == 0);
    CHECK(access(path, F_OK) < 0);
    CHECK(bc_path(path, sizeof path, root, "communication-profiles.json", &e) == 0);
    CHECK(access(path, F_OK) < 0);
    printf("%u verificações terminal com PTY real: TX/RX, CRLF fragmentado, idle, fragmento final, "
           "cancelamento e desconexão; sem hardware\n",
           checks);
    return 0;
}
