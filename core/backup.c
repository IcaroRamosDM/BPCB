#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include "communication.h"
#include "workbench.h"
#include <archive.h>
#include <archive_entry.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/file.h>
#endif
#include <unistd.h>
#include <dirent.h>
#include <glib.h>

#define BACKUP_LIMIT (128u * 1024u * 1024u)

static void digest(const char *bytes, size_t length, char hex[65]) {
    unsigned char hash[32];
    unsigned n = 0;
    EVP_Digest(bytes, length, hash, &n, EVP_sha256(), NULL);
    for (unsigned i = 0; i < 32; i++)
        sprintf(hex + 2 * i, "%02x", hash[i]);
    hex[64] = 0;
}

static void json_digest(json_object *value, char hex[65]) {
    const char *s = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    digest(s, strlen(s), hex);
}

static int id_ok(const char *s) {
    if (!*s || strlen(s) > 64)
        return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_')
            return 0;
    return 1;
}

static int entry_ok(const char *name, json_object *o) {
    if (!bc_safe_path(name))
        return 0;
    char expected[256];
    if (!strcmp(name, "compatibility.json")) {
        bc_error e = {{0}};
        return bc_compatibility_validate(o, &e) == 0;
    }
    if (!strcmp(name, "communication-profiles.json")) {
        bc_error e = {{0}};
        return bc_comm_profiles_validate(o, &e) == 0;
    }
    if (!strcmp(name, "ui.json"))
        return json_object_is_type(bc_get(o, "dark"), json_type_boolean);
    if (!strncmp(name, "profiles/", 9)) {
        if (!id_ok(bc_str(o, "board_id")) ||
            !json_object_is_type(bc_get(o, "values"), json_type_object))
            return 0;
        snprintf(expected, sizeof expected, "profiles/%s.json", bc_str(o, "board_id"));
        return !strcmp(name, expected);
    }
    if (!strncmp(name, "calibration/", 12)) {
        bc_error e = {{0}};
        if (!o || bc_cal_validate(o, &e))
            return 0;
        snprintf(expected, sizeof expected, "calibration/%s/%s.json", bc_str(o, "board_id"),
                 bc_str(o, "physical_id"));
        return !strcmp(name, expected);
    }
    if (!strncmp(name, "results/", 8)) {
        const char *id = bc_str(o, "id");
        if (strlen(id) != 10 || strncmp(id, "run-", 4))
            return 0;
        for (const char *p = id + 4; *p; p++)
            if (!isalnum((unsigned char)*p))
                return 0;
        snprintf(expected, sizeof expected, "results/%s", id);
        return !strcmp(name, expected) && !strcmp(bc_str(o, "format"), "bpcb.result") &&
               bc_num(o, "schema") == 1 &&
               json_object_is_type(bc_get(o, "events"), json_type_array);
    }
    return 0;
}

static int add_file(json_object *files, const char *name, json_object *value, bc_error *e) {
    if (!value)
        return -1;
    if (!entry_ok(name, value)) {
        json_object_put(value);
        return bc_fail(e, "Conteúdo inválido no backup: %s.", name);
    }
    json_object_object_add(files, name, value);
    return 0;
}

