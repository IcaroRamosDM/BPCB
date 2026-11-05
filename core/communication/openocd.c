#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "../lab.h"
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/file.h>
#include <sys/wait.h>
#endif
#define LOG_LIMIT 65536

static void text(json_object *o, const char *key, const char *s) {
    json_object_object_add(o, key, json_object_new_string(s));
}

json_object *bc_openocd_targets(void) {
    json_object *a = json_object_new_array();
    const char *stm[] = {"stm32f1x", "stm32f2x", "stm32f3x", "stm32f4x", "stm32f7x",
                         "stm32h7x", "stm32l1",  "stm32l4x", "stm32g4x"};
    const char *esp[] = {"esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6", "esp32h2"};
    const char *fpga[] = {"xilinx-xc6s", "xilinx-xc7", "xilinx-xcu"};
    for (unsigned family = 0; family < 3; family++) {
        unsigned count = family == 0   ? sizeof stm / sizeof *stm
                         : family == 1 ? sizeof esp / sizeof *esp
                                       : sizeof fpga / sizeof *fpga;
        for (unsigned i = 0; i < count; i++) {
            const char *id = family == 0 ? stm[i] : family == 1 ? esp[i] : fpga[i];
            json_object *o = json_object_new_object();
            char script[128];
            snprintf(script, sizeof script, "%s/%s.cfg", family == 2 ? "cpld" : "target", id);
            text(o, "id", id);
            text(o, "family", family == 0 ? "stm32" : family == 1 ? "esp32" : "fpga");
            text(o, "script", script);
            text(o, "formats", family == 0 ? "elf,hex,bin" : family == 1 ? "bin" : "svf,xsvf");
            json_object_array_add(a, o);
        }
    }
    return a;
}

static int absolute(const char *s) {
#ifdef _WIN32
    return s && ((g_ascii_isalpha(s[0]) && s[1] == ':' && (s[2] == '/' || s[2] == '\\')) ||
                 g_str_has_prefix(s, "\\\\"));
#else
    return s && s[0] == '/';
#endif
}

