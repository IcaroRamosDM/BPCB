#define _POSIX_C_SOURCE 200809L
#include "../workbench.h"
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/file.h>
#endif
static void text(json_object *o, const char *k, const char *v) {
    json_object_object_add(o, k, json_object_new_string(v));
}

static int label(const char *s, size_t maximum) {
    size_t n = s ? strlen(s) : 0;
    if (!n || n > maximum || !bc_utf8((const unsigned char *)s, n))
        return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32 || s[i] == 127)
            return 0;
    return 1;
}

static char *identity(json_object *config, const char *os, const char *driver,
                      const char *procedure) {
    json_object *c = bc_clone(config);
    json_object_object_add(c, "connections_checked", json_object_new_boolean(0));
    json_object_object_add(c, "allow_kernel_detach", json_object_new_boolean(0));
    text(c, "registry_os", os);
    text(c, "registry_driver", driver);
    text(c, "registry_procedure", procedure);
    const char *s = json_object_to_json_string_ext(c, JSON_C_TO_STRING_PLAIN);
    char *d = g_compute_checksum_for_string(G_CHECKSUM_SHA256, s, -1);
    json_object_put(c);
    return d;
}

json_object *bc_compatibility_record(json_object *report, const char *name, const char *os,
                                     const char *driver, bc_error *e) {
    if (!label(name, 120) || !label(os, 120) || !label(driver, 120))
        return bc_fail(e, "Informe modelo/revisão, sistema e versão do driver (1…120 bytes cada)."),
               NULL;
    json_object *r = bc_qualification_assess(report, bc_get(report, "answers"), e);
    if (!r)
        return NULL;
    json_object *record = json_object_new_object();
    json_object_object_add(record, "schema", json_object_new_int(1));
    text(record, "name", name);
    text(record, "os", os);
    text(record, "driver", driver);
    text(record, "app_version", BC_VERSION);
    text(record, "source", "operator-recorded");
    json_object_object_add(record, "report", r);
    text(record, "status",
         !strcmp(bc_str(r, "status"), "pass")   ? "validated"
         : !strcmp(bc_str(r, "status"), "fail") ? "failed"
                                                : "partial");
    char *d = identity(bc_get(r, "configuration"), os, driver, bc_str(r, "procedure"));
    text(record, "id", d);
    g_free(d);
    json_object_object_add(record, "recorded_at", json_object_new_int64((int64_t)time(NULL)));
    return record;
}

static int valid_record(json_object *r, bc_error *e) {
    if (bc_lab_object(
            r, "|schema||name||os||driver||app_version||source||report||status||id||recorded_at|",
            e) < 0 ||
        !json_object_is_type(bc_get(r, "schema"), json_type_int) || bc_num(r, "schema") != 1 ||
        strcmp(bc_str(r, "source"), "operator-recorded") || !label(bc_str(r, "app_version"), 24) ||
        !json_object_is_type(bc_get(r, "recorded_at"), json_type_int) ||
        bc_num(r, "recorded_at") < 0)
        return bc_fail(e, "Registro de compatibilidade inválido.");
    json_object *canonical = bc_compatibility_record(bc_get(r, "report"), bc_str(r, "name"),
                                                     bc_str(r, "os"), bc_str(r, "driver"), e);
    if (!canonical)
        return -1;
    int valid = !strcmp(bc_str(r, "id"), bc_str(canonical, "id")) &&
                !strcmp(bc_str(r, "status"), bc_str(canonical, "status")) &&
                json_object_equal(bc_get(r, "report"), bc_get(canonical, "report"));
    json_object_put(canonical);
    return valid ? 0 : bc_fail(e, "Avaliação/identidade do registro diverge das evidências.");
}

