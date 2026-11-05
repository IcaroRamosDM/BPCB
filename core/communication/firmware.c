#define _POSIX_C_SOURCE 200809L
#include "../lab.h"
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/file.h>
#endif
static void text(json_object *o, const char *k, const char *v) {
    json_object_object_add(o, k, json_object_new_string(v));
}

static void arg(json_object *a, const char *s) {
    json_object_array_add(a, json_object_new_string(s));
}

static int regular(const char *path, off_t maximum, bc_error *e) {
    struct stat st;
    if (!g_path_is_absolute(path) || g_str_has_prefix(path, "\\\\") ||
        g_str_has_prefix(path, "//") || lstat(path, &st) < 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 1 || st.st_size > maximum)
        return bc_fail(e, "Arquivo absoluto/regular obrigatório ou tamanho excedido: %s", path);
    return 0;
}

static int address(json_object *image, uint64_t *out, bc_error *e) {
    json_object *a = bc_get(image, "address");
    if (!json_object_is_type(a, json_type_int) || json_object_get_int64(a) < 0 ||
        json_object_get_int64(a) > UINT32_MAX)
        return bc_fail(e, "BIN exige endereço explícito de 32 bits.");
    *out = (uint64_t)json_object_get_int64(a);
    return 0;
}

static int extension(const char *file, const char *format) {
    const char *ext = strrchr(file, '.');
    return ext && !g_ascii_strcasecmp(ext + 1, format);
}

typedef struct {
    uint64_t start, end;
} image_range;

