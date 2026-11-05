#define _POSIX_C_SOURCE 200809L
#include "../workbench.h"
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define IMAGE_LIMIT (16 * 1024 * 1024)
#define MANIFEST_LIMIT 65536

static void text(json_object *o, const char *k, const char *v) {
    json_object_object_add(o, k, json_object_new_string(v));
}

static void image_name(char out[64], size_t index, const char *format) {
    snprintf(out, 64, "images/image-%02u.%s", (unsigned)index, format);
}

static int valid_extension(const char *s) {
    return !strcmp(s, "bin") || !strcmp(s, "elf") || !strcmp(s, "hex") || !strcmp(s, "svf") ||
           !strcmp(s, "xsvf");
}

/* Only generated names are ever removed. No recursive removal of a user directory. */
static void clear_owned(const char *root, json_object *m) {
    char path[4096];
    json_object *a = bc_get(m, "images");
    if (!json_object_is_type(a, json_type_array)) {
        rmdir(root);
        return;
    }
    for (size_t i = 0; i < json_object_array_length(a) && i < 16; i++) {
        const char *fmt = bc_str(json_object_array_get_idx(a, i), "format");
        if (!valid_extension(fmt))
            continue;
        char relative[64];
        image_name(relative, i, fmt);
        if (bc_path(path, sizeof path, root, relative, NULL) == 0)
            unlink(path);
    }
    if (bc_path(path, sizeof path, root, "images", NULL) == 0)
        rmdir(path);
    rmdir(root);
}

static int regular_entry(struct archive_entry *entry, bc_error *e) {
    if (archive_entry_filetype(entry) != AE_IFREG || archive_entry_symlink(entry) ||
        archive_entry_hardlink(entry) || archive_entry_is_encrypted(entry) ||
        archive_entry_size(entry) < 0)
        return bc_fail(e, "Pacote contém entrada não regular, vínculo ou criptografia.");
    return 0;
}