int bc_compatibility_validate(json_object *a, bc_error *e) {
    if (!json_object_is_type(a, json_type_array) || json_object_array_length(a) > 256)
        return bc_fail(e, "Use até 256 registros de compatibilidade.");
    for (size_t i = 0; i < json_object_array_length(a); i++) {
        json_object *r = json_object_array_get_idx(a, i);
        if (valid_record(r, e) < 0)
            return -1;
        for (size_t j = 0; j < i; j++)
            if (!strcmp(bc_str(r, "id"), bc_str(json_object_array_get_idx(a, j), "id")))
                return bc_fail(e, "Registro repetido.");
    }
    return 0;
}

json_object *bc_compatibility_list(const char *root, bc_error *e) {
    char path[4096];
    if (bc_path(path, sizeof path, root, "compatibility.json", e) < 0)
        return NULL;
    struct stat st;
    if (lstat(path, &st) < 0 && errno == ENOENT)
        return json_object_new_array();
    json_object *a = bc_read_json(path, e);
    if (!a)
        return NULL;
    if (bc_compatibility_validate(a, e) < 0) {
        json_object_put(a);
        return NULL;
    }
    return a;
}

int bc_compatibility_save(const char *root, json_object *r, int replace, bc_error *e) {
    if (valid_record(r, e) < 0)
        return -1;
    char path[4096], lockpath[4096];
    if (bc_path(path, sizeof path, root, "compatibility.json", e) < 0 ||
        bc_path(lockpath, sizeof lockpath, root, "storage.lock", e) < 0)
        return -1;
    int lock = open(lockpath, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600), rc = -1;
    json_object *a = NULL;
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
        bc_fail(e, "Armazenamento ocupado ou indisponível.");
        goto done;
    }
    a = bc_compatibility_list(root, e);
    if (!a)
        goto done;
    size_t at = json_object_array_length(a);
    for (size_t i = 0; i < json_object_array_length(a); i++)
        if (!strcmp(bc_str(r, "id"), bc_str(json_object_array_get_idx(a, i), "id")))
            at = i;
    if (at < json_object_array_length(a) && !replace) {
        bc_fail(e, "Já existe registro desta configuração/sistema/driver/roteiro. Confirme a "
                   "substituição.");
        goto done;
    }
    if (at >= 256) {
        bc_fail(e, "Limite de registros atingido.");
        goto done;
    }
    json_object_array_put_idx(a, at, bc_clone(r));
    rc = bc_write_json(path, a, e);
done:
    json_object_put(a);
    if (lock >= 0)
        close(lock);
    return rc;
}

json_object *bc_compatibility_match(json_object *a, json_object *config, const char *os,
                                    const char *driver, bc_error *e) {
    if (bc_compatibility_validate(a, e) < 0)
        return NULL;
    json_object *c = bc_comm_config(config, e);
    if (!c)
        return NULL;
    json_object *r = json_object_new_object(), *matches = json_object_new_array();
    text(r, "status", "untested");
    text(r, "scope",
         "Compatibilidade registrada pelo operador para a seleção, sistema, driver e roteiro. Não "
         "concede autorização nem certifica outros alvos.");
    json_object_object_add(r, "records", matches);
    for (size_t i = 0; i < json_object_array_length(a); i++) {
        json_object *record = json_object_array_get_idx(a, i);
        char *d = identity(c, os, driver, bc_str(bc_get(record, "report"), "procedure"));
        int match = !strcmp(d, bc_str(record, "id"));
        g_free(d);
        if (match)
            json_object_array_add(matches, bc_clone(record));
    }
    if (json_object_array_length(matches)) {
        const char *status = "partial";
        int pass = 0, fail = 0, partial = 0;
        for (size_t i = 0; i < json_object_array_length(matches); i++) {
            const char *s = bc_str(json_object_array_get_idx(matches, i), "status");
            partial |=
                !strcmp(s, "partial") ||
                strcmp(bc_str(json_object_array_get_idx(matches, i), "app_version"), BC_VERSION);
            pass |= !strcmp(s, "validated");
            fail |= !strcmp(s, "failed");
        }
        status = fail ? "failed" : pass && !partial ? "validated" : "partial";
        text(r, "status", status);
    }
    json_object_put(c);
    return r;
}