static int range_compare(const void *a, const void *b) {
    const image_range *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int elf_check(const char *path, bc_error *e) {
    FILE *f = fopen(path, "rb");
    struct stat st;
    uint8_t h[52];
    int rc = -1;
    if (!f || fstat(fileno(f), &st) < 0 || fread(h, 1, sizeof h, f) != sizeof h) {
        bc_fail(e, "Cabeçalho ELF ausente/incompleto.");
        goto done;
    }
    uint64_t size = (uint64_t)st.st_size;
    if (memcmp(h, "\177ELF", 4) || h[4] != 1 || h[5] != 1 || h[6] != 1 || le16(h + 16) != 2 ||
        le16(h + 18) != 40 || le32(h + 20) != 1 || le16(h + 40) != 52) {
        bc_fail(e, "STM32 exige ELF32 executável little endian ARM válido.");
        goto done;
    }
    uint32_t phoff = le32(h + 28), shoff = le32(h + 32);
    unsigned phnum = le16(h + 44), shnum = le16(h + 48);
    if (!phnum || phnum > 128 || le16(h + 42) != 32 || phoff < 52 ||
        (uint64_t)phoff + (uint64_t)phnum * 32 > size ||
        (shnum &&
         (le16(h + 46) != 40 || shoff < 52 || (uint64_t)shoff + (uint64_t)shnum * 40 > size)) ||
        (!shnum && shoff) || (shnum && le16(h + 50) >= shnum)) {
        bc_fail(e, "Tabelas ELF fora do arquivo ou extensões não suportadas.");
        goto done;
    }
    image_range ranges[128];
    unsigned count = 0;
    int entry_found = 0;
    uint32_t entry = le32(h + 24) & ~1u;
    for (unsigned i = 0; i < phnum; i++) {
        uint8_t ph[32];
        if (fseek(f, (long)((uint64_t)phoff + i * 32), SEEK_SET) ||
            fread(ph, 1, sizeof ph, f) != sizeof ph) {
            bc_fail(e, "Tabela de segmentos ELF truncada.");
            goto done;
        }
        uint32_t type = le32(ph), offset = le32(ph + 4), virtual_address = le32(ph + 8),
                 load = le32(ph + 12);
        uint32_t filesz = le32(ph + 16), memsz = le32(ph + 20), flags = le32(ph + 24),
                 align = le32(ph + 28);
        if ((uint64_t)offset + filesz > size || (type == 1 && memsz < filesz) ||
            (align > 1 && (align & (align - 1)))) {
            bc_fail(e, "Segmento ELF com comprimento/alinhamento inválido.");
            goto done;
        }
        if (type != 1 || !filesz)
            continue;
        if (load < 0x08000000 || (uint64_t)load + filesz > 0x10000000 ||
            (uint64_t)virtual_address + memsz > 0x100000000ull) {
            bc_fail(e, "Segmento ELF fora da região de flash STM32 suportada.");
            goto done;
        }
        ranges[count++] = (image_range){load, (uint64_t)load + filesz};
        if ((flags & 1) && entry >= virtual_address &&
            (uint64_t)entry < (uint64_t)virtual_address + filesz)
            entry_found = 1;
    }
    if (!count || !entry_found) {
        bc_fail(e, "ELF sem imagem de flash/ponto de entrada executável.");
        goto done;
    }
    qsort(ranges, count, sizeof *ranges, range_compare);
    for (unsigned i = 1; i < count; i++)
        if (ranges[i].start < ranges[i - 1].end) {
            bc_fail(e, "Segmentos ELF de flash sobrepostos.");
            goto done;
        }
    for (unsigned i = 0; i < shnum; i++) {
        uint8_t sh[40];
        if (fseek(f, (long)((uint64_t)shoff + i * 40), SEEK_SET) ||
            fread(sh, 1, sizeof sh, f) != sizeof sh) {
            bc_fail(e, "Tabela de seções ELF truncada.");
            goto done;
        }
        uint32_t type = le32(sh + 4), offset = le32(sh + 16), length = le32(sh + 20),
                 link = le32(sh + 24);
        if ((type != 8 && (uint64_t)offset + length > size) || (link && link >= shnum)) {
            bc_fail(e, "Seção ELF fora do arquivo ou referência inválida.");
            goto done;
        }
    }
    rc = 0;
done:
    if (f)
        fclose(f);
    return rc;
}

static int stm_format(const char *path, const char *format, bc_error *e) {
    if (!strcmp(format, "elf"))
        return elf_check(path, e);
    if (strcmp(format, "hex"))
        return 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return bc_fail(e, "Abrir HEX: %s", strerror(errno));
    GArray *ranges = g_array_new(FALSE, FALSE, sizeof(image_range));
    char line[600];
    int ended = 0, rc = -1;
    uint64_t base = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strcspn(line, "\r\n");
        for (size_t i = n; line[i]; i++)
            if (line[i] != '\r' && line[i] != '\n') {
                bc_fail(e, "Conteúdo após o registro Intel HEX.");
                goto done;
            }
        if (!n)
            continue;
        if (ended || line[0] != ':' || n < 11 || n > 521 || (n % 2) != 1) {
            bc_fail(e, "Registro Intel HEX inválido.");
            goto done;
        }
        line[n] = 0;
        uint8_t b[260];
        size_t size = 0;
        if (bc_comm_hex(line + 1, b, sizeof b, &size, e) < 0 || size < 5 ||
            size != (size_t)b[0] + 5) {
            bc_fail(e, "Comprimento Intel HEX inválido.");
            goto done;
        }
        unsigned sum = 0;
        for (size_t i = 0; i < size; i++)
            sum += b[i];
        if (sum & 255u) {
            bc_fail(e, "Checksum Intel HEX inválido.");
            goto done;
        }
        unsigned type = b[3], offset = ((unsigned)b[1] << 8) | b[2];
        if (type == 0) {
            image_range range = {base + offset, base + offset + b[0]};
            if (!b[0] || offset + b[0] > 65536 || range.start < 0x08000000 ||
                range.end > 0x10000000 || ranges->len >= 1000000) {
                bc_fail(e, "HEX fora da região STM32 ou cruzando limite de registro.");
                goto done;
            }
            g_array_append_val(ranges, range);
        } else if (type == 1 && b[0] == 0 && offset == 0)
            ended = 1;
        else if ((type == 2 || type == 4) && b[0] == 2 && offset == 0)
            base = (uint64_t)(((unsigned)b[4] << 8) | b[5]) << (type == 2 ? 4 : 16);
        else if ((type == 3 || type == 5) && b[0] == 4 && offset == 0) {
        } else {
            bc_fail(e, "Tipo Intel HEX não suportado.");
            goto done;
        }
    }
    if (ferror(f) || !ended || !ranges->len) {
        bc_fail(e, "HEX incompleto ou sem dados.");
        goto done;
    }
    g_array_sort(ranges, range_compare);
    for (guint i = 1; i < ranges->len; i++)
        if (g_array_index(ranges, image_range, i).start <
            g_array_index(ranges, image_range, i - 1).end) {
            bc_fail(e, "Registros Intel HEX sobrepostos.");
            goto done;
        }
    rc = 0;
done:
    g_array_free(ranges, TRUE);
    fclose(f);
    return rc;
}