static int local_archive(const char *path, bc_error *e) {
    struct stat st;
    if (lstat(path, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 1 ||
        st.st_size > IMAGE_LIMIT + 1024 * 1024)
        return bc_fail(e, "Pacote de firmware deve ser arquivo regular local de até 17 MiB.");
    return 0;
}

static unsigned u16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_at(int fd, off_t at, void *data, size_t size) {
    if (lseek(fd, at, SEEK_SET) != at)
        return -1;
    size_t got = 0;
    while (got < size) {
        ssize_t n = read(fd, (uint8_t *)data + got, size - got);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

/* libarchive normalizes ZIP backslashes. Inspect raw directory/local names first
   so aliases and platform-dependent names cannot bypass our canonical format. */
static int zip_names(int fd, bc_error *e) {
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 22 ||
        st.st_size > IMAGE_LIMIT + 1024 * 1024)
        return bc_fail(e, "ZIP regular de até 17 MiB obrigatório.");
    size_t tail = (size_t)st.st_size;
    if (tail > 65557)
        tail = 65557;
    uint8_t bytes[65557];
    if (read_at(fd, st.st_size - (off_t)tail, bytes, tail) < 0)
        return bc_fail(e, "ZIP truncado.");
    size_t end = tail;
    for (size_t i = tail - 22;; i--) {
        if (u32(bytes + i) == 0x06054b50u && i + 22u + u16(bytes + i + 20) == tail) {
            end = i;
            break;
        }
        if (!i)
            break;
    }
    if (end == tail)
        return bc_fail(e, "Diretório ZIP ausente.");
    const uint8_t *h = bytes + end;
    unsigned count = u16(h + 10);
    uint64_t offset = u32(h + 16), size = u32(h + 12), eocd = (uint64_t)st.st_size - tail + end;
    if (u16(h + 4) || u16(h + 6) || u16(h + 8) != count || !count || count > 17 ||
        offset + size != eocd || offset > IMAGE_LIMIT + 1024 * 1024)
        return bc_fail(e, "ZIP multipartes, ZIP64 ou diretório inválido não é suportado.");
    uint64_t at = offset, begin[17], finish[17];
    char names[17][128];
    for (unsigned i = 0; i < count; i++) {
        uint8_t c[46], l[30];
        if (at + sizeof c > offset + size || read_at(fd, (off_t)at, c, sizeof c) < 0 ||
            u32(c) != 0x02014b50u)
            return bc_fail(e, "Cabeçalho ZIP inválido.");
        unsigned n = u16(c + 28), extra = u16(c + 30), comment = u16(c + 32), flags = u16(c + 8),
                 method = u16(c + 10);
        uint64_t local = u32(c + 42), compressed = u32(c + 20), expanded = u32(c + 24);
        if (!n || n >= 128 || u16(c + 34) || (flags & 0x2041u) || (method != 0 && method != 8) ||
            expanded > IMAGE_LIMIT || at + 46u + n + extra + comment > offset + size ||
            local + 30u + n > offset || read_at(fd, (off_t)(at + 46), names[i], n) < 0)
            return bc_fail(e, "Entrada ZIP fora dos limites ou criptografada.");
        names[i][n] = 0;
        if (strlen(names[i]) != n || !bc_safe_path(names[i]) ||
            !bc_utf8((const unsigned char *)names[i], n))
            return bc_fail(e, "Nome bruto ZIP inválido.");
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(names[i], names[j]))
                return bc_fail(e, "Nome ZIP repetido.");
        if (read_at(fd, (off_t)local, l, sizeof l) < 0 || u32(l) != 0x04034b50u ||
            u16(l + 6) != flags || u16(l + 8) != method || u16(l + 26) != n)
            return bc_fail(e, "Cabeçalhos ZIP local/diretório divergentes.");
        char raw[128];
        if (read_at(fd, (off_t)(local + 30), raw, n) < 0 || memcmp(raw, names[i], n))
            return bc_fail(e, "Nome ZIP local/diretório divergente.");
        begin[i] = local;
        finish[i] = local + 30u + n + u16(l + 28) + compressed;
        if (finish[i] > offset)
            return bc_fail(e, "Conteúdo ZIP ultrapassa o diretório.");
        for (unsigned j = 0; j < i; j++)
            if (begin[i] < finish[j] && begin[j] < finish[i])
                return bc_fail(e, "Entradas ZIP sobrepostas.");
        at += 46u + n + extra + comment;
    }
    if (at != offset + size || begin[0] != 0 || strcmp(names[0], "manifest.json"))
        return bc_fail(e, "Layout ZIP não canônico.");
    return lseek(fd, 0, SEEK_SET) == 0 ? 0 : bc_fail(e, "Reposicionar ZIP.");
}

static int snapshot_archive(const char *source, const char *folder, char path[4096],
                            char digest[65], bc_error *e) {
    if (local_archive(source, e) < 0 || bc_path(path, 4096, folder, "bundle.zip", e) < 0)
        return -1;
    int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW), out = -1, rc = -1;
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    struct stat st;
    if (in < 0 || fstat(in, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 1 ||
        st.st_size > IMAGE_LIMIT + 1024 * 1024) {
        bc_fail(e, "Pacote local inválido.");
        goto done;
    }
    out = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (out < 0) {
        bc_fail(e, "Criar cópia privada: %s", strerror(errno));
        goto done;
    }
    uint8_t block[65536];
    size_t total = 0;
    for (;;) {
        ssize_t n = read(in, block, sizeof block);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            bc_fail(e, "Ler pacote de firmware.");
            goto done;
        }
        if (!n)
            break;
        if ((size_t)n > IMAGE_LIMIT + 1024 * 1024 - total) {
            bc_fail(e, "Pacote excedeu o limite durante a cópia.");
            goto done;
        }
        g_checksum_update(sum, block, n);
        size_t at = 0;
        while (at < (size_t)n) {
            ssize_t w = write(out, block + at, (size_t)n - at);
            if (w < 0 && errno == EINTR)
                continue;
            if (w <= 0) {
                bc_fail(e, "Salvar cópia privada.");
                goto done;
            }
            at += (size_t)w;
        }
        total += (size_t)n;
    }
    if (total != (size_t)st.st_size || fsync(out) < 0) {
        bc_fail(e, "Pacote mudou de tamanho ou falhou ao salvar.");
        goto done;
    }
    snprintf(digest, 65, "%s", g_checksum_get_string(sum));
    rc = 0;
done:
    if (in >= 0)
        close(in);
    if (out >= 0) {
        close(out);
        if (rc)
            unlink(path);
    }
    g_checksum_free(sum);
    return rc;
}

