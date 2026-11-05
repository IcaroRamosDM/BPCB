#define _POSIX_C_SOURCE 200809L
#include "communication/internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "TOOLS FAIL %d: %s\n", __LINE__, #x);                                  \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static int stopped(void *p) {
    return *(int *)p;
}

typedef struct {
    int nack;
    unsigned sent, read;
} fake;

static int write_packet(void *ctx, const uint8_t *b, size_t n, unsigned timeout, bc_error *e) {
    fake *f = ctx;
    (void)e;
    CHECK(timeout > 0);
    f->sent++;
    if (n == 4 && b[0] == 0xaa && b[1] == 0x80)
        f->nack = b[2] == 0x13;
    return (int)n;
}

static int read_packet(void *ctx, uint8_t *b, size_t n, unsigned timeout, bc_error *e) {
    fake *f = ctx;
    (void)e;
    CHECK(timeout > 0);
    memset(b, f->nack ? 128 : 0, n);
    f->read++;
    return (int)n;
}

static int timeout_packet(void *ctx, uint8_t *b, size_t n, unsigned timeout, bc_error *e) {
    (void)ctx;
    (void)b;
    (void)n;
    (void)timeout;
    errno = ETIMEDOUT;
    return bc_fail(e, "idle");
}

static int progress(void *ctx, json_object *o) {
    unsigned *n = ctx;
    (*n)++;
    CHECK(bc_num(o, "address") >= 8 && bc_num(o, "address") <= 119);
    return 0;
}