json_object *bc_firmware_manifest(json_object *input, const char *base, bc_error *e) {
    if (bc_lab_object(input, "|schema||name||version||family||target||backend||images|", e) < 0 ||
        !json_object_is_type(bc_get(input, "schema"), json_type_int) ||
        bc_num(input, "schema") != 1)
        return bc_fail(e, "Manifesto de firmware exige schema 1."), NULL;
    const char *strings[] = {"name", "version", "family", "backend"};
    for (unsigned i = 0; i < 4; i++)
        if (!json_object_is_type(bc_get(input, strings[i]), json_type_string)) {
            bc_fail(e, "Campo textual obrigatório no manifesto: %s.", strings[i]);
            return NULL;
        }
    const char *family = bc_str(input, "family"), *backend = bc_str(input, "backend");
    int esp = !strcmp(family, "esp32"), stm = !strcmp(family, "stm32"),
        fpga = !strcmp(family, "fpga");
    if ((!esp && !stm && !fpga) || (strcmp(backend, "openocd") && strcmp(backend, "esptool")) ||
        (!esp && !strcmp(backend, "esptool")) || !*bc_str(input, "name") ||
        strlen(bc_str(input, "name")) > 120 || strlen(bc_str(input, "version")) > 80)
        return bc_fail(e, "Família/backend/nome de firmware inválidos."), NULL;
    if (!json_object_is_type(bc_get(input, "target"), json_type_string) &&
        !json_object_is_type(bc_get(input, "target"), json_type_object))
        return bc_fail(e, "Selecione o alvo exato."), NULL;
    if (!strcmp(backend, "esptool")) {
        const char *chip = bc_str(input, "target");
        if (strncmp(chip, "esp32", 5) || strlen(chip) > 24)
            return bc_fail(e, "Chip ESP32 inválido."), NULL;
        for (const char *p = chip; *p; p++)
            if (!g_ascii_isalnum(*p))
                return bc_fail(e, "Nome de chip inválido."), NULL;
    }
    json_object *images = bc_get(input, "images");
    if (!json_object_is_type(images, json_type_array) || !json_object_array_length(images) ||
        json_object_array_length(images) > 16 || (fpga && json_object_array_length(images) != 1))
        return bc_fail(e, "Use 1…16 imagens; FPGA exige um vetor SVF/XSVF."), NULL;
    if (esp && !strcmp(backend, "openocd") && json_object_array_length(images) > 1)
        return bc_fail(e, "ESP32 com várias imagens usa backend esptool; JTAG aceita uma imagem "
                          "por pacote."),
               NULL;
    json_object *r = bc_clone(input), *out = json_object_new_array();
    json_object_object_add(r, "images", out);
    uint64_t total = 0, starts[16] = {0}, ends[16] = {0};
    for (size_t i = 0; i < json_object_array_length(images); i++) {
        json_object *image = json_object_array_get_idx(images, i);
        if (bc_lab_object(image, "|file||format||role||address||sha256||bytes|", e) < 0)
            goto bad;
        const char *format = bc_str(image, "format"), *file = bc_str(image, "file");
        int bin = !strcmp(format, "bin"), elf = !strcmp(format, "elf"),
            hex = !strcmp(format, "hex"),
            vector = !strcmp(format, "svf") || !strcmp(format, "xsvf");
        if (!extension(file, format) || (stm && !bin && !elf && !hex) || (esp && !bin) ||
            (fpga && !vector) || (stm && json_object_array_length(images) > 1 && !bin) ||
            !bc_lab_id(bc_str(image, "role"))) {
            bc_fail(e, "Formato/role incompatível. STM32 múltiplo exige BIN com endereços; FPGA "
                       "usa SVF/XSVF.");
            goto bad;
        }
        char *path = g_canonicalize_filename(file, base && *base ? base : NULL);
        if (!path || regular(path, 32 * 1024 * 1024, e) < 0) {
            g_free(path);
            goto bad;
        }
        /* Reject symbolic parent components as well as a symbolic final file. */
        char *walk = g_path_get_dirname(path);
        while (walk && strcmp(walk, "/") && strlen(walk) > 3) {
            struct stat st;
            if (lstat(walk, &st) < 0 || !S_ISDIR(st.st_mode)) {
                g_free(walk);
                g_free(path);
                bc_fail(e, "Pasta de firmware ausente ou simbólica.");
                goto bad;
            }
            char *up = g_path_get_dirname(walk);
            if (!strcmp(up, walk)) {
                g_free(up);
                g_free(walk);
                walk = NULL;
                break;
            }
            g_free(walk);
            walk = up;
        }
        g_free(walk);
        struct stat st;
        if (stat(path, &st) < 0) {
            g_free(path);
            goto bad;
        }
        total += (uint64_t)st.st_size;
        if (total > 16 * 1024 * 1024) {
            g_free(path);
            bc_fail(e, "Pacote excede 16 MiB de imagens.");
            goto bad;
        }
        char digest[65];
        if (bc_sha_file(path, digest, e) < 0) {
            g_free(path);
            goto bad;
        }
        if (*bc_str(image, "sha256") && strcmp(digest, bc_str(image, "sha256"))) {
            g_free(path);
            bc_fail(e, "SHA-256 da imagem diverge do manifesto.");
            goto bad;
        }
        if (bin) {
            if (address(image, &starts[i], e) < 0) {
                g_free(path);
                goto bad;
            }
            ends[i] = starts[i] + (uint64_t)st.st_size;
            if (starts[i] % 4 || ends[i] > UINT32_MAX ||
                (stm && (starts[i] < 0x08000000 || ends[i] > 0x10000000)) ||
                (esp && ends[i] > 0x1000000)) {
                g_free(path);
                bc_fail(e, "Endereço/extensão BIN inválido para a família.");
                goto bad;
            }
            for (size_t j = 0; j < i; j++)
                if (starts[i] < ends[j] && starts[j] < ends[i]) {
                    g_free(path);
                    bc_fail(e, "Imagens se sobrepõem.");
                    goto bad;
                }
            if (esp) {
                if (starts[i] % 4096) {
                    g_free(path);
                    bc_fail(e, "Imagens ESP32 devem começar em setor de 4096 bytes.");
                    goto bad;
                }
                for (size_t j = 0; j < i; j++)
                    if ((starts[i] / 4096) < ((ends[j] + 4095) / 4096) &&
                        (starts[j] / 4096) < ((ends[i] + 4095) / 4096)) {
                        g_free(path);
                        bc_fail(e, "Imagens ESP32 compartilham setor de apagamento.");
                        goto bad;
                    }
            }
        } else if (stm && stm_format(path, format, e) < 0) {
            g_free(path);
            goto bad;
        }
        json_object *copy = bc_clone(image);
        text(copy, "file", path);
        g_free(path);
        text(copy, "sha256", digest);
        json_object_object_add(copy, "bytes", json_object_new_int64(st.st_size));
        json_object_array_add(out, copy);
    }
    return r;
bad:
    json_object_put(r);
    return NULL;
}