static json_object *extract(const char *path, const char *temp, bc_error *e) {
    if (local_archive(path, e) < 0)
        return NULL;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return bc_fail(e, "Abrir pacote: %s", strerror(errno)), NULL;
    if (zip_names(fd, e) < 0) {
        close(fd);
        return NULL;
    }
    struct archive *ar = archive_read_new();
    archive_read_support_format_zip(ar);
    json_object *m = NULL, *result = NULL;
    struct archive_entry *entry;
    char images_dir[4096];
    unsigned seen = 0;
    size_t total = 0;
    uint32_t mask = 0;
    int rc;
    if (archive_read_open_fd(ar, fd, 65536) != ARCHIVE_OK) {
        bc_fail(e, "Pacote ZIP inválido.");
        goto done;
    }
    if (archive_read_next_header(ar, &entry) != ARCHIVE_OK ||
        strcmp(archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "",
               "manifest.json") ||
        regular_entry(entry, e) < 0 || archive_entry_size(entry) > MANIFEST_LIMIT) {
        bc_fail(e, "A primeira entrada deve ser manifest.json regular de até 64 KiB.");
        goto done;
    }
    size_t size = (size_t)archive_entry_size(entry);
    char *data = malloc(size + 1);
    if (!data) {
        bc_fail(e, "Memória insuficiente.");
        goto done;
    }
    size_t at = 0;
    while (at < size) {
        la_ssize_t n = archive_read_data(ar, data + at, size - at);
        if (n <= 0)
            break;
        at += (size_t)n;
    }
    char extra;
    if (at != size || archive_read_data(ar, &extra, 1) != 0) {
        free(data);
        bc_fail(e, "Manifesto truncado ou excedido.");
        goto done;
    }
    data[size] = 0;
    json_object *wrapper = bc_parse_json(data, size, e);
    free(data);
    if (!wrapper)
        goto done;
    if (bc_lab_object(wrapper, "|format||schema||manifest|", e) < 0 ||
        strcmp(bc_str(wrapper, "format"), "bpcb.firmware.bundle") ||
        !json_object_is_type(bc_get(wrapper, "schema"), json_type_int) ||
        bc_num(wrapper, "schema") != 1) {
        json_object_put(wrapper);
        bc_fail(e, "Formato de pacote de firmware incompatível.");
        goto done;
    }
    m = bc_clone(bc_get(wrapper, "manifest"));
    json_object_put(wrapper);
    json_object *a = bc_get(m, "images");
    size_t count = json_object_is_type(a, json_type_array) ? json_object_array_length(a) : 0;
    if (!count || count > 16) {
        bc_fail(e, "Pacote exige 1…16 imagens.");
        goto done;
    }
    for (size_t i = 0; i < count; i++) {
        json_object *x = json_object_array_get_idx(a, i);
        char expected[64];
        const char *fmt = bc_str(x, "format");
        if (!valid_extension(fmt)) {
            bc_fail(e, "Extensão inválida.");
            goto done;
        }
        image_name(expected, i, fmt);
        if (strcmp(bc_str(x, "file"), expected) ||
            !json_object_is_type(bc_get(x, "bytes"), json_type_int) || bc_num(x, "bytes") < 1 ||
            bc_num(x, "bytes") > IMAGE_LIMIT || strlen(bc_str(x, "sha256")) != 64) {
            bc_fail(e, "Nome, tamanho ou hash da imagem inválido.");
            goto done;
        }
        total += (size_t)bc_num(x, "bytes");
    }
    if (total > IMAGE_LIMIT) {
        bc_fail(e, "Imagens excedem 16 MiB.");
        goto done;
    }
    if (bc_path(images_dir, sizeof images_dir, temp, "images", e) < 0 ||
        mkdir(images_dir, 0700) < 0) {
        bc_fail(e, "Criar área privada de imagens: %s", strerror(errno));
        goto done;
    }
    while ((rc = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
        if (++seen > count || regular_entry(entry, e) < 0) {
            bc_fail(e, "Entrada extra ou não regular.");
            goto done;
        }
        const char *name = archive_entry_pathname(entry);
        size_t index = count;
        for (size_t i = 0; i < count; i++)
            if (name && !strcmp(name, bc_str(json_object_array_get_idx(a, i), "file")))
                index = i;
        if (index == count || (mask & (1u << index))) {
            bc_fail(e, "Nome extra, repetido ou caminho inválido no pacote.");
            goto done;
        }
        mask |= 1u << index;
        json_object *x = json_object_array_get_idx(a, index);
        size_t expected = (size_t)bc_num(x, "bytes");
        if ((uint64_t)archive_entry_size(entry) != expected) {
            bc_fail(e, "Tamanho da entrada diverge do manifesto.");
            goto done;
        }
        char destination[4096];
        if (bc_path(destination, sizeof destination, temp, name, e) < 0)
            goto done;
        int out = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (out < 0) {
            bc_fail(e, "Criar imagem privada: %s", strerror(errno));
            goto done;
        }
        uint8_t block[65536];
        size_t written = 0;
        int ok = 1;
        la_ssize_t n;
        while ((n = archive_read_data(ar, block, sizeof block)) > 0) {
            if ((size_t)n > expected - written) {
                ok = 0;
                break;
            }
            size_t offset = 0;
            while (offset < (size_t)n) {
                ssize_t w = write(out, block + offset, (size_t)n - offset);
                if (w < 0 && errno == EINTR)
                    continue;
                if (w <= 0) {
                    ok = 0;
                    break;
                }
                offset += (size_t)w;
            }
            if (!ok)
                break;
            written += (size_t)n;
        }
        if (n < 0 || written != expected || fsync(out) < 0)
            ok = 0;
        if (close(out) < 0)
            ok = 0;
        if (!ok) {
            bc_fail(e, "Imagem truncada, corrompida ou sem espaço.");
            goto done;
        }
    }
    if (rc != ARCHIVE_EOF || seen != count || mask != ((1u << count) - 1u) ||
        archive_read_has_encrypted_entries(ar) > 0) {
        bc_fail(e, "Pacote incompleto, criptografado ou ZIP corrompido.");
        goto done;
    }
    result = bc_firmware_manifest(m, temp, e);
    if (!result)
        goto done;
    /* Return the portable manifest only. The validated absolute paths belong to temp. */
    json_object_put(result);
    result = m;
    m = NULL;
done:
    archive_read_free(ar);
    close(fd);
    if (!result)
        clear_owned(temp, m);
    json_object_put(m);
    return result;
}

static char *temporary(bc_error *e) {
    GError *g = NULL;
    char *p = g_dir_make_tmp("bpcb-firmware-XXXXXX", &g);
    if (!p) {
        bc_fail(e, "Criar área temporária: %s", g ? g->message : "erro");
        g_clear_error(&g);
    }
    return p;
}

json_object *bc_firmware_bundle_inspect(const char *path, bc_error *e) {
    char *temp = temporary(e);
    if (!temp)
        return NULL;
    char copy[4096] = {0}, digest[65];
    json_object *m = NULL, *r = NULL;
    if (snapshot_archive(path, temp, copy, digest, e) == 0)
        m = extract(copy, temp, e);
    if (*copy)
        unlink(copy);
    if (m) {
        r = json_object_new_object();
        text(r, "format", "bpcb.firmware.bundle");
        text(r, "sha256", digest);
        json_object_object_add(r, "manifest", bc_clone(m));
        text(r, "scope",
             "Imagens e manifesto verificados; ferramenta e gravação exigem revisão posterior.");
        clear_owned(temp, m);
    } else
        rmdir(temp);
    g_free(temp);
    json_object_put(m);
    return r;
}

static int entry_write(struct archive *ar, const char *name, const void *data, size_t size) {
    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, name);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0600);
    archive_entry_set_size(entry, (la_int64_t)size);
    int rc = archive_write_header(ar, entry) == ARCHIVE_OK &&
                     archive_write_data(ar, data, size) == (la_ssize_t)size
                 ? 0
                 : -1;
    archive_entry_free(entry);
    return rc;
}

