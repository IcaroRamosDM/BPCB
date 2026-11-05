#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void stamp(json_object *o, const char *key) {
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    char s[32];
    strftime(s, sizeof s, "%Y-%m-%dT%H:%M:%SZ", &utc);
    json_object_object_add(o, key, json_object_new_string(s));
}

static int location(const char *root, const char *id, char path[4096], bc_error *e) {
    if (strlen(id) != 10 || strncmp(id, "run-", 4))
        return bc_fail(e, "Identificador de execução inválido.");
    for (const char *p = id + 4; *p; p++)
        if (!isalnum((unsigned char)*p))
            return bc_fail(e, "Identificador de execução inválido.");
    char dir[4096];
    return bc_path(dir, sizeof dir, root, "results", e) || bc_path(path, 4096, dir, id, e) ? -1 : 0;
}

json_object *bc_record_new(const char *board, const char *test, json_object *identity,
                           bc_error *e) {
    json_object *o = json_object_new_object();
    json_object_object_add(o, "format", json_object_new_string("bpcb.result"));
    json_object_object_add(o, "schema", json_object_new_int(1));
    json_object_object_add(o, "id", json_object_new_string("memory"));
    json_object_object_add(o, "bpcb_version", json_object_new_string(BC_VERSION));
    json_object_object_add(o, "board_id", json_object_new_string(board));
    json_object_object_add(o, "test_id", json_object_new_string(test));
    json_object_object_add(o, "status", json_object_new_string("incomplete"));
    json_object *who = json_object_new_object();
    const char *fields[] = {"physical_board", "device", "operator"};
    for (size_t i = 0; i < 3; i++) {
        const char *value = bc_str(identity, fields[i]);
        if (strlen(value) > 256) {
            bc_fail(e, "Identificação do ensaio muito longa.");
            json_object_put(who);
            json_object_put(o);
            return NULL;
        }
        json_object_object_add(who, fields[i], json_object_new_string(value));
    }
    json_object_object_add(o, "identity", who);
    json_object_object_add(o, "events", json_object_new_array());
    stamp(o, "started_at");
    return o;
}

json_object *bc_history_begin(const char *root, const char *board, const char *test,
                              json_object *identity, bc_error *e) {
    json_object *o = bc_record_new(board, test, identity, e);
    if (!o)
        return NULL;
    char dir[4096], path[4096];
    if (bc_path(dir, sizeof dir, root, "results", e) || bc_mkdirs(dir, e) ||
        bc_path(path, sizeof path, dir, "run-XXXXXX", e)) {
        json_object_put(o);
        return NULL;
    }
    int fd = mkstemp(path);
    if (fd < 0) {
        bc_fail(e, "Criar registro: %s", strerror(errno));
        json_object_put(o);
        return NULL;
    }
    close(fd);
    json_object_object_add(o, "id", json_object_new_string(strrchr(path, '/') + 1));
    if (bc_write_json(path, o, e)) {
        json_object_put(o);
        unlink(path);
        return NULL;
    }
    return o;
}

/* Called only by the execution thread. No disk I/O while hardware is active. */
void bc_history_event(json_object *record, json_object *event) {
    if (!record || !strcmp(bc_str(event, "type"), "manual_state") ||
        !strcmp(bc_str(event, "type"), "sample"))
        return;
    size_t bytes = strlen(json_object_to_json_string_ext(event, JSON_C_TO_STRING_PLAIN));
    if (bc_num(record, "recorded_bytes") + (double)bytes > 3 * 1024 * 1024) {
        json_object_object_add(record, "events_truncated", json_object_new_boolean(1));
        return;
    }
    json_object_object_add(
        record, "recorded_bytes",
        json_object_new_double(bc_num(record, "recorded_bytes") + (double)bytes));
    if (!strcmp(bc_str(event, "type"), "snapshot"))
        json_object_object_add(record, "snapshot", bc_clone(bc_get(event, "data")));
    json_object *events = bc_get(record, "events");
    size_t count = json_object_array_length(events);
    if (count >= 2048) {
        json_object_object_add(record, "events_truncated", json_object_new_boolean(1));
        return;
    }
    json_object *copy = bc_clone(event);
    stamp(copy, "recorded_at");
    json_object_array_add(events, copy);
}

void bc_record_finish(json_object *record, json_object *end) {
    bc_history_event(record, end);
    stamp(record, "finished_at");
    json_object_object_add(record, "status", json_object_new_string(bc_str(end, "status")));
    json_object_object_add(record, "message", json_object_new_string(bc_str(end, "message")));
}

int bc_history_finish(const char *root, json_object *record, json_object *end, bc_error *e) {
    char path[4096];
    if (location(root, bc_str(record, "id"), path, e))
        return -1;
    bc_record_finish(record, end);
    if (strlen(json_object_to_json_string_ext(record, JSON_C_TO_STRING_PRETTY)) > 8 * 1024 * 1024)
        return bc_fail(
            e, "Registro grande demais; a execução ficou marcada como incompleta no histórico.");
    return bc_write_json(path, record, e);
}