static json_object *openocd_request(json_object *request, json_object *manifest,
                                    json_object *image) {
    json_object *r = json_object_new_object();
    const char *keys[] = {"executable", "scripts",         "serial",        "usb_location",
                          "speed_khz",  "timeout_seconds", "trusted_tools", "program_confirmed"};
    for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++)
        if (bc_get(request, keys[i]))
            json_object_object_add(r, keys[i], bc_clone(bc_get(request, keys[i])));
    json_object_object_add(r, "target", bc_clone(bc_get(manifest, "target")));
    text(r, "action", bc_str(request, "action"));
    text(r, "firmware", bc_str(image, "file"));
    if (bc_get(image, "address"))
        json_object_object_add(r, "address", bc_clone(bc_get(image, "address")));
    text(r, "expected_sha256", bc_str(image, "sha256"));
    return r;
}

json_object *bc_firmware_plan(json_object *request, bc_error *e) {
    if (bc_lab_object(
            request,
            "|manifest||base||executable||scripts||serial||usb_location||speed_khz||timeout_"
            "seconds||trusted_tools||program_confirmed||expected_sha256||action||communication|",
            e) < 0)
        return NULL;
    json_object *manifest =
        bc_firmware_manifest(bc_get(request, "manifest"), bc_str(request, "base"), e);
    if (!manifest)
        return NULL;
    const char *action = bc_str(request, "action");
    int seconds;
    if ((strcmp(action, "check") && strcmp(action, "program") && strcmp(action, "verify")) ||
        bc_lab_int(request, "timeout_seconds", 120, 1, 600, &seconds, e) < 0 ||
        regular(bc_str(request, "executable"), 128 * 1024 * 1024, e) < 0) {
        json_object_put(manifest);
        return NULL;
    }
    json_object *r = json_object_new_object(), *args = json_object_new_array();
    json_object_object_add(r, "manifest", manifest);
    text(r, "action", action);
    json_object_object_add(r, "timeout_seconds", json_object_new_int(seconds));
    char exe_sha[65];
    if (bc_sha_file(bc_str(request, "executable"), exe_sha, e) < 0)
        goto bad;
    text(r, "tool_sha256", exe_sha);
    if (!strcmp(bc_str(manifest, "backend"), "esptool")) {
        json_object *c = bc_comm_config(bc_get(request, "communication"), e);
        if (!c)
            goto bad;
        const char *port = bc_str(c, "device");
        if (strcmp(bc_str(c, "bus"), "uart") || !*port ||
            (!strcmp(bc_str(c, "adapter"), "ch347") && bc_num(c, "ch347_mode") == 2)) {
            json_object_put(c);
            bc_fail(e, "Esptool exige porta UART VCP exata (HID M2 não é uma porta serial).");
            goto bad;
        }
        arg(args, bc_str(request, "executable"));
        if (!strcmp(action, "check"))
            arg(args, "version");
        else {
            arg(args, "--chip");
            arg(args, bc_str(manifest, "target"));
            arg(args, "--port");
            arg(args, port);
            arg(args, "--baud");
            char baud[32];
            snprintf(baud, sizeof baud, "%u", (unsigned)bc_num(c, "baud"));
            arg(args, baud);
            arg(args, "--before");
            arg(args, "no-reset");
            arg(args, "--after");
            arg(args, "no-reset");
            arg(args, !strcmp(action, "program") ? "write-flash" : "verify-flash");
            json_object *images = bc_get(manifest, "images");
            for (size_t i = 0; i < json_object_array_length(images); i++) {
                json_object *im = json_object_array_get_idx(images, i);
                char offset[32];
                snprintf(offset, sizeof offset, "0x%llx",
                         (unsigned long long)bc_num(im, "address"));
                arg(args, offset);
                arg(args, bc_str(im, "file"));
            }
        }
        json_object_object_add(r, "communication", c);
        text(r, "boot_mode",
             "Coloque o ESP32 no bootloader conforme sua placa; DTR/RTS/reset automático não são "
             "acionados pelo BPCB.");
    } else {
        json_object *images = bc_get(manifest, "images"),
                    *q = openocd_request(request, manifest, json_object_array_get_idx(images, 0));
        json_object *p = bc_openocd_plan(q, e);
        json_object_put(q);
        if (!p)
            goto bad;
        json_object_object_add(r, "openocd", p);
        text(r, "stm32_multi_image",
             json_object_array_length(images) > 1
                 ? "BINs serão unidos em Intel HEX endereçado, sem preencher lacunas; uma única "
                   "operação OpenOCD."
                 : "Imagem única.");
    }
    json_object_object_add(r, "argv", args);
    args = NULL;
    char *digest = g_compute_checksum_for_string(
        G_CHECKSUM_SHA256, json_object_to_json_string_ext(r, JSON_C_TO_STRING_PLAIN), -1);
    text(r, "sha256", digest);
    g_free(digest);
    text(r, "scope",
         "Prévia local. Gravação pode apagar setores e não é atômica. Cancelar não restaura "
         "firmware. Não altera eFuses, option bytes ou proteções por comandos próprios; vetores "
         "FPGA podem conter operações permanentes.");
    return r;
bad:
    json_object_put(args);
    json_object_put(r);
    return NULL;
}

