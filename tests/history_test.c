#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static json_object *run(const char *root, const char *test, json_object *values, int cancel) {
    bc_job *job = bc_job_start("run", root, "demonstracao-v1", test, "", values);
    assert(job);
    json_object *end = NULL;
    if (cancel)
        bc_job_cancel(job);
    do {
        json_object *ev;
        while ((ev = bc_job_next(job))) {
            if (!strcmp(bc_str(ev, "type"), "finished")) {
                if (end)
                    json_object_put(end);
                end = bc_clone(ev);
            }
            json_object_put(ev);
        }
        if (end)
            break;
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    } while (1);
    bc_job_free(job);
    return end;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char root[] = "/tmp/bpcb-history-XXXXXX";
    assert(mkdtemp(root));
    bc_error e = {{0}};
    char package[4096], path[4096];
    assert(!bc_path(package, sizeof package, argv[1], "Demonstracao-V1.bancada", &e));
    assert(!bc_install(root, package, "", &e));
    json_object *v = json_object_new_object(), *end = run(root, "corrente", v, 0);
    assert(!strcmp(bc_str(end, "status"), "ok"));
    assert(!*bc_str(end, "history_id"));
    json_object_put(end);
    assert(!bc_path(path, sizeof path, root, "results", &e));
    assert(access(path, F_OK) < 0);
    json_object *settings = json_tokener_parse("{\"enabled\":\"false\"}");
    json_object_object_add(v, "history", settings);
    end = run(root, "corrente", v, 0);
    assert(!*bc_str(end, "history_id"));
    json_object_put(end);
    assert(access(path, F_OK) < 0);
    settings = json_tokener_parse("{\"enabled\":true,\"physical_board\":\"PCB-1\",\"device\":\"="
                                  "SUM(1,2)\",\"operator\":\"Ícaro\"}");
    json_object_object_add(v, "history", settings);
    end = run(root, "corrente", v, 0);
    assert(!strcmp(bc_str(end, "status"), "ok"));
    char id[64];
    snprintf(id, sizeof id, "%s", bc_str(end, "history_id"));
    assert(*id);
    json_object_put(end);
    json_object *record = bc_history_read(root, id, &e);
    assert(record);
    assert(!strcmp(bc_str(record, "status"), "ok"));
    assert(bc_get(record, "snapshot"));
    assert(json_object_array_length(bc_get(record, "events")) > 2);
    json_object_put(record);
    assert(!bc_path(path, sizeof path, root, "results.csv", &e));
    assert(!bc_history_export(root, id, path, &e));
    assert(bc_history_export(root, id, path, &e) < 0);
    e.text[0] = 0;
    FILE *f = fopen(path, "r");
    assert(f);
    char text[8192];
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = 0;
    fclose(f);
    assert(strstr(text, "'=SUM(1,2)"));
    assert(strstr(text, "Simulação"));
    end = run(root, "missing", v, 0);
    assert(!strcmp(bc_str(end, "status"), "error"));
    assert(*bc_str(end, "history_id"));
    json_object_put(end);
    end = run(root, "corrente", v, 1);
    assert(!strcmp(bc_str(end, "status"), "cancelled"));
    assert(*bc_str(end, "history_id"));
    json_object_put(end);
    record = bc_history_begin(root, "test", "unfinished", settings, &e);
    assert(record);
    char incomplete[64];
    snprintf(incomplete, sizeof incomplete, "%s", bc_str(record, "id"));
    json_object_put(record);
    record = bc_history_read(root, incomplete, &e);
    assert(!strcmp(bc_str(record, "status"), "incomplete"));
    json_object_put(record);
    assert(!bc_history_read(root, "../../ui.json", &e));
    e.text[0] = 0;
    assert(bc_history_delete(root, "../ui.json", &e) < 0);
    e.text[0] = 0;
    json_object *list = bc_history_list(root, &e);
    assert(list && json_object_array_length(bc_get(list, "items")) == 4);
    json_object_put(list);
    assert(!bc_history_delete(root, id, &e));
    assert(!bc_history_read(root, id, &e));
    e.text[0] = 0;
    json_object_object_add(settings, "enabled", json_object_new_boolean(0));
    end = run(root, "corrente", v, 0);
    assert(!*bc_str(end, "history_id"));
    json_object_put(end);
    list = bc_history_list(root, &e);
    assert(list && json_object_array_length(bc_get(list, "items")) == 3);
    json_object_put(list);
    json_object_put(v);
    puts("Histórico: opt-in, persistência, falha, cancelamento, execução incompleta, CSV, caminhos "
         "e exclusão OK.");
    return 0;
}