static int regular(const char *path, off_t maximum, bc_error *e) {
    struct stat st;
    if (!absolute(path) || lstat(path, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 1 ||
        st.st_size > maximum)
        return bc_fail(e, "Arquivo absoluto, regular e sem link obrigatório: %s", path);
    return 0;
}

static char *tcl_path(const char *s) {
    size_t n = strlen(s);
    if (n > 4095)
        return NULL;
    char *out = malloc(n * 2 + 80);
    if (!out)
        return NULL;
    strcpy(out, "[encoding convertfrom utf-8 [binary format H* ");
    size_t at = strlen(out);
    for (size_t i = 0; i < n; i++)
        at += (size_t)sprintf(out + at, "%02x", (unsigned char)s[i]);
    strcpy(out + at, "]]");
    return out;
}

static json_object *target(json_object *request, bc_error *e) {
    json_object *t = bc_get(request, "target"), *copy = NULL;
    if (json_object_is_type(t, json_type_string)) {
        json_object *a = bc_openocd_targets();
        for (size_t i = 0; i < json_object_array_length(a); i++) {
            json_object *r = json_object_array_get_idx(a, i);
            if (!strcmp(json_object_get_string(t), bc_str(r, "id")))
                copy = bc_clone(r);
        }
        json_object_put(a);
    } else if (json_object_is_type(t, json_type_object))
        copy = bc_clone(t);
    if (!copy) {
        bc_fail(e, "Selecione a família exata do alvo JTAG.");
        return NULL;
    }
    json_object_object_foreach(copy, key, value) {
        (void)key;
        if (json_object_is_type(value, json_type_string) &&
            (size_t)json_object_get_string_len(value) != strlen(json_object_get_string(value))) {
            json_object_put(copy);
            bc_fail(e, "Perfil de alvo contém NUL embutido.");
            return NULL;
        }
    }
    const char *family = bc_str(copy, "family"), *script = bc_str(copy, "script");
    int ok = !strcmp(family, "stm32") || !strcmp(family, "esp32") || !strcmp(family, "fpga");
    if (strlen(script) > 160 ||
        (!g_str_has_prefix(script, "target/") && !g_str_has_prefix(script, "cpld/")) ||
        !g_str_has_suffix(script, ".cfg") || strstr(script, ".."))
        ok = 0;
    for (const char *p = script; *p; p++)
        if (!g_ascii_isalnum(*p) && !strchr("/_-.", *p))
            ok = 0;
    if (!ok) {
        json_object_put(copy);
        bc_fail(e, "Perfil de alvo inválido. Use família stm32/esp32/fpga e um script target/ ou "
                   "cpld/ da instalação confiável.");
        return NULL;
    }
    return copy;
}

json_object *bc_openocd_plan(json_object *request, bc_error *e) {
    if (!json_object_is_type(request, json_type_object)) {
        bc_fail(e, "Solicitação OpenOCD inválida.");
        return NULL;
    }
    const char *allowed[] = {
        "executable",    "scripts",           "target",      "action", "firmware",
        "address",       "timeout_seconds",   "speed_khz",   "serial", "expected_sha256",
        "trusted_tools", "program_confirmed", "usb_location"};
    json_object_object_foreach(request, field, item) {
        (void)item;
        int found = 0;
        for (unsigned i = 0; i < sizeof allowed / sizeof *allowed; i++)
            if (!strcmp(field, allowed[i]))
                found = 1;
        if (!found) {
            bc_fail(e, "Campo OpenOCD desconhecido: %s", field);
            return NULL;
        }
    }
    json_object_object_foreach(request, key, value) {
        if (json_object_is_type(value, json_type_string) &&
            (size_t)json_object_get_string_len(value) != strlen(json_object_get_string(value))) {
            bc_fail(e, "OpenOCD: NUL embutido em %s.", key);
            return NULL;
        }
    }
    const char *exe = bc_str(request, "executable"), *scripts = bc_str(request, "scripts"),
               *action = bc_str(request, "action"), *file = bc_str(request, "firmware"),
               *serial = bc_str(request, "serial");
    const char *location = bc_str(request, "usb_location");
    if (strlen(location) > 80) {
        bc_fail(e, "Localização USB muito longa.");
        return NULL;
    }
    for (const char *c = location; *c; c++)
        if (!g_ascii_isdigit(*c) && *c != '-' && *c != '.') {
            bc_fail(e, "Localização USB inválida.");
            return NULL;
        }
    if (regular(exe, 128 * 1024 * 1024, e) < 0)
        return NULL;
    struct stat st;
    if (!absolute(scripts) || lstat(scripts, &st) < 0 || !S_ISDIR(st.st_mode)) {
        bc_fail(e, "Selecione a pasta scripts da instalação OpenOCD confiável.");
        return NULL;
    }
    if (strcmp(action, "check") && strcmp(action, "identify") && strcmp(action, "verify") &&
        strcmp(action, "program")) {
        bc_fail(e, "Ação OpenOCD inválida.");
        return NULL;
    }
    for (const char *p = serial; *p; p++)
        if (!g_ascii_isalnum(*p) && !strchr("._-", *p)) {
            bc_fail(e, "Número de série OpenOCD inválido.");
            return NULL;
        }
    if (strlen(serial) > 128) {
        bc_fail(e, "Número de série muito longo.");
        return NULL;
    }
    int timeout = bc_get(request, "timeout_seconds")
                      ? json_object_get_int(bc_get(request, "timeout_seconds"))
                      : 120;
    int speed =
        bc_get(request, "speed_khz") ? json_object_get_int(bc_get(request, "speed_khz")) : 1875;
    if ((bc_get(request, "timeout_seconds") &&
         !json_object_is_type(bc_get(request, "timeout_seconds"), json_type_int)) ||
        timeout < 1 || timeout > 600 ||
        (bc_get(request, "speed_khz") &&
         !json_object_is_type(bc_get(request, "speed_khz"), json_type_int)) ||
        speed < 1 || speed > 30000) {
        bc_fail(e, "OpenOCD: timeout 1..600 s; clock 1..30000 kHz.");
        return NULL;
    }
    json_object *result = json_object_new_object(), *args = json_object_new_array(), *t = NULL;
    char command[1024], sha[65] = "";
#define ARG(s) json_object_array_add(args, json_object_new_string(s))
    ARG(exe);
    ARG("-s");
    ARG(scripts);
    ARG("-c");
    ARG("gdb_port disabled; tcl_port disabled; telnet_port disabled");
    if (!strcmp(action, "check")) {
        ARG("-c");
        ARG("echo [adapter list]; shutdown");
        goto ready;
    }
    t = target(request, e);
    if (!t)
        goto bad;
    char scriptpath[4096];
    if (bc_path(scriptpath, sizeof scriptpath, scripts, bc_str(t, "script"), e) < 0 ||
        regular(scriptpath, 1024 * 1024, e) < 0)
        goto bad;
    ARG("-c");
    ARG("adapter driver ch347; adapter usb vid_pid 0x1a86 0x55dd; transport select jtag");
    if (*serial) {
        ARG("-c");
        snprintf(command, sizeof command, "adapter serial %s", serial);
        ARG(command);
    }
    if (*location) {
        ARG("-c");
        snprintf(command, sizeof command, "adapter usb location %s", location);
        ARG(command);
    }
    ARG("-c");
    snprintf(command, sizeof command, "adapter speed %d; reset_config none", speed);
    ARG(command);
    ARG("-c");
    snprintf(command, sizeof command, "source [find %s]", bc_str(t, "script"));
    ARG(command);
    if (!strcmp(action, "identify")) {
        ARG("-c");
        ARG("init; scan_chain; shutdown");
        goto ready;
    }
    if (regular(file, 64 * 1024 * 1024, e) < 0 || bc_sha_file(file, sha, e) < 0)
        goto bad;
    const char *ext = strrchr(file, '.');
    if (!ext) {
        bc_fail(e, "Firmware: use ELF, HEX, BIN, SVF ou XSVF.");
        goto bad;
    }
    int bin = !g_ascii_strcasecmp(ext, ".bin"), elf = !g_ascii_strcasecmp(ext, ".elf"),
        hex = !g_ascii_strcasecmp(ext, ".hex"), svf = !g_ascii_strcasecmp(ext, ".svf"),
        xsvf = !g_ascii_strcasecmp(ext, ".xsvf");
    const char *family = bc_str(t, "family");
    if ((!strcmp(family, "stm32") && !bin && !elf && !hex) || (!strcmp(family, "esp32") && !bin) ||
        (!strcmp(family, "fpga") && !svf && !xsvf)) {
        bc_fail(e, "Formato incompatível com a família escolhida.");
        goto bad;
    }
    int64_t address =
        bc_get(request, "address") ? json_object_get_int64(bc_get(request, "address")) : 0;
    if (bin && (!json_object_is_type(bc_get(request, "address"), json_type_int) || address < 0 ||
                address > UINT32_MAX || address % 4)) {
        bc_fail(e, "BIN requer endereço explícito de 32 bits, alinhado a 4 bytes.");
        goto bad;
    }
    if (bin && ((!strcmp(family, "stm32") && (address < 0x08000000 || address >= 0x10000000)) ||
                (!strcmp(family, "esp32") && address >= 0x1000000))) {
        bc_fail(
            e, "Endereço fora da região de flash esperada para a família. Confira o mapa do alvo.");
        goto bad;
    }
    char *quoted = tcl_path(file);
    if (!quoted)
        goto bad;
    char *program = g_malloc(strlen(quoted) + 512);
    if (svf || xsvf) {
        if (!strcmp(action, "verify")) {
            free(quoted);
            g_free(program);
            bc_fail(e, "SVF/XSVF executam vetores que podem programar a FPGA; não oferecem uma "
                       "verificação independente genérica.");
            goto bad;
        }
        snprintf(program, strlen(quoted) + 512, "init; %s %s%s quiet; shutdown",
                 svf ? "svf" : "xsvf", xsvf ? "plain " : "", quoted);
    } else if (!strcmp(family, "esp32")) {
        if (!strcmp(action, "program"))
            snprintf(program, strlen(quoted) + 512, "program_esp %s 0x%llx verify exit", quoted,
                     (unsigned long long)address);
        else
            snprintf(program, strlen(quoted) + 512,
                     "init; reset halt; esp verify_bank_hash 0 %s 0x%llx; shutdown", quoted,
                     (unsigned long long)address);
    } else if (!strcmp(action, "program"))
        snprintf(program, strlen(quoted) + 512,
                 bin ? "program %s verify exit 0x%llx" : "program %s verify exit", quoted,
                 (unsigned long long)address);
    else
        snprintf(program, strlen(quoted) + 512,
                 bin ? "init; reset halt; verify_image %s 0x%llx bin; shutdown"
                     : "init; reset halt; verify_image %s; shutdown",
                 quoted, (unsigned long long)address);
    free(quoted);
    ARG("-c");
    ARG(program);
    g_free(program);
ready:
    json_object_object_add(result, "argv", args);
    if (t)
        json_object_object_add(result, "target", t);
    text(result, "action", action);
    text(result, "serial", serial);
    text(result, "usb_location", location);
    text(result, "sha256", sha);
    json_object_object_add(result, "timeout_seconds", json_object_new_int(timeout));
    text(result, "scope",
         !strcmp(action, "program")
             ? "A gravação pode apagar flash ou configurar a FPGA. Não altera eFuses, proteção ou "
               "chaves. O arquivo e o alvo devem corresponder à placa. Cancelar não desfaz uma "
               "gravação parcial."
             : "Consulta/verificação JTAG pode reiniciar/parar o alvo; não comprova condições "
               "elétricas. Scripts e executável OpenOCD são programas locais confiáveis.");
    return result;
bad:
    json_object_put(t);
    json_object_put(args);
    json_object_put(result);
    return NULL;
#undef ARG
}

json_object *bc_openocd_run(json_object *request, int (*cancel)(void *), void *ctx, bc_error *e) {
    json_object *effective = NULL;
    json_object *plan = bc_openocd_plan(request, e);
    if (!plan)
        return NULL;
    const char *action = bc_str(plan, "action");
    if (!json_object_is_type(bc_get(request, "trusted_tools"), json_type_boolean) ||
        !json_object_get_boolean(bc_get(request, "trusted_tools"))) {
        bc_fail(e, "Confirme a origem confiável do OpenOCD e de seus scripts antes de executar.");
        goto bad;
    }
    if (*bc_str(request, "expected_sha256") &&
        strcmp(bc_str(request, "expected_sha256"), bc_str(plan, "sha256"))) {
        bc_fail(e, "Arquivo difere do SHA-256 da prévia; prepare novamente.");
        goto bad;
    }
    if (!strcmp(action, "program") &&
        (!json_object_is_type(bc_get(request, "program_confirmed"), json_type_boolean) ||
         !json_object_get_boolean(bc_get(request, "program_confirmed")) ||
         strcmp(bc_str(request, "expected_sha256"), bc_str(plan, "sha256")))) {
        bc_fail(e, "Confirme a gravação com o SHA-256 da prévia atual.");
        goto bad;
    }
    /* Feature detection never opens the adapter. Reject older builds before JTAG. */
    if (strcmp(action, "check")) {
        json_object *check = bc_clone(request);
        text(check, "action", "check");
        json_object *cp = bc_openocd_plan(check, e);
        json_object_put(check);
        json_object *info = cp ? bc_external_process(bc_get(cp, "argv"), 5, cancel, ctx, e) : NULL;
        json_object_put(cp);
        if (!info)
            goto bad;
        if (bc_num(info, "exit_code") || !strstr(bc_str(info, "log"), "ch347")) {
            json_object_put(info);
            bc_fail(e, "Este OpenOCD não oferece o driver ch347. Selecione uma compilação "
                       "compatível; para ESP32 use também os scripts/driver flash da Espressif.");
            goto bad;
        }
        json_object_put(info);
    }
    if (strcmp(action, "check")) {
        json_object *list = bc_comm_list(cancel, ctx, e);
        if (!list)
            goto bad;
        unsigned matches = 0;
        const char *wanted = bc_str(request, "serial"), *location = bc_str(request, "usb_location");
        char selected_serial[256] = "", selected_location[100] = "";
        for (size_t i = 0; i < json_object_array_length(list); i++) {
            json_object *d = json_object_array_get_idx(list, i);
            const char *device = bc_str(d, "device"),
                       *loc = g_str_has_prefix(device, "usb:1a86:55dd:")
                                  ? device + strlen("usb:1a86:55dd:")
                                  : "";
            if (!strcmp(bc_str(d, "adapter"), "ch347") && bc_get(d, "mode") &&
                bc_num(d, "mode") == 3 && strcmp(bc_str(d, "bus"), "uart") &&
                (!*wanted || !strcmp(wanted, bc_str(d, "serial"))) &&
                (!*location || !strcmp(location, loc))) {
                matches++;
                snprintf(selected_serial, sizeof selected_serial, "%s", bc_str(d, "serial"));
                if (*loc && !strchr(loc, ':'))
                    snprintf(selected_location, sizeof selected_location, "%s", loc);
            }
        }
        json_object_put(list);
        if (matches != 1 || (!*selected_serial && !*selected_location)) {
            bc_fail(e,
                    "OpenOCD exige exatamente um CH347T M3 identificado por série ou localização "
                    "USB estável. Atualize a seleção; nenhum substituto foi escolhido.");
            goto bad;
        }
        effective = bc_clone(request);
        if (*selected_serial)
            text(effective, "serial", selected_serial);
        if (*selected_location)
            text(effective, "usb_location", selected_location);
        json_object *pinned = bc_openocd_plan(effective, e);
        if (!pinned)
            goto bad;
        if (strcmp(bc_str(pinned, "sha256"), bc_str(plan, "sha256"))) {
            json_object_put(pinned);
            bc_fail(e, "Firmware mudou durante a preparação; crie uma nova prévia.");
            goto bad;
        }
        json_object_put(plan);
        plan = pinned;
        action = bc_str(plan, "action");
    }
    int lock = -1;
    if (strcmp(action, "check")) {
        lock = open(bc_bench_lock_path(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
            if (lock >= 0)
                close(lock);
            bc_fail(e, "Outra operação está usando a bancada.");
            goto bad;
        }
    }
    json_object *executed = plan;
    char *private_dir = NULL;
    char snapshot[4096] = "";
    if (!strcmp(action, "program") || !strcmp(action, "verify")) {
        GError *error = NULL;
        private_dir = g_dir_make_tmp("bpcb-firmware-XXXXXX", &error);
        if (!private_dir) {
            bc_fail(e, "Criar cópia privada de firmware: %s", error ? error->message : "falha");
            g_clear_error(&error);
            if (lock >= 0)
                close(lock);
            goto bad;
        }
        const char *source = bc_str(request, "firmware"), *extension = strrchr(source, '.');
        snprintf(snapshot, sizeof snapshot, "%s/firmware%s", private_dir,
                 extension ? extension : "");
        int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW),
            out = open(snapshot, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat st;
        int copied = in >= 0 && out >= 0 && fstat(in, &st) == 0 && S_ISREG(st.st_mode) &&
                     st.st_size > 0 && st.st_size <= 64 * 1024 * 1024;
        uint64_t total = 0;
        while (copied) {
            uint8_t bytes[32768];
            ssize_t n = read(in, bytes, sizeof bytes);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0) {
                copied = 0;
                break;
            }
            if (!n)
                break;
            total += (uint64_t)n;
            if (total > 64 * 1024 * 1024) {
                copied = 0;
                break;
            }
            size_t at = 0;
            while (at < (size_t)n) {
                ssize_t w = write(out, bytes + at, (size_t)n - at);
                if (w < 0 && errno == EINTR)
                    continue;
                if (w <= 0) {
                    copied = 0;
                    break;
                }
                at += (size_t)w;
            }
        }
        if (in >= 0)
            close(in);
        if (out >= 0) {
            if (fsync(out) < 0)
                copied = 0;
            if (close(out) < 0)
                copied = 0;
        }
        char sha[65];
        if (!copied || bc_sha_file(snapshot, sha, e) < 0 || strcmp(sha, bc_str(plan, "sha256"))) {
            bc_fail(e, "Firmware mudou durante a preparação; crie uma nova prévia.");
            unlink(snapshot);
            rmdir(private_dir);
            g_free(private_dir);
            if (lock >= 0)
                close(lock);
            goto bad;
        }
        json_object *copy = bc_clone(effective ? effective : request);
        text(copy, "firmware", snapshot);
        executed = bc_openocd_plan(copy, e);
        json_object_put(copy);
        if (!executed) {
            unlink(snapshot);
            rmdir(private_dir);
            g_free(private_dir);
            if (lock >= 0)
                close(lock);
            goto bad;
        }
    }
    json_object *r = bc_external_process(bc_get(executed, "argv"),
                                         (int)bc_num(plan, "timeout_seconds"), cancel, ctx, e);
    if (executed != plan)
        json_object_put(executed);
    if (private_dir) {
        unlink(snapshot);
        rmdir(private_dir);
        g_free(private_dir);
    }
    if (lock >= 0)
        close(lock);
    if (r && !strcmp(action, "check")) {
        int supported = strstr(bc_str(r, "log"), "ch347") != NULL;
        json_object_object_add(r, "ch347_supported", json_object_new_boolean(supported));
        if (!supported)
            json_object_object_add(r, "ok", json_object_new_boolean(0));
        text(r, "message",
             supported ? "Driver CH347 disponível nesta instalação."
                       : "Esta instalação OpenOCD não contém o driver CH347.");
    }
    if (r)
        json_object_object_add(r, "plan", plan);
    else
        json_object_put(plan);
    json_object_put(effective);
    return r;
bad:
    json_object_put(effective);
    json_object_put(plan);
    return NULL;
}