static int hex_record(FILE *f, unsigned type, unsigned address, const uint8_t *bytes, unsigned n) {
    unsigned sum = n + (address >> 8) + (address & 255) + type;
    if (fprintf(f, ":%02X%04X%02X", n, address, type) < 0)
        return -1;
    for (unsigned i = 0; i < n; i++) {
        sum += bytes[i];
        if (fprintf(f, "%02X", bytes[i]) < 0)
            return -1;
    }
    return fprintf(f, "%02X\n", (-sum) & 255u) < 0 ? -1 : 0;
}

static int merge_stm(json_object *images, const char *destination, bc_error *e) {
    FILE *out = fopen(destination, "wb");
    if (!out)
        return bc_fail(e, "Criar imagem HEX privada.");
    unsigned high = UINT32_MAX;
    int rc = 0;
    for (size_t i = 0; i < json_object_array_length(images) && !rc; i++) {
        json_object *image = json_object_array_get_idx(images, i);
        FILE *in = fopen(bc_str(image, "file"), "rb");
        if (!in) {
            rc = -1;
            break;
        }
        uint64_t addr = (uint64_t)bc_num(image, "address");
        uint8_t b[32];
        size_t n;
        while ((n = fread(b, 1, sizeof b, in))) {
            unsigned upper = (unsigned)(addr >> 16);
            if (upper != high) {
                uint8_t u[] = {(uint8_t)(upper >> 8), (uint8_t)upper};
                rc = hex_record(out, 4, 0, u, 2);
                high = upper;
            }
            size_t first = n;
            if ((addr & 65535u) + n > 65536u)
                first = (size_t)(65536u - (addr & 65535u));
            if (!rc)
                rc = hex_record(out, 0, (unsigned)(addr & 65535u), b, (unsigned)first);
            addr += first;
            if (first < n) {
                unsigned upper2 = (unsigned)(addr >> 16);
                uint8_t u[] = {(uint8_t)(upper2 >> 8), (uint8_t)upper2};
                if (!rc)
                    rc = hex_record(out, 4, 0, u, 2);
                high = upper2;
                if (!rc)
                    rc = hex_record(out, 0, (unsigned)(addr & 65535u), b + first,
                                    (unsigned)(n - first));
                addr += n - first;
            }
            if (rc)
                break;
        }
        if (ferror(in))
            rc = -1;
        fclose(in);
    }
    if (!rc)
        rc = hex_record(out, 1, 0, NULL, 0);
    if (fclose(out))
        rc = -1;
    return rc ? bc_fail(e, "Falha criando Intel HEX multiarquivo.") : 0;
}