static json_object *collect(const char *root, int history, bc_error *e) {
    char file[4096];
    json_object *files = json_object_new_object(), *index = NULL;
    if (bc_path(file, sizeof file, root, "active.json", e))
        goto fail;
    struct stat active_stat;
    index = lstat(file, &active_stat) < 0 && errno == ENOENT ? json_object_new_object()
                                                             : bc_read_json(file, e);
    if (!index)
        goto fail;
    json_object_object_foreach(index, board, key) {
        char folder[4096], name[256], relative[512];
        json_object *profile = bc_profile(root, board, folder, e);
        if (!profile)
            goto fail;
        snprintf(relative, sizeof relative, "state/%s.json", json_object_get_string(key));
        if (bc_path(file, sizeof file, root, relative, e)) {
            json_object_put(profile);
            goto fail;
        }
        json_object *values =
            access(file, F_OK) == 0 ? bc_read_json(file, e) : json_object_new_object();
        if (!values) {
            json_object_put(profile);
            goto fail;
        }
        json_object *item = json_object_new_object();
        json_object_object_add(item, "board_id", json_object_new_string(board));
        json_object_object_add(item, "revision",
                               json_object_new_string(bc_str(profile, "revision")));
        json_object_object_add(item, "package_version",
                               json_object_new_string(bc_str(profile, "version")));
        json_object_object_add(item, "values", values);
        json_object_put(profile);
        snprintf(name, sizeof name, "profiles/%s.json", board);
        if (add_file(files, name, item, e))
            goto fail;
        json_object *cals = bc_cal_list(root, board, e);
        if (!cals)
            goto fail;
        for (size_t i = 0; i < json_object_array_length(cals); i++) {
            json_object *cal = json_object_array_get_idx(cals, i);
            snprintf(name, sizeof name, "calibration/%s/%s.json", board,
                     bc_str(cal, "physical_id"));
            if (add_file(files, name, bc_clone(cal), e)) {
                json_object_put(cals);
                goto fail;
            }
        }
        json_object_put(cals);
    }
    if (bc_path(file, sizeof file, root, "ui.json", e))
        goto fail;
    if (access(file, F_OK) == 0 && add_file(files, "ui.json", bc_read_json(file, e), e))
        goto fail;
    if (bc_path(file, sizeof file, root, "communication-profiles.json", e))
        goto fail;
    if (access(file, F_OK) == 0 &&
        add_file(files, "communication-profiles.json", bc_comm_profiles(root, e), e))
        goto fail;
    if (bc_path(file, sizeof file, root, "compatibility.json", e))
        goto fail;
    if (access(file, F_OK) == 0 &&
        add_file(files, "compatibility.json", bc_compatibility_list(root, e), e))
        goto fail;
    if (history) {
        json_object *list = bc_history_list(root, e);
        if (!list)
            goto fail;
        if (bc_num(list, "skipped") || bc_num(list, "truncated")) {
            json_object_put(list);
            bc_fail(e, "Há registros inválidos ou mais de 20.000 resultados; confira o histórico "
                       "antes do backup.");
            goto fail;
        }
        json_object *items = bc_get(list, "items");
        for (size_t i = 0; i < json_object_array_length(items); i++) {
            const char *id = bc_str(json_object_array_get_idx(items, i), "id");
            char name[64];
            snprintf(name, sizeof name, "results/%s", id);
            if (add_file(files, name, bc_history_read(root, id, e), e)) {
                json_object_put(list);
                goto fail;
            }
        }
        json_object_put(list);
    }
    json_object_put(index);
    return files;
fail:
    if (index)
        json_object_put(index);
    json_object_put(files);
    return NULL;
}

int bc_backup_create(const char *root, const char *destination, int history, bc_error *e) {
    char lockpath[4096], runpath[4096];
    if (bc_path(lockpath, sizeof lockpath, root, "storage.lock", e) ||
        bc_path(runpath, sizeof runpath, root, "execution.lock", e))
        return -1;
    int lock = open(lockpath, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600),
        run = open(runpath, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || run < 0 || flock(lock, LOCK_EX | LOCK_NB) || flock(run, LOCK_EX | LOCK_NB)) {
        if (lock >= 0)
            close(lock);
        if (run >= 0)
            close(run);
        return bc_fail(e,
                       "Há outra operação/ensaio em andamento. Encerre antes de criar o backup.");
    }
    json_object *files = collect(root, history, e);
    close(run);
    close(lock);
    if (!files)
        return -1;
    json_object *manifest = json_object_new_object(), *hashes = json_object_new_object();
    size_t total = 0;
    json_object_object_add(manifest, "format", json_object_new_string("bpcb.backup"));
    json_object_object_add(manifest, "schema", json_object_new_int(1));
    json_object_object_add(manifest, "bpcb_version", json_object_new_string(BC_VERSION));
    json_object_object_add(manifest, "history_included", json_object_new_boolean(history));
    json_object_object_add(manifest, "files", hashes);
    json_object_object_foreach(files, name, value) {
        const char *s = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
        size_t n = strlen(s);
        char hash[65];
        total += n;
        if (n > 8u * 1024u * 1024u || total > BACKUP_LIMIT) {
            json_object_put(files);
            json_object_put(manifest);
            return bc_fail(e, "Backup excede 128 MiB ou contém um registro maior que 8 MiB.");
        }
        digest(s, n, hash);
        json_object_object_add(hashes, name, json_object_new_string(hash));
    }
    json_object_object_add(files, "manifest.json", manifest);
    int fd = open(destination, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        json_object_put(files);
        return bc_fail(e, "Criar backup: escolha um arquivo novo (%s).", strerror(errno));
    }
    struct archive *archive = archive_write_new();
    archive_write_set_format_zip(archive);
    int ok = archive_write_open_fd(archive, fd) == ARCHIVE_OK;
    json_object_object_foreach(files, entry, data) {
        if (!ok)
            break;
        const char *s = json_object_to_json_string_ext(data, JSON_C_TO_STRING_PLAIN);
        size_t n = strlen(s);
        struct archive_entry *item = archive_entry_new();
        archive_entry_set_pathname(item, entry);
        archive_entry_set_filetype(item, AE_IFREG);
        archive_entry_set_perm(item, 0600);
        archive_entry_set_size(item, (la_int64_t)n);
        ok = archive_write_header(archive, item) == ARCHIVE_OK &&
             archive_write_data(archive, s, n) == (la_ssize_t)n;
        archive_entry_free(item);
    }
    if (archive_write_close(archive) != ARCHIVE_OK)
        ok = 0;
    archive_write_free(archive);
    int synced = fsync(fd), closed = close(fd);
    if (synced || closed)
        ok = 0;
    json_object_put(files);
    if (!ok) {
        unlink(destination);
        return bc_fail(e, "Não foi possível concluir o backup.");
    }
    return 0;
}