int bc_firmware_bundle_export(json_object *input, const char *destination, bc_error *e) {
    json_object *m = bc_firmware_manifest(input, "", e);
    if (!m)
        return -1;
    json_object *portable = bc_clone(m), *a = bc_get(m, "images"), *p = bc_get(portable, "images");
    for (size_t i = 0; i < json_object_array_length(a); i++) {
        char name[64];
        image_name(name, i, bc_str(json_object_array_get_idx(a, i), "format"));
        text(json_object_array_get_idx(p, i), "file", name);
    }
    json_object *wrapper = json_object_new_object();
    text(wrapper, "format", "bpcb.firmware.bundle");
    json_object_object_add(wrapper, "schema", json_object_new_int(1));
    json_object_object_add(wrapper, "manifest", portable);
    const char *data = json_object_to_json_string_ext(wrapper, JSON_C_TO_STRING_PLAIN);
    int fd = -1, rc = -1;
    struct archive *ar = NULL;
    if (strlen(data) > MANIFEST_LIMIT) {
        bc_fail(e, "Manifesto excede 64 KiB.");
        goto done;
    }
    fd = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        bc_fail(e, "Escolha um nome novo para o pacote (%s).", strerror(errno));
        goto done;
    }
    ar = archive_write_new();
    archive_write_set_format_zip(ar);
    archive_write_set_options(ar, "zip:compression=store");
    if (archive_write_open_fd(ar, fd) != ARCHIVE_OK ||
        entry_write(ar, "manifest.json", data, strlen(data)) < 0)
        goto failure;
    for (size_t i = 0; i < json_object_array_length(a); i++) {
        json_object *x = json_object_array_get_idx(a, i);
        int in = open(bc_str(x, "file"), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (in < 0)
            goto failure;
        struct stat st;
        size_t size = (size_t)bc_num(x, "bytes");
        if (fstat(in, &st) < 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != size) {
            close(in);
            goto failure;
        }
        struct archive_entry *entry = archive_entry_new();
        archive_entry_set_pathname(entry, bc_str(json_object_array_get_idx(p, i), "file"));
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0600);
        archive_entry_set_size(entry, (la_int64_t)size);
        int valid = archive_write_header(ar, entry) == ARCHIVE_OK;
        archive_entry_free(entry);
        GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
        uint8_t block[65536];
        size_t read_bytes = 0;
        while (valid && read_bytes < size) {
            size_t want = size - read_bytes;
            if (want > sizeof block)
                want = sizeof block;
            ssize_t n = read(in, block, want);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                valid = 0;
                break;
            }
            g_checksum_update(sum, block, n);
            valid = archive_write_data(ar, block, (size_t)n) == n;
            read_bytes += (size_t)n;
        }
        uint8_t extra;
        if (valid && read(in, &extra, 1) != 0)
            valid = 0;
        close(in);
        valid =
            valid && read_bytes == size && !strcmp(g_checksum_get_string(sum), bc_str(x, "sha256"));
        g_checksum_free(sum);
        if (!valid)
            goto failure;
    }

    if (archive_write_close(ar) != ARCHIVE_OK || fsync(fd) < 0)
        goto failure;
    rc = 0;
    goto done;