/* Copy and hash in one bounded pass. A growing/truncated source is rejected;
   the reviewed image is never loaded as a single allocation. */
static int snapshot_image(const char *source, const char *destination, uint64_t expected,
                          const char *sha, int (*cancel)(void *), void *context, bc_error *e) {
    int input = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW), output = -1, rc = -1;
    struct stat initial, final;
    GChecksum *checksum = NULL;
    if (input < 0 || fstat(input, &initial) < 0 || !S_ISREG(initial.st_mode) ||
        initial.st_size < 0 || (uint64_t)initial.st_size != expected ||
        expected > 16 * 1024 * 1024) {
        bc_fail(e, "Imagem mudou antes da cópia privada.");
        goto done;
    }
    output = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (output < 0) {
        bc_fail(e, "Criar snapshot privado: %s", strerror(errno));
        goto done;
    }
    checksum = g_checksum_new(G_CHECKSUM_SHA256);
    uint8_t buffer[65536];
    uint64_t copied = 0;
    while (copied < expected) {
        if (cancel && cancel(context)) {
            bc_fail(e, "Preparação de firmware cancelada.");
            goto done;
        }
        size_t amount =
            expected - copied > sizeof buffer ? sizeof buffer : (size_t)(expected - copied);
        ssize_t n = read(input, buffer, amount);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            bc_fail(e, "Imagem truncada ou leitura falhou durante cópia.");
            goto done;
        }
        g_checksum_update(checksum, buffer, (gssize)n);
        size_t offset = 0;
        while (offset < (size_t)n) {
            ssize_t written = write(output, buffer + offset, (size_t)n - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                bc_fail(e, "Falha escrevendo snapshot privado.");
                goto done;
            }
            offset += (size_t)written;
        }
        copied += (uint64_t)n;
    }
    ssize_t extra;
    do {
        extra = read(input, buffer, 1);
    } while (extra < 0 && errno == EINTR);
    if (extra != 0 || fstat(input, &final) < 0 || final.st_size != initial.st_size ||
        final.st_mtime != initial.st_mtime || final.st_ctime != initial.st_ctime ||
        strcmp(g_checksum_get_string(checksum), sha) || fsync(output) < 0) {
        bc_fail(e, "Imagem mudou ou SHA-256/cópia privada divergiu.");
        goto done;
    }
    rc = 0;