static json_object *read_backup(const char *path, bc_error *e) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        bc_fail(e, "Abrir backup: %s", strerror(errno));
        return NULL;
    }
    struct archive *archive = archive_read_new();
    archive_read_support_format_zip(archive);
    json_object *files = json_object_new_object();
    int ok = archive_read_open_fd(archive, fd, 65536) == ARCHIVE_OK, rc = ARCHIVE_EOF;
    size_t total = 0;
    struct archive_entry *entry;
    while (ok && (rc = archive_read_next_header(archive, &entry)) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        la_int64_t count = archive_entry_size(entry);
        if (!name || !bc_safe_path(name) || archive_entry_filetype(entry) != AE_IFREG ||
            archive_entry_symlink(entry) || archive_entry_hardlink(entry) || count < 0 ||
            count > 8 * 1024 * 1024 || total + (size_t)count > BACKUP_LIMIT ||
            bc_get(files, name) || json_object_object_length(files) >= 25002) {
            ok = 0;
            break;
        }
        size_t n = (size_t)count, used = 0;
        char *data = malloc(n + 1);
        if (!data) {
            ok = 0;
            break;
        }
        while (used < n) {
            la_ssize_t got = archive_read_data(archive, data + used, n - used);
            if (got <= 0)
                break;
            used += (size_t)got;
        }
        char extra;
        la_ssize_t more = archive_read_data(archive, &extra, 1);
        json_object *o = used == n && more == 0 ? bc_parse_json(data, n, e) : NULL;
        free(data);
        if (!o || (!strcmp(name, "manifest.json") ? 0 : !entry_ok(name, o))) {
            if (o)
                json_object_put(o);
            ok = 0;
            break;
        }
        json_object_object_add(files, name, o);
        total += n;
    }
    if (rc != ARCHIVE_EOF)
        ok = 0;
    archive_read_free(archive);
    close(fd);
    json_object *manifest = bc_get(files, "manifest.json"), *hashes = bc_get(manifest, "files");
    if (strcmp(bc_str(manifest, "format"), "bpcb.backup") || bc_num(manifest, "schema") != 1 ||
        !json_object_is_type(hashes, json_type_object) ||
        json_object_object_length(hashes) + 1 != json_object_object_length(files))
        ok = 0;
    if (ok) {
        json_object_object_foreach(hashes, name, expected) {
            json_object *o = bc_get(files, name);
            char hash[65];
            if (!o) {
                ok = 0;
                break;
            }
            json_digest(o, hash);
            if (strcmp(hash, json_object_get_string(expected))) {
                ok = 0;
                break;
            }
        }
    }
    if (!ok) {
        json_object_put(files);
        bc_fail(e, "Backup inválido, corrompido ou acima dos limites. Nenhum dado foi restaurado.");
        return NULL;
    }
    return files;
}

static int safe_target(const char *root, const char *relative, char path[4096], bc_error *e) {
    if (!bc_safe_path(relative) || bc_path(path, 4096, root, relative, e))
        return -1;
    char probe[4096];
    snprintf(probe, sizeof probe, "%s", path);
    size_t start = strlen(root) + 1;
    for (size_t i = start;; i++)
        if (probe[i] == '/' || !probe[i]) {
            char saved = probe[i];
            probe[i] = 0;
            struct stat st;
            if (lstat(probe, &st) == 0 && S_ISLNK(st.st_mode)) {
                bc_fail(e, "Destino contém link: %s.", relative);
                return -1;
            }
            probe[i] = saved;
            if (!saved)
                break;
        }
    return 0;
}