static void profiles(void) {
    char root[] = "/tmp/bpcb-profiles-XXXXXX";
    CHECK(mkdtemp(root));
    bc_error e = {{0}};
    json_object *a = bc_comm_profiles(root, &e);
    CHECK(a && json_object_array_length(a) == 0);
    json_object_put(a);
    json_object *c =
        json_tokener_parse("{\"adapter\":\"serial\",\"bus\":\"uart\",\"device\":\"/dev/"
                           "ttyUSB0\",\"connections_checked\":true,\"allow_kernel_detach\":true}");
    CHECK(bc_comm_profile_save(root, "Bancada IoT", c, 0, &e) == 0);
    CHECK(bc_comm_profile_save(root, "Bancada IoT", c, 0, &e) < 0);
    CHECK(bc_comm_profile_save(root, "Bancada IoT", c, 1, &e) == 0);
    a = bc_comm_profiles(root, &e);
    CHECK(a && json_object_array_length(a) == 1);
    json_object *config = bc_get(json_object_array_get_idx(a, 0), "config");
    CHECK(!json_object_get_boolean(bc_get(config, "connections_checked")) &&
          !json_object_get_boolean(bc_get(config, "allow_kernel_detach")));
    CHECK(bc_comm_profiles_validate(a, &e) == 0);
    json_object_put(a);
    const char *bad[] = {"", " a", "a ", "a/b", "a\\b", "a\n", "a\t"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(bc_comm_profile_save(root, bad[i], c, 0, &e) < 0);
    for (unsigned i = 0; i < 64; i++) {
        char name[32];
        snprintf(name, sizeof name, "perfil-%u", i);
        CHECK(bc_comm_profile_save(root, name, c, 0, &e) == (i == 63 ? -1 : 0));
    }
    CHECK(bc_comm_profile_delete(root, "Bancada IoT", &e) == 0);
    CHECK(bc_comm_profile_delete(root, "Bancada IoT", &e) < 0);
    char path[4096];
    snprintf(path, sizeof path, "%s/communication-profiles.json", root);
    CHECK(unlink(path) == 0);
    CHECK(symlink("/etc/passwd", path) == 0);
    CHECK(!bc_comm_profiles(root, &e));
    CHECK(bc_comm_profile_save(root, "new", c, 0, &e) < 0);
    CHECK(unlink(path) == 0);
    a = json_tokener_parse("[{\"name\":\"bad\"}]");
    CHECK(bc_comm_profiles_validate(a, &e) < 0);
    json_object_put(a);
    a = json_tokener_parse("[{\"name\":\"A\\u0000B\",\"config\":{}}]");
    CHECK(bc_comm_profiles_validate(a, &e) < 0);
    json_object_put(a);
    snprintf(path, sizeof path, "%s/storage.lock", root);
    unlink(path);
    rmdir(root);
    json_object_put(c);
}

static void backup_profiles(void) {
    bc_error e = {{0}};
    char root[] = "/tmp/bpcb-profiles-backup-XXXXXX";
    CHECK(mkdtemp(root));
    char target[4096], backup[4096];
    CHECK(bc_path(target, sizeof target, root, "target", &e) == 0);
    CHECK(bc_path(backup, sizeof backup, root, "backup.bpcbbackup", &e) == 0);
    CHECK(bc_boot(root, "", &e) == 0 && bc_boot(target, "", &e) == 0);
    json_object *c = json_tokener_parse("{\"adapter\":\"ch347\",\"bus\":\"i2c\",\"ch347_mode\":3}");
    CHECK(bc_comm_profile_save(root, "Waveshare", c, 0, &e) == 0);
    json_object_put(c);
    int result = bc_backup_create(root, backup, 0, &e);
    if (result)
        fprintf(stderr, "BACKUP: %s\n", e.text);
    CHECK(result == 0);
    json_object *preview = bc_backup_inspect(target, backup, &e);
    CHECK(preview);
    CHECK(bc_backup_restore(target, backup, bc_str(preview, "token"), 0, &e) == 0);
    json_object_put(preview);
    json_object *a = bc_comm_profiles(target, &e);
    CHECK(a && json_object_array_length(a) == 1 &&
          !strcmp(bc_str(json_object_array_get_idx(a, 0), "name"), "Waveshare"));
    json_object_put(a);
}

static void formats(void) {
    bc_error e = {{0}};
    uint8_t all[256], decoded[256];
    for (unsigned i = 0; i < 256; i++)
        all[i] = (uint8_t)i;
    char *hex = bc_comm_format_bytes(all, 256, 1);
    size_t n = 0;
    CHECK(hex && bc_comm_hex(hex, decoded, sizeof decoded, &n, &e) == 0 && n == 256 &&
          !memcmp(all, decoded, 256));
    free(hex);
    char *ascii = bc_comm_format_bytes((const uint8_t *)"A\n\r\t\\\0", 6, 0);
    CHECK(ascii && !strcmp(ascii, "A\\n\\r\\t\\\\\\x00"));
    free(ascii);
    const char *delims[] = {"none", "lf", "cr", "crlf"};
    for (unsigned i = 0; i < 4; i++) {
        json_object *o = bc_comm_text_operation("AT", 0, delims[i], 0, &e);
        CHECK(o);
        CHECK(bc_comm_operation_validate("uart", o, &e) == 0);
        json_object_put(o);
    }
    CHECK(!bc_comm_text_operation("á", 0, "none", 1, &e));
    CHECK(!bc_comm_text_operation("gg", 1, "none", 1, &e));
    CHECK(!bc_comm_text_operation("AT", 0, "invalid", 1, &e));
    CHECK(!bc_comm_text_operation("", 0, "none", 0, &e));
    char *longtext = malloc(BC_COMM_MAX + 2);
    memset(longtext, 'A', BC_COMM_MAX + 1);
    longtext[BC_COMM_MAX + 1] = 0;
    CHECK(!bc_comm_text_operation(longtext, 0, "none", 1, &e));
    longtext[BC_COMM_MAX] = 0;
    CHECK(!bc_comm_text_operation(longtext, 0, "lf", 1, &e));
    free(longtext);
    json_object *o = json_tokener_parse(
        "{\"adapter\":\"serial\",\"bus\":\"uart\",\"device\":\"/dev/a\\u0000suffix\"}");
    CHECK(!bc_comm_config(o, &e));
    json_object_put(o);
    o = json_tokener_parse("{\"write\":\"AA\\u0000BB\",\"read\":0}");
    CHECK(bc_comm_operation_validate("uart", o, &e) < 0);
    json_object_put(o);
}

static void maps(void) {
    bc_error e = {{0}};
    json_object *a = bc_comm_register_maps();
    CHECK(json_object_array_length(a) >= 7);
    for (size_t i = 0; i < json_object_array_length(a); i++) {
        json_object *m = json_object_array_get_idx(a, i);
        CHECK(bc_comm_register_map_validate(m, &e) == 0);
        json_object *rows = bc_get(m, "registers");
        for (size_t j = 0; j < json_object_array_length(rows); j++) {
            json_object *r = json_object_array_get_idx(rows, j);
            if (strcmp(bc_str(r, "access"), "wo")) {
                json_object *o = bc_comm_register_operation(m, j, 0x48, NULL, &e);
                CHECK(o && bc_comm_operation_validate("i2c", o, &e) == 0);
                json_object_put(o);
            } else
                CHECK(!bc_comm_register_operation(m, j, 0x48, NULL, &e));
        }
    }
    json_object *m = json_object_array_get_idx(a, 0);
    CHECK(!bc_comm_register_operation(m, 0, 0x48, "01", &e));
    CHECK(!bc_comm_register_operation(m, 999, 0x48, NULL, &e));
    CHECK(!bc_comm_register_operation(m, 0, 1, NULL, &e));
    json_object *o = bc_comm_register_operation(m, 1, 0x38, "01", &e);
    CHECK(o && !strcmp(bc_str(o, "write"), "01 01") && bc_num(o, "read") == 0);
    json_object_put(o);
    CHECK(!bc_comm_register_operation(m, 1, 0x38, "01 02", &e));
    json_object_put(a);
}

static void scan_and_uart(void) {
    bc_error e = {{0}};
    fake f = {0};
    bc_comm s = {0};
    s.kind = BC_CH341;
    s.config = bc_comm_config(NULL, &e);
    s.io = (bc_comm_io){&f, write_packet, read_packet, NULL, NULL, NULL};
    bc_comm *ptr = &s;
    unsigned count = 0;
    json_object *a = bc_comm_scan(&ptr, 8, 10, progress, &count, &e);
    CHECK(a && count == 3 && json_object_array_length(a) == 3);
    CHECK(json_object_get_boolean(bc_get(json_object_array_get_idx(a, 0), "ack")));
    CHECK(!json_object_get_boolean(bc_get(json_object_array_get_idx(a, 1), "ack")));
    json_object_put(a);
    CHECK(!bc_comm_scan(&ptr, 7, 10, NULL, NULL, &e));
    int stop = 1;
    s.cancel = stopped;
    s.cancel_context = &stop;
    CHECK(!bc_comm_scan(&ptr, 8, 10, NULL, NULL, &e) && errno == ECANCELED);
    json_object_put(s.config);
    s = (bc_comm){0};
    s.kind = BC_SERIAL;
    json_object *raw =
        json_tokener_parse("{\"adapter\":\"serial\",\"bus\":\"uart\",\"device\":\"/dev/ttyTEST\"}");
    s.config = bc_comm_config(raw, &e);
    json_object_put(raw);
    s.io.read = timeout_packet;
    uint8_t byte;
    CHECK(bc_comm_uart_poll(&s, &byte, 1, 50, &e) == 0 && !s.lost);
    CHECK(bc_comm_uart_poll(&s, &byte, 1, 1, &e) < 0);
    s.cancel = stopped;
    s.cancel_context = &stop;
    CHECK(bc_comm_uart_poll(&s, &byte, 1, 50, &e) < 0 && errno == ECANCELED);
    json_object_put(s.config);
}

static void byte_assessment(void) {
    json_object *m =
        json_tokener_parse("{\"byte_comparison\":true,\"value\":4628,\"unit\":\"código\","
                           "\"expected_bytes\":\"12 14\",\"received_bytes\":\"12 14\"}");
    bc_assess(m);
    CHECK(!strcmp(bc_str(m, "verdict"), "pass"));
    json_object_object_add(m, "received_bytes", json_object_new_string("12 15"));
    bc_assess(m);
    CHECK(!strcmp(bc_str(m, "verdict"), "fail"));
    json_object_object_add(m, "mask_bytes", json_object_new_string("FF 00"));
    bc_assess(m);
    CHECK(!strcmp(bc_str(m, "verdict"), "pass"));
    json_object_object_del(m, "mask_bytes");
    json_object_object_add(m, "expected_bytes", json_object_new_string("12 F4"));
    json_object_object_add(m, "received_bytes", json_object_new_string("12 14"));
    json_object_object_add(m, "comparison_bits", json_object_new_int(13));
    bc_assess(m);
    CHECK(!strcmp(bc_str(m, "verdict"), "pass"));
    json_object_object_add(m, "received_bytes", json_object_new_string("GG"));
    bc_assess(m);
    CHECK(!strcmp(bc_str(m, "verdict"), "none"));
    json_object_put(m);
}

int main(void) {
    byte_assessment();
    profiles();
    backup_profiles();
    formats();
    maps();
    scan_and_uart();
    printf("%u checks de ferramentas de comunicação\n", checks);
    return 0;
}