failure:
    bc_fail(e, "Não foi possível concluir o pacote ou uma imagem mudou.");
done:
    if (ar)
        archive_write_free(ar);
    if (fd >= 0) {
        close(fd);
        if (rc)
            unlink(destination);
    }
    json_object_put(wrapper);
    json_object_put(m);
    return rc;
}

json_object *bc_firmware_bundle_import(const char *path, const char *destination,
                                       const char *expected, bc_error *e) {
    if (!g_path_is_absolute(destination) || g_str_has_prefix(destination, "//") ||
        g_str_has_prefix(destination, "\\\\"))
        return bc_fail(e, "Escolha uma pasta local absoluta e nova."), NULL;
    struct stat st;
    if (lstat(destination, &st) == 0 || errno != ENOENT)
        return bc_fail(e, "A pasta de destino já existe ou está inacessível."), NULL;
    char current[65];
    if (!expected || strlen(expected) != 64 || bc_sha_file(path, current, e) < 0 ||
        strcmp(expected, current))
        return bc_fail(e, "Pacote mudou após a revisão."), NULL;
    /* Create beside the destination; final publication is atomic and never overwrites. */
    char *parent = g_path_get_dirname(destination);
    char *template = g_build_filename(parent, ".bpcb-firmware-XXXXXX", NULL);
    g_free(parent);
    if (!mkdtemp(template)) {
        bc_fail(e, "Criar pasta temporária de destino: %s", strerror(errno));
        g_free(template);
        return NULL;
    }
    char copy[4096] = {0}, digest[65];
    json_object *m = NULL, *result = NULL;
    if (snapshot_archive(path, template, copy, digest, e) < 0)
        goto done;
    if (strcmp(expected, digest)) {
        bc_fail(e, "Pacote mudou após a revisão.");
        goto done;
    }
    m = extract(copy, template, e);
    if (!m)
        goto done;
    if (bc_sha_file(path, current, e) < 0 || strcmp(expected, current)) {
        bc_fail(e, "Pacote mudou durante a importação.");
        goto done;
    }
    /* mkdir reserves the destination. Rename images into that owned empty directory. */
    if (mkdir(destination, 0700) < 0) {
        bc_fail(e, "Destino ocupado ou sem permissão.");
        goto done;
    }
    char from[4096], to[4096];
    if (bc_path(from, sizeof from, template, "images", e) < 0 ||
        bc_path(to, sizeof to, destination, "images", e) < 0 || rename(from, to) < 0) {
        rmdir(destination);
        bc_fail(e, "Publicar imagens importadas: %s", strerror(errno));
        goto done;
    }
    result = bc_firmware_manifest(m, destination, e);
    if (!result) {
        clear_owned(destination, m);
        goto done;
    }
    char manifest_path[4096];
    if (bc_path(manifest_path, sizeof manifest_path, destination, "firmware.json", e) < 0 ||
        bc_write_json(manifest_path, m, e) < 0) {
        json_object_put(result);
        result = NULL;
        clear_owned(destination, m);
        goto done;
    }
done:
    if (*copy)
        unlink(copy);
    if (m)
        clear_owned(template, m);
    else
        rmdir(template);
    g_free(template);
    json_object_put(m);
    return result;
}