static json_object *plan(const char *root, json_object *files, bc_error *e) {
    char index_path[4096];
    if (bc_path(index_path, sizeof index_path, root, "active.json", e))
        return NULL;
    struct stat active_stat;
    json_object *index = lstat(index_path, &active_stat) < 0 && errno == ENOENT
                             ? json_object_new_object()
                             : bc_read_json(index_path, e),
                *items = json_object_new_array();
    if (!index) {
        json_object_put(items);
        return NULL;
    }
    json_object_object_foreach(files, name, data) {
        if (!strcmp(name, "manifest.json"))
            continue;
        char target[512], path[4096];
        json_object *value = data;
        if (!strncmp(name, "profiles/", 9)) {
            const char *board = bc_str(data, "board_id"), *key = bc_str(index, board);
            char folder[4096];
            if (!*key || !bc_safe_path(key) || strchr(key, '/')) {
                bc_fail(e,
                        "Instale primeiro uma versão compatível da bancada %s para este sistema.",
                        board);
                goto fail;
            }
            json_object *profile = bc_profile(root, board, folder, e);
            if (!profile)
                goto fail;
            if (strcmp(bc_str(profile, "revision"), bc_str(data, "revision"))) {
                json_object_put(profile);
                bc_fail(e, "Revisão incompatível para %s.", board);
                goto fail;
            }
            value = bc_get(data, "values");
            json_object *checked = bc_apply(profile, value, e);
            json_object_put(profile);
            if (!checked)
                goto fail;
            json_object_put(checked);
            snprintf(target, sizeof target, "state/%s.json", key);
        } else
            snprintf(target, sizeof target, "%s", name);
        if (safe_target(root, target, path, e))
            goto fail;
        int exists = access(path, F_OK) == 0;
        json_object *before = exists ? bc_read_json(path, e) : NULL;
        if (exists && !before)
            goto fail;
        json_object *item = json_object_new_object();
        json_object_object_add(item, "path", json_object_new_string(target));
        json_object_object_add(item, "source", json_object_new_string(name));
        json_object_object_add(item, "data", bc_clone(value));
        json_object_object_add(item, "before", before);
        json_object_object_add(item, "exists", json_object_new_boolean(exists));
        json_object_object_add(
            item, "conflict", json_object_new_boolean(exists && !json_object_equal(before, value)));
        json_object_array_add(items, item);
    }
    json_object_put(index);
    return items;
fail:
    json_object_put(index);
    json_object_put(items);
    return NULL;
}

static void plan_token(const char *archive_hash, json_object *items, char out[132]) {
    char target_hash[65];
    json_digest(items, target_hash);
    snprintf(out, 132, "%s:%s", archive_hash, target_hash);
}

json_object *bc_backup_inspect(const char *root, const char *path, bc_error *e) {
    json_object *files = read_backup(path, e);
    if (!files)
        return NULL;
    json_object *items = plan(root, files, e);
    if (!items) {
        json_object_put(files);
        return NULL;
    }
    char hash[65], token[132];
    if (bc_sha_file(path, hash, e)) {
        json_object_put(items);
        json_object_put(files);
        return NULL;
    }
    plan_token(hash, items, token);
    json_object *result = json_object_new_object(), *summary = json_object_new_array();
    unsigned conflicts = 0;
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i), *row = json_object_new_object();
        json_object_object_add(row, "path", json_object_get(bc_get(item, "source")));
        int conflict = json_object_get_boolean(bc_get(item, "conflict"));
        conflicts += (unsigned)conflict;
        json_object_object_add(
            row, "status",
            json_object_new_string(conflict                                          ? "conflito"
                                   : json_object_get_boolean(bc_get(item, "exists")) ? "idêntico"
                                                                                     : "novo"));
        json_object_array_add(summary, row);
    }
    json_object_object_add(result, "files", summary);
    json_object_object_add(result, "conflicts", json_object_new_int64(conflicts));
    json_object_object_add(result, "token", json_object_new_string(token));
    json_object_object_add(
        result, "history_included",
        json_object_get(bc_get(bc_get(files, "manifest.json"), "history_included")));
    json_object_put(items);
    json_object_put(files);
    return result;
}