done:
    if (checksum)
        g_checksum_free(checksum);
    if (input >= 0)
        close(input);
    if (output >= 0 && close(output) < 0)
        rc = bc_fail(e, "Falha fechando snapshot privado: %s", strerror(errno));
    if (rc < 0 && output >= 0)
        unlink(destination);
    return rc;
}

json_object *bc_firmware_run(json_object *request, int (*cancel)(void *), void *ctx, bc_error *e) {
    json_object *plan = bc_firmware_plan(request, e);
    if (!plan)
        return NULL;
    const char *action = bc_str(request, "action");
    if (!json_object_is_type(bc_get(request, "trusted_tools"), json_type_boolean) ||
        !json_object_get_boolean(bc_get(request, "trusted_tools"))) {
        bc_fail(e, "Confirme a origem confiável da ferramenta de firmware.");
        goto bad;
    }
    if (strcmp(action, "check") &&
        strcmp(bc_str(request, "expected_sha256"), bc_str(plan, "sha256"))) {
        bc_fail(e, "Prévia de firmware ausente/obsoleta.");
        goto bad;
    }
    if (!strcmp(action, "program") &&
        (!json_object_is_type(bc_get(request, "program_confirmed"), json_type_boolean) ||
         !json_object_get_boolean(bc_get(request, "program_confirmed")))) {
        bc_fail(e, "Confirme explicitamente a gravação do pacote.");
        goto bad;
    }
    json_object *manifest = bc_get(plan, "manifest"), *images = bc_get(manifest, "images"),
                *result = NULL;
    int esp = !strcmp(bc_str(manifest, "backend"), "esptool");
    if (!strcmp(action, "check")) {
        if (esp)
            result = bc_external_process(bc_get(plan, "argv"), 5, cancel, ctx, e);
        else {
            json_object *q =
                openocd_request(request, manifest, json_object_array_get_idx(images, 0));
            result = bc_openocd_run(q, cancel, ctx, e);
            json_object_put(q);
        }
        if (result)
            json_object_object_add(result, "plan", plan);
        else
            json_object_put(plan);
        return result;
    }
    GError *error = NULL;
    char *dir = g_dir_make_tmp("bpcb-firmware-XXXXXX", &error);
    if (!dir) {
        bc_fail(e, "Criar cópia privada: %s", error ? error->message : "");
        g_clear_error(&error);
        goto bad;
    }
    json_object *private_manifest = bc_clone(manifest),
                *private_images = bc_get(private_manifest, "images");
    char paths[17][4096] = {{0}};
    int lock = -1, port_lock = -1;
    for (size_t i = 0; i < json_object_array_length(images); i++) {
        if (cancel && cancel(ctx)) {
            bc_fail(e, "Preparação cancelada.");
            goto cleanup;
        }
        json_object *image = json_object_array_get_idx(images, i);
        snprintf(paths[i], sizeof paths[i], "%s/image-%u.%s", dir, (unsigned)i,
                 bc_str(image, "format"));
        if (snapshot_image(bc_str(image, "file"), paths[i], (uint64_t)bc_num(image, "bytes"),
                           bc_str(image, "sha256"), cancel, ctx, e) < 0)
            goto cleanup;
        text(json_object_array_get_idx(private_images, i), "file", paths[i]);
    }
    char current_tool[65];
    if (bc_sha_file(bc_str(request, "executable"), current_tool, e) < 0 ||
        strcmp(current_tool, bc_str(plan, "tool_sha256"))) {
        bc_fail(e, "Ferramenta mudou depois da prévia.");
        goto cleanup;
    }
    if (esp) {
        json_object *config = bc_get(plan, "communication");
        if (!json_object_get_boolean(bc_get(config, "connections_checked")) ||
            bc_comm_serial_probe(config, e) < 0) {
            if (!e->text[0])
                bc_fail(e, "Confira conexões e identidade da porta UART.");
            goto cleanup;
        }
        lock = open(bc_bench_lock_path(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
            bc_fail(e, "Bancada ocupada.");
            goto cleanup;
        }
        port_lock = bc_comm_lock(bc_str(config, "device"), e);
        if (port_lock < 0)
            goto cleanup;
        json_object *q = bc_clone(request);
        json_object_object_add(q, "manifest", bc_clone(private_manifest));
        json_object *private_plan = bc_firmware_plan(q, e);
        json_object_put(q);
        if (!private_plan)
            goto cleanup;
        json_object *check = json_object_new_array();
        arg(check, bc_str(request, "executable"));
        arg(check, "version");
        json_object *info = bc_external_process(check, 5, cancel, ctx, e);
        json_object_put(check);
        if (!info || !json_object_get_boolean(bc_get(info, "ok")) ||
            (!strstr(bc_str(info, "log"), "esptool v5.") &&
             !strstr(bc_str(info, "log"), "esptool.py v5."))) {
            json_object_put(info);
            json_object_put(private_plan);
            bc_fail(e, "Selecione esptool v5 compatível com argumentos revisados.");
            goto cleanup;
        }
        json_object_put(info);
        result = bc_external_process(bc_get(private_plan, "argv"),
                                     (int)bc_num(plan, "timeout_seconds"), cancel, ctx, e);
        json_object_put(private_plan);
    } else {
        json_object *image = json_object_array_get_idx(private_images, 0), *merged = NULL;
        if (json_object_array_length(private_images) > 1) {
            snprintf(paths[16], sizeof paths[16], "%s/combined.hex", dir);
            if (merge_stm(private_images, paths[16], e) < 0)
                goto cleanup;
            merged = json_object_new_object();
            text(merged, "file", paths[16]);
            char sha[65];
            if (bc_sha_file(paths[16], sha, e) < 0) {
                json_object_put(merged);
                goto cleanup;
            }
            text(merged, "sha256", sha);
            image = merged;
        }
        json_object *q = openocd_request(request, manifest, image);
        result = bc_openocd_run(q, cancel, ctx, e);
        json_object_put(q);
        json_object_put(merged);
    }
cleanup:
    if (port_lock >= 0)
        bc_comm_unlock(port_lock);
    if (lock >= 0)
        close(lock);
    for (unsigned i = 0; i < 17; i++)
        if (*paths[i])
            unlink(paths[i]);
    rmdir(dir);
    g_free(dir);
    json_object_put(private_manifest);
    if (result) {
        json_object_object_add(result, "plan", plan);
        json_object_object_add(result, "images",
                               json_object_new_int64((int64_t)json_object_array_length(images)));
        return result;
    }
bad:
    json_object_put(plan);
    return NULL;
}