json_object *bc_history_read(const char *root, const char *id, bc_error *e) {
    char path[4096];
    if (location(root, id, path, e))
        return NULL;
    json_object *o = bc_read_json(path, e);
    if (o && (strcmp(bc_str(o, "format"), "bpcb.result") || bc_num(o, "schema") != 1 ||
              strcmp(bc_str(o, "id"), id) ||
              !json_object_is_type(bc_get(o, "events"), json_type_array))) {
        json_object_put(o);
        bc_fail(e, "Registro de execução inválido.");
        return NULL;
    }
    return o;
}

static int newest(const void *a, const void *b) {
    json_object *x = *(json_object *const *)a, *y = *(json_object *const *)b;
    int n = strcmp(bc_str(y, "started_at"), bc_str(x, "started_at"));
    return n ? n : strcmp(bc_str(x, "id"), bc_str(y, "id"));
}

json_object *bc_history_list(const char *root, bc_error *e) {
    char dir[4096];
    if (bc_path(dir, sizeof dir, root, "results", e))
        return NULL;
    json_object *result = json_object_new_object(), *items = json_object_new_array();
    json_object_object_add(result, "items", items);
    int skipped = 0, truncated = 0;
    DIR *d = opendir(dir);
    if (!d) {
        if (errno == ENOENT)
            return result;
        bc_fail(e, "Abrir histórico: %s", strerror(errno));
        json_object_put(result);
        return NULL;
    }
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (strncmp(entry->d_name, "run-", 4) || strlen(entry->d_name) != 10)
            continue;
        if (json_object_array_length(items) >= 20000) {
            truncated = 1;
            break;
        }
        bc_error local = {{0}};
        json_object *o = bc_history_read(root, entry->d_name, &local);
        if (!o) {
            skipped++;
            continue;
        }
        json_object_object_del(o, "events");
        json_object_object_del(o, "snapshot");
        json_object_array_add(items, o);
    }
    closedir(d);
    json_object_array_sort(items, newest);
    json_object_object_add(result, "skipped", json_object_new_int(skipped));
    json_object_object_add(result, "truncated", json_object_new_boolean(truncated));
    return result;
}

int bc_history_delete(const char *root, const char *id, bc_error *e) {
    char path[4096];
    if (location(root, id, path, e))
        return -1;
    return unlink(path) ? bc_fail(e, "Excluir registro: %s", strerror(errno)) : 0;
}

static void csv(FILE *f, const char *s) {
    fputc('"', f);
    /* Do not let text labels become formulas when opened in a spreadsheet. */
    const char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p == '=' || *p == '+' || *p == '-' || *p == '@')
        fputc('\'', f);
    for (; *s; s++) {
        if (*s == '"')
            fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

int bc_history_export(const char *root, const char *id, const char *destination, bc_error *e) {
    json_object *record = bc_history_read(root, id, e);
    if (!record)
        return -1;
    int fd = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        json_object_put(record);
        return bc_fail(e, "Criar CSV (escolha um nome novo): %s", strerror(errno));
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        unlink(destination);
        json_object_put(record);
        return bc_fail(e, "Abrir CSV.");
    }
    fputs("\xEF\xBB\xBF"
          "execucao,inicio_utc,estado,placa,pacote_versao,teste,placa_fisica,dispositivo,operador,"
          "sinal,valor,unidade,comparacao,criterio,origem,instante\r\n",
          f);
    json_object *who = bc_get(record, "identity"), *snapshot = bc_get(record, "snapshot"),
                *events = bc_get(record, "events");
    int rows = 0;
    for (size_t i = 0; i <= json_object_array_length(events); i++) {
        json_object *ev =
            i < json_object_array_length(events) ? json_object_array_get_idx(events, i) : NULL;
        if (ev && strcmp(bc_str(ev, "type"), "measurement"))
            continue;
        if (!ev && rows)
            break;
        const char *fields[] = {id,
                                bc_str(record, "started_at"),
                                bc_str(record, "status"),
                                bc_str(record, "board_id"),
                                bc_str(snapshot, "version"),
                                bc_str(record, "test_id"),
                                bc_str(who, "physical_board"),
                                bc_str(who, "device"),
                                bc_str(who, "operator"),
                                bc_str(ev, "label")};
        for (size_t k = 0; k < 10; k++) {
            if (k)
                fputc(',', f);
            csv(f, fields[k]);
        }
        fputc(',', f);
        if (ev)
            fprintf(f, "%.17g", bc_num(ev, "value"));
        const char *tail[] = {bc_str(ev, "unit"), bc_str(ev, "verdict"), bc_str(ev, "criterion"),
                              bc_str(ev, "source"), bc_str(ev, "timestamp")};
        for (size_t k = 0; k < 5; k++) {
            fputc(',', f);
            csv(f, tail[k]);
        }
        fputs("\r\n", f);
        rows++;
    }
    int failed = ferror(f);
    if (fflush(f) || fsync(fd))
        failed = 1;
    if (fclose(f))
        failed = 1;
    json_object_put(record);
    if (failed) {
        unlink(destination);
        return bc_fail(e, "Falha ao gravar CSV.");
    }
    return 0;
}