static int reserve(const char *root, int *storage, int *run, bc_error *e) {
    char a[4096], b[4096];
    *storage = *run = -1;
    if (bc_path(a, sizeof a, root, "storage.lock", e) ||
        bc_path(b, sizeof b, root, "execution.lock", e))
        return -1;
    *storage = open(a, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    *run = open(b, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (*storage < 0 || *run < 0 || flock(*storage, LOCK_EX | LOCK_NB) ||
        flock(*run, LOCK_EX | LOCK_NB)) {
        if (*storage >= 0)
            close(*storage);
        if (*run >= 0)
            close(*run);
        *storage = *run = -1;
        return bc_fail(
            e, "Há outra operação/ensaio em andamento. Encerre antes de restaurar ou recuperar.");
    }
    return 0;
}

static int recovery_id(const char *id) {
    if (strlen(id) != 23 || strncmp(id, "restore-recovery-", 17))
        return 0;
    for (const char *p = id + 17; *p; p++)
        if (!isalnum((unsigned char)*p))
            return 0;
    return 1;
}

static int target_ok(const char *path) {
    if (!bc_safe_path(path) || strlen(path) > 400)
        return 0;
    if (!strcmp(path, "ui.json") || !strcmp(path, "communication-profiles.json") ||
        !strcmp(path, "compatibility.json"))
        return 1;
    size_t n = strlen(path);
    if (!strncmp(path, "state/", 6) && !strchr(path + 6, '/') && n > 11 &&
        !strcmp(path + n - 5, ".json"))
        return 1;
    if (!strncmp(path, "calibration/", 12)) {
        const char *slash = strchr(path + 12, '/');
        return slash && !strchr(slash + 1, '/') && n > 18 && !strcmp(path + n - 5, ".json");
    }
    if (!strncmp(path, "results/run-", 12) && n == 18) {
        for (const char *p = path + 12; *p; p++)
            if (!isalnum((unsigned char)*p))
                return 0;
        return 1;
    }
    return 0;
}

static int pending_state(const char *state) {
    return !strcmp(state, "prepared") || !strcmp(state, "applying") || !strcmp(state, "recovering");
}

static int state_known(const char *state) {
    return pending_state(state) || !strcmp(state, "completed") || !strcmp(state, "recovered") ||
           !strcmp(state, "kept") || !strcmp(state, "rolled_back");
}

static json_object *read_journal(const char *root, const char *id, char directory[4096],
                                 bc_error *e) {
    if (!recovery_id(id) || safe_target(root, id, directory, e)) {
        bc_fail(e, "Identificador de recuperação inválido.");
        return NULL;
    }
    struct stat st;
    if (lstat(directory, &st) || !S_ISDIR(st.st_mode)) {
        bc_fail(e, "Pasta de recuperação ausente.");
        return NULL;
    }
    char path[4096];
    if (bc_path(path, sizeof path, directory, "index.json", e))
        return NULL;
    return bc_read_json(path, e);
}

json_object *bc_recovery_list(const char *root, bc_error *e) {
    DIR *directory = opendir(root);
    if (!directory) {
        if (errno == ENOENT)
            return json_object_new_array();
        bc_fail(e, "Abrir pasta de dados para conferir restaurações.");
        return NULL;
    }
    json_object *list = json_object_new_array();
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!recovery_id(entry->d_name))
            continue;
        char folder[4096], file[4096];
        bc_error local = {{0}};
        if (bc_path(folder, sizeof folder, root, entry->d_name, &local) ||
            bc_path(file, sizeof file, folder, "index.json", &local))
            continue;
        struct stat st;
        if (lstat(folder, &st) || !S_ISDIR(st.st_mode))
            continue;
        if (access(file, F_OK) && errno == ENOENT)
            continue; /* snapshot not completed: no data writes started */
        json_object *journal = read_journal(root, entry->d_name, folder, &local),
                    *row = json_object_new_object();
        int legacy = json_object_is_type(journal, json_type_array);
        int valid = journal && !strcmp(bc_str(journal, "format"), "bpcb.restore") &&
                    bc_num(journal, "schema") == 1 && state_known(bc_str(journal, "state")) &&
                    json_object_is_type(bc_get(journal, "items"), json_type_array);
        const char *state = legacy ? "legacy" : valid ? bc_str(journal, "state") : "invalid";
        json_object_object_add(row, "id", json_object_new_string(entry->d_name));
        json_object_object_add(row, "state", json_object_new_string(state));
        json_object_object_add(row, "created_at",
                               json_object_new_string(bc_str(journal, "created_at")));
        json_object_object_add(
            row, "pending", json_object_new_boolean(!legacy && (!valid || pending_state(state))));
        json_object_object_add(row, "recoverable",
                               json_object_new_boolean(
                                   valid && (pending_state(state) || !strcmp(state, "completed"))));
        json_object_array_add(list, row);
        if (journal)
            json_object_put(journal);
        if (json_object_array_length(list) > 1000) {
            closedir(directory);
            json_object_put(list);
            bc_fail(e, "Mais de 1.000 cópias de recuperação. Organize a pasta de dados antes de "
                       "continuar.");
            return NULL;
        }
    }
    closedir(directory);
    return list;
}

int bc_recovery_pending(const char *root, bc_error *e) {
    json_object *list = bc_recovery_list(root, e);
    if (!list)
        return -1;
    int pending = 0;
    for (size_t i = 0; i < json_object_array_length(list); i++)
        pending +=
            json_object_get_boolean(bc_get(json_object_array_get_idx(list, i), "pending")) ? 1 : 0;
    json_object_put(list);
    return pending;
}

static json_object *recovery_plan(const char *root, const char *id, json_object **journal_out,
                                  bc_error *e) {
    char directory[4096];
    json_object *journal = read_journal(root, id, directory, e);
    if (!journal)
        return NULL;
    json_object *entries = bc_get(journal, "items"), *plan = json_object_new_array(),
                *seen = json_object_new_object();
    if (strcmp(bc_str(journal, "format"), "bpcb.restore") || bc_num(journal, "schema") != 1 ||
        !state_known(bc_str(journal, "state")) || !json_object_is_type(entries, json_type_array) ||
        json_object_array_length(entries) > 25000) {
        bc_fail(e, "Cópia antiga ou diário inválido; confira os arquivos manualmente.");
        goto fail;
    }
    for (size_t i = 0; i < json_object_array_length(entries); i++) {
        json_object *item = json_object_array_get_idx(entries, i);
        const char *target = bc_str(item, "path");
        if (!target_ok(target) || bc_get(seen, target) ||
            !json_object_is_type(bc_get(item, "existed"), json_type_boolean) ||
            !json_object_is_type(bc_get(item, "selected"), json_type_boolean)) {
            bc_fail(e, "Destino inválido ou duplicado no diário de recuperação.");
            goto fail;
        }
        json_object_object_add(seen, target, json_object_new_boolean(1));
        if (!json_object_get_boolean(bc_get(item, "selected")))
            continue;
        char path[4096], name[48], hash[65];
        json_object *before = NULL, *current = NULL;
        int existed = json_object_get_boolean(bc_get(item, "existed"));
        if (existed) {
            snprintf(name, sizeof name, "%u.json", (unsigned)i);
            if (bc_path(path, sizeof path, directory, name, e))
                goto fail;
            before = bc_read_json(path, e);
            if (!before)
                goto fail;
            json_digest(before, hash);
            if (strcmp(hash, bc_str(item, "before_sha256"))) {
                json_object_put(before);
                bc_fail(e, "A cópia anterior foi alterada ou corrompida.");
                goto fail;
            }
        }
        if (safe_target(root, target, path, e)) {
            if (before)
                json_object_put(before);
            goto fail;
        }
        int exists = access(path, F_OK) == 0;
        if (exists) {
            current = bc_read_json(path, e);
            if (!current) {
                if (before)
                    json_object_put(before);
                goto fail;
            }
        }
        int same_before = exists == existed && (!exists || json_object_equal(before, current));
        int same_after = 0;
        if (current) {
            json_digest(current, hash);
            same_after = !strcmp(hash, bc_str(item, "after_sha256"));
        }
        json_object *row = json_object_new_object();
        json_object_object_add(row, "path", json_object_new_string(target));
        json_object_object_add(row, "before", before);
        json_object_object_add(row, "current", current);
        json_object_object_add(row, "existed", json_object_new_boolean(existed));
        json_object_object_add(row, "exists", json_object_new_boolean(exists));
        json_object_object_add(row, "already_before", json_object_new_boolean(same_before));
        json_object_object_add(row, "conflict",
                               json_object_new_boolean(!same_before && !same_after));
        json_object_array_add(plan, row);
    }
    json_object_put(seen);
    *journal_out = journal;
    return plan;
fail:
    json_object_put(seen);
    json_object_put(plan);
    json_object_put(journal);
    return NULL;
}

static void recovery_token(json_object *journal, json_object *plan, char token[132]) {
    char hash[65];
    json_digest(journal, hash);
    plan_token(hash, plan, token);
}

json_object *bc_recovery_inspect(const char *root, const char *id, bc_error *e) {
    json_object *journal = NULL, *items = recovery_plan(root, id, &journal, e);
    if (!items)
        return NULL;
    json_object *out = json_object_new_object(), *rows = json_object_new_array();
    unsigned conflicts = 0;
    char token[132];
    recovery_token(journal, items, token);
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i), *row = json_object_new_object();
        int conflict = json_object_get_boolean(bc_get(item, "conflict"));
        conflicts += (unsigned)conflict;
        json_object_object_add(row, "path", json_object_get(bc_get(item, "path")));
        json_object_object_add(
            row, "status",
            json_object_new_string(conflict ? "conflito: alteração posterior"
                                   : json_object_get_boolean(bc_get(item, "already_before"))
                                       ? "já está no estado anterior"
                                   : json_object_get_boolean(bc_get(item, "existed"))
                                       ? "recuperar conteúdo anterior"
                                       : "remover arquivo criado pela restauração"));
        json_object_array_add(rows, row);
    }
    json_object_object_add(out, "id", json_object_new_string(id));
    json_object_object_add(out, "state", json_object_get(bc_get(journal, "state")));
    json_object_object_add(out, "token", json_object_new_string(token));
    json_object_object_add(out, "files", rows);
    json_object_object_add(out, "conflicts", json_object_new_int64(conflicts));
    json_object_put(items);
    json_object_put(journal);
    return out;
}

static int journal_state(const char *folder, json_object *journal, const char *state, bc_error *e) {
    char path[4096];
    json_object_object_add(journal, "state", json_object_new_string(state));
    return bc_path(path, sizeof path, folder, "index.json", e) || bc_write_json(path, journal, e)
               ? -1
               : 0;
}

static int remove_recovered(const char *path, bc_error *e) {
    if (unlink(path) && errno != ENOENT)
        return bc_fail(e, "Falha ao remover arquivo criado pela restauração.");
#ifndef _WIN32
    char parent[4096];
    snprintf(parent, sizeof parent, "%s", path);
    char *slash = strrchr(parent, '/');
    if (slash)
        *slash = 0;
    else
        strcpy(parent, ".");
    int fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return bc_fail(e, "Sincronizar pasta de recuperação.");
    int rc = fsync(fd);
    close(fd);
    if (rc)
        return bc_fail(e, "Sincronizar pasta de recuperação.");
#endif
    return 0;
}

int bc_recovery_apply(const char *root, const char *id, const char *expected, int keep_current,
                      bc_error *e) {
    int storage, run;
    if (reserve(root, &storage, &run, e))
        return -1;
    json_object *journal = NULL, *items = recovery_plan(root, id, &journal, e);
    int rc = -1;
    char folder[4096], token[132];
    if (!items || safe_target(root, id, folder, e))
        goto done;
    if (!pending_state(bc_str(journal, "state")) && strcmp(bc_str(journal, "state"), "completed")) {
        bc_fail(e, "Esta cópia já foi resolvida.");
        goto done;
    }
    recovery_token(journal, items, token);
    if (!expected || strcmp(expected, token)) {
        bc_fail(e, "Os dados mudaram após a prévia. Confira novamente.");
        goto done;
    }
    if (keep_current) {
        rc = journal_state(folder, journal, "kept", e);
        goto done;
    }
    for (size_t i = 0; i < json_object_array_length(items); i++)
        if (json_object_get_boolean(bc_get(json_object_array_get_idx(items, i), "conflict"))) {
            bc_fail(e, "Há alterações posteriores à restauração. Nenhum arquivo será sobrescrito; "
                       "preserve e confira esses conflitos.");
            goto done;
        }
    if (journal_state(folder, journal, "recovering", e))
        goto done;
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        if (json_object_get_boolean(bc_get(item, "already_before")))
            continue;
        char path[4096];
        if (safe_target(root, bc_str(item, "path"), path, e))
            goto done;
        if (json_object_get_boolean(bc_get(item, "existed"))) {
            if (bc_write_json(path, bc_get(item, "before"), e))
                goto done;
        } else if (remove_recovered(path, e))
            goto done;
    }
    rc = journal_state(folder, journal, "recovered", e);
done:
    if (journal)
        json_object_put(journal);
    if (items)
        json_object_put(items);
    close(run);
    close(storage);
    return rc;
}

int bc_backup_restore(const char *root, const char *path, const char *expected, int replace,
                      bc_error *e) {
    int lock, run;
    if (reserve(root, &lock, &run, e))
        return -1;
    int rc = -1, pending = bc_recovery_pending(root, e);
    json_object *files = NULL, *items = NULL, *journal = NULL;
    char hash[65], token[132], recovery[4096];
    size_t applied = 0;
    if (pending) {
        if (pending > 0)
            bc_fail(e, "Resolva a restauração interrompida em Recuperação antes de iniciar outra.");
        goto done;
    }
    files = read_backup(path, e);
    items = files ? plan(root, files, e) : NULL;
    if (!items || bc_sha_file(path, hash, e))
        goto done;
    plan_token(hash, items, token);
    if (!expected || strcmp(expected, token)) {
        bc_fail(e, "O backup ou os cadastros mudaram após a conferência. Confira novamente.");
        goto done;
    }
    if (bc_path(recovery, sizeof recovery, root, "restore-recovery-XXXXXX", e) ||
        !mkdtemp(recovery)) {
        bc_fail(e, "Não foi possível preparar a cópia de recuperação.");
        goto done;
    }
    journal = json_object_new_object();
    json_object *entries = json_object_new_array();
    json_object_object_add(journal, "format", json_object_new_string("bpcb.restore"));
    json_object_object_add(journal, "schema", json_object_new_int(1));
    json_object_object_add(journal, "items", entries);
    GDateTime *date = g_date_time_new_now_utc();
    char *stamp = g_date_time_format_iso8601(date);
    json_object_object_add(journal, "created_at", json_object_new_string(stamp));
    g_free(stamp);
    g_date_time_unref(date);
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i), *old = bc_get(item, "before");
        char dest[4096], name[48];
        snprintf(name, sizeof name, "%u.json", (unsigned)i);
        if (old && (bc_path(dest, sizeof dest, recovery, name, e) || bc_write_json(dest, old, e)))
            goto done;
        json_object *row = json_object_new_object();
        json_object_object_add(row, "path", json_object_get(bc_get(item, "path")));
        json_object_object_add(row, "existed", json_object_get(bc_get(item, "exists")));
        json_object_object_add(
            row, "selected",
            json_object_new_boolean(replace || !json_object_get_boolean(bc_get(item, "conflict"))));
        if (old) {
            json_digest(old, hash);
            json_object_object_add(row, "before_sha256", json_object_new_string(hash));
        }
        json_digest(bc_get(item, "data"), hash);
        json_object_object_add(row, "after_sha256", json_object_new_string(hash));
        json_object_array_add(entries, row);
    }
    if (journal_state(recovery, journal, "prepared", e))
        goto done;
#ifndef _WIN32
    int root_fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root_fd < 0) {
        bc_fail(e, "Sincronizar pasta de recuperação.");
        goto done;
    }
    int sync_rc = fsync(root_fd);
    close(root_fd);
    if (sync_rc) {
        bc_fail(e, "Sincronizar pasta de recuperação.");
        goto done;
    }
#endif
    if (journal_state(recovery, journal, "applying", e))
        goto done;
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        applied = i + 1;
        if (!replace && json_object_get_boolean(bc_get(item, "conflict")))
            continue;
        char dest[4096], parent[4096];
        if (safe_target(root, bc_str(item, "path"), dest, e))
            goto rollback;
        strcpy(parent, dest);
        *strrchr(parent, '/') = 0;
        if (bc_mkdirs(parent, e) || bc_write_json(dest, bc_get(item, "data"), e))
            goto rollback;
    }
    rc = journal_state(recovery, journal, "completed", e);
    goto done;
rollback:
    {
        char cause[500];
        snprintf(cause, sizeof cause, "%.499s", e->text);
        int rollback_failed = 0;
        while (applied) {
            json_object *item = json_object_array_get_idx(items, --applied);
            if (!replace && json_object_get_boolean(bc_get(item, "conflict")))
                continue;
            char dest[4096];
            bc_error ignored = {{0}};
            if (safe_target(root, bc_str(item, "path"), dest, &ignored)) {
                rollback_failed = 1;
                continue;
            }
            if (json_object_get_boolean(bc_get(item, "exists"))) {
                if (bc_write_json(dest, bc_get(item, "before"), &ignored))
                    rollback_failed = 1;
            } else if (remove_recovered(dest, &ignored))
                rollback_failed = 1;
        }
        if (!rollback_failed) {
            bc_error ignored = {{0}};
            if (journal_state(recovery, journal, "rolled_back", &ignored))
                rollback_failed = 1;
        }
        bc_fail(e, "Restauração falhou: %s. %s; cópia em %.160s.", cause,
                rollback_failed ? "Abra Recuperação assistida" : "Alterações revertidas", recovery);
    }
done:
    if (journal)
        json_object_put(journal);
    if (items)
        json_object_put(items);
    if (files)
        json_object_put(files);
    close(run);
    close(lock);
    return rc;
}
