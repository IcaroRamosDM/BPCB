#define _POSIX_C_SOURCE 200809L
#include "workbench.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/file.h>
#endif
static unsigned checks;
static bc_error e;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "WORKBENCH %d: %s (%s)\n", __LINE__, #x, e.text);                      \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static void text(json_object *o, const char *k, const char *v) {
    json_object_object_add(o, k, json_object_new_string(v));
}

static void num(json_object *o, const char *k, double v) {
    json_object_object_add(o, k, json_object_new_double(v));
}

static void integer(json_object *o, const char *k, int v) {
    json_object_object_add(o, k, json_object_new_int(v));
}

static void decode(void) {
    json_object *s = bc_frame_example();
    CHECK(bc_frame_validate(s, &e) == 0);
    uint8_t bytes[] = {0xaa, 0x55, 0xc4, 9, 0xe4, 12, 1, 0xbd};
    json_object *r = bc_frame_decode(s, bytes, 8, "temperature", &e);
    CHECK(r && json_object_get_boolean(bc_get(r, "matched")) && bc_num(r, "value") == 25 &&
          fabs(bc_num(bc_get(bc_get(r, "readings"), "voltage"), "value") - 3.3) < 1e-12);
    json_object_put(r);
    for (size_t n = 0; n < 8; n++) {
        r = bc_frame_decode(s, bytes, n, "", &e);
        CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
        json_object_put(r);
    }
    CHECK(!bc_frame_decode(s, bytes, 8, "missing", &e));
    for (unsigned i = 0; i < 8; i++) {
        bytes[i] ^= 1;
        r = bc_frame_decode(s, bytes, 8, "", &e);
        CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
        json_object_put(r);
        bytes[i] ^= 1;
    }
    const char *types[] = {"u8", "s8", "u16", "s16", "u32", "s32", "f32"};
    unsigned widths[] = {1, 1, 2, 2, 4, 4, 4};
    double expected[] = {255, -1, 65535, -1, 4294967295.0, -1, 1.0};
    for (unsigned i = 0; i < 7; i++)
        for (unsigned big = 0; big < 2; big++) {
            json_object *x =
                json_tokener_parse("{\"schema\":1,\"name\":\"vector\",\"length\":4,\"fields\":[{"
                                   "\"id\":\"v\",\"offset\":0,\"type\":\"u32\"}]}");
            json_object *f = json_object_array_get_idx(bc_get(x, "fields"), 0);
            text(f, "type", types[i]);
            text(f, "endian", big ? "big" : "little");
            uint8_t data[4] = {255, 255, 255, 255};
            if (i == 6) {
                data[0] = big ? 0x3f : 0;
                data[1] = big ? 0x80 : 0;
                data[2] = big ? 0 : 0x80;
                data[3] = big ? 0 : 0x3f;
            }
            integer(x, "length", (int)widths[i]);
            r = bc_frame_decode(x, data, widths[i], "", &e);
            CHECK(r && json_object_get_boolean(bc_get(r, "matched")) &&
                  bc_num(r, "value") == expected[i]);
            json_object_put(r);
            num(f, "scale", 2);
            num(f, "bias", 3);
            r = bc_frame_decode(x, data, widths[i], "", &e);
            CHECK(r && bc_num(r, "value") == expected[i] * 2 + 3);
            json_object_put(r);
            json_object_put(x);
        }
    json_object *x = json_tokener_parse("{\"schema\":1,\"name\":\"float\",\"length\":4,\"fields\":["
                                        "{\"id\":\"v\",\"offset\":0,\"type\":\"f32\"}]}");
    uint8_t nan[] = {0, 0, 0xc0, 0x7f};
    r = bc_frame_decode(x, nan, 4, "", &e);
    CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
    json_object_put(r);
    json_object_put(x);
    json_object *fields = bc_get(s, "fields"), *first = json_object_array_get_idx(fields, 0);
    integer(first, "offset", 1);
    CHECK(bc_frame_validate(s, &e) < 0);
    integer(first, "offset", 2);
    num(first, "scale", NAN);
    CHECK(bc_frame_validate(s, &e) < 0);
    num(first, "scale", .01);
    integer(s, "length", 4097);
    CHECK(bc_frame_validate(s, &e) < 0);
    integer(s, "length", 8);
    integer(bc_get(s, "checksum"), "length", 8);
    CHECK(bc_frame_validate(s, &e) < 0);
    integer(bc_get(s, "checksum"), "length", 7);
    json_object_array_add(fields, bc_clone(first));
    CHECK(bc_frame_validate(s, &e) < 0);
    json_object_array_del_idx(fields, 3, 1);
    const char *checksums[] = {"xor8", "crc16_modbus"};
    for (unsigned i = 0; i < 2; i++) {
        x = json_tokener_parse("{\"schema\":1,\"name\":\"crc\",\"length\":4,\"fields\":[{\"id\":"
                               "\"v\",\"offset\":0,\"type\":\"u16\"}],\"checksum\":{\"type\":"
                               "\"xor8\",\"offset\":2,\"start\":0,\"length\":2}}");
        text(bc_get(x, "checksum"), "type", checksums[i]);
        uint8_t data[] = {0x12, 0x34, 0x26, 0};
        if (i) {
            uint16_t crc = bc_modbus_crc(data, 2);
            data[2] = (uint8_t)crc;
            data[3] = (uint8_t)(crc >> 8);
        }
        r = bc_frame_decode(x, data, 4, "", &e);
        CHECK(r && json_object_get_boolean(bc_get(r, "matched")));
        json_object_put(r);
        if (i) {
            text(bc_get(x, "checksum"), "endian", "big");
            uint8_t temp = data[2];
            data[2] = data[3];
            data[3] = temp;
            r = bc_frame_decode(x, data, 4, "", &e);
            CHECK(r && json_object_get_boolean(bc_get(r, "matched")));
            json_object_put(r);
        }
        json_object_put(x);
    }
    /* Deterministic corrupt-input corpus exercises all byte values and lengths. */
    uint32_t random = 0x1badb002;
    for (unsigned i = 0; i < 20000; i++) {
        uint8_t data[12];
        for (unsigned j = 0; j < 12; j++) {
            random = random * 1664525u + 1013904223u;
            data[j] = (uint8_t)(random >> 24);
        }
        r = bc_frame_decode(s, data, i % 13, "temperature", &e);
        CHECK(r && json_object_is_type(bc_get(r, "matched"), json_type_boolean));
        json_object_put(r);
    }
    json_object_put(s);
}

static void wiring(void) {
    json_object *a =
        json_tokener_parse("[{\"id\":\"gnd\",\"channel\":\"manual\",\"connector\":\"J1\",\"pin\":"
                           "\"1\",\"adapter_pin\":\"GND\",\"signal\":\"GND\",\"role\":\"ground\","
                           "\"voltage_v\":0,\"pcb_limit_v\":3.3,\"adapter_limit_v\":3.3}]");
    CHECK(bc_wiring_validate(a, NULL, &e) == 0);
    char *t = bc_wiring_text(a, &e);
    CHECK(t && strstr(t, "J1/1"));
    free(t);
    json_object *x = json_object_array_get_idx(a, 0);
    num(x, "voltage_v", 5);
    CHECK(bc_wiring_validate(a, NULL, &e) < 0);
    num(x, "voltage_v", 1);
    CHECK(bc_wiring_validate(a, NULL, &e) < 0);
    num(x, "voltage_v", 0);
    text(x, "role", "uart_tx");
    CHECK(bc_wiring_validate(a, NULL, &e) < 0);
    text(x, "role", "ground");
    json_object_array_add(a, bc_clone(x));
    CHECK(bc_wiring_validate(a, NULL, &e) < 0);
    json_object_array_del_idx(a, 1, 1);
    num(x, "pcb_limit_v", NAN);
    CHECK(bc_wiring_validate(a, NULL, &e) < 0);
    json_object_put(a);
}

static void registry(void) {
    json_object *c = json_tokener_parse(
        "{\"adapter\":\"serial\",\"bus\":\"uart\",\"device\":\"/dev/ttyUSB-fixture\"}");
    json_object *plan = bc_qualification_plan(c, "uart_loopback", &e);
    CHECK(plan);
    json_object *a = json_object_new_object();
    text(a, "fingerprint", bc_str(plan, "fingerprint"));
    json_object *report = bc_qualification_assess(plan, a, &e);
    CHECK(report && !strcmp(bc_str(report, "status"), "pending"));
    json_object *r =
        bc_compatibility_record(report, "Modelo revisão A", "Linux fixture", "Driver fixture", &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "partial"));
    json_object *rows = json_object_new_array();
    json_object_array_add(rows, bc_clone(r));
    CHECK(bc_compatibility_validate(rows, &e) == 0);
    json_object *m = bc_compatibility_match(rows, c, "Linux fixture", "Driver fixture", &e);
    CHECK(m && !strcmp(bc_str(m, "status"), "partial"));
    json_object_put(m);
    m = bc_compatibility_match(rows, c, "Outro sistema", "Driver fixture", &e);
    CHECK(m && !strcmp(bc_str(m, "status"), "untested"));
    json_object_put(m);
    char root[] = "/tmp/bpcb-registry-XXXXXX";
    CHECK(mkdtemp(root));
    CHECK(bc_compatibility_save(root, r, 0, &e) == 0);
    CHECK(bc_compatibility_save(root, r, 0, &e) < 0);
    CHECK(bc_compatibility_save(root, r, 1, &e) == 0);
    json_object *loaded = bc_compatibility_list(root, &e);
    CHECK(loaded && json_object_equal(rows, loaded));
    json_object_put(loaded);
    char lockpath[4096];
    snprintf(lockpath, sizeof lockpath, "%s/storage.lock", root);
    int fd = open(lockpath, O_RDWR);
    CHECK(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    CHECK(bc_compatibility_save(root, r, 1, &e) < 0);
    close(fd);
    text(r, "status", "validated");
    CHECK(bc_compatibility_save(root, r, 1, &e) < 0);
    text(r, "status", "partial");
    text(r, "source", "automatic-certification");
    CHECK(bc_compatibility_save(root, r, 1, &e) < 0);
    text(r, "source", "operator-recorded");
    json_object_array_add(rows, bc_clone(r));
    CHECK(bc_compatibility_validate(rows, &e) < 0);
    char file[4096];
    snprintf(file, sizeof file, "%s/compatibility.json", root);
    unlink(file);
    unlink(lockpath);
    rmdir(root);
    json_object_put(rows);
    json_object_put(r);
    json_object_put(report);
    json_object_put(a);
    json_object_put(plan);
    json_object_put(c);
}

typedef struct {
    double time;
    size_t at, size, chunk;
    uint8_t data[16], pending[16];
    size_t used;
    int sent, cancel, passive;
} stream;

static double tick(void *p) {
    return ((stream *)p)->time;
}

static int stopped_(void *p) {
    return ((stream *)p)->cancel;
}

static int wait_(void *p, unsigned ms, bc_error *error) {
    (void)error;
    ((stream *)p)->time += ms / 1000.0;
    return 0;
}

static json_object *tx(void *p, json_object *op, bc_error *error) {
    (void)op;
    (void)error;
    ((stream *)p)->sent = 1;
    return json_tokener_parse("{\"received\":\"\"}");
}

static int poll_(void *p, uint8_t *out, size_t maximum, unsigned ms, bc_error *error) {
    (void)error;
    stream *s = p;
    s->time += ms / 1000.0;
    if (!s->sent && !s->passive)
        return 0;
    size_t n = s->size - s->at;
    if (n > s->chunk)
        n = s->chunk;
    if (n > maximum)
        n = maximum;
    memcpy(out, s->data + s->at, n);
    s->at += n;
    return (int)n;
}

static int unread_(void *p, const uint8_t *data, size_t n, bc_error *error) {
    stream *s = p;
    if (n > sizeof s->pending)
        return bc_fail(error, "Queue limit");
    memcpy(s->pending, data, n);
    s->used = n;
    return 0;
}

static void framing(void) {
    json_object *op =
        json_tokener_parse("{\"kind\":\"uart_protocol\",\"protocol\":\"frame\",\"write\":\"01\","
                           "\"delimiter\":\"none\",\"max_bytes\":64,\"timeout_ms\":500}");
    json_object_object_add(op, "frame", bc_frame_example());
    CHECK(bc_uart_protocol_validate(op, &e) == 0);
    const uint8_t bytes[] = {0xaa, 0x55, 0xc4, 9, 0xe4, 12, 1, 0xbd};
    for (size_t chunk = 1; chunk <= 16; chunk++) {
        stream s = {.size = 16, .chunk = chunk};
        memcpy(s.data, bytes, 8);
        memcpy(s.data + 8, bytes, 8);
        bc_lab_io io = {&s, tx, poll_, tick, wait_, stopped_, unread_};
        json_object *r = bc_uart_protocol_execute(op, &io, &e);
        CHECK(r && json_object_get_boolean(bc_get(r, "matched")) && bc_num(r, "value") == 25);
        json_object_put(r);
    }
    for (size_t n = 0; n < 8; n++) {
        stream s = {.size = n, .chunk = 1};
        memcpy(s.data, bytes, 8);
        bc_lab_io io = {&s, tx, poll_, tick, wait_, stopped_, unread_};
        json_object *r = bc_uart_protocol_execute(op, &io, &e);
        CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
        json_object_put(r);
    }
    stream s = {.cancel = 1};
    bc_lab_io io = {&s, tx, poll_, tick, wait_, stopped_, unread_};
    CHECK(!bc_uart_protocol_execute(op, &io, &e));
    CHECK(!s.sent);
    text(op, "write", "");
    stream passive = {.size = 8, .chunk = 1, .passive = 1};
    memcpy(passive.data, bytes, 8);
    bc_lab_io passive_io = {&passive, tx, poll_, tick, wait_, stopped_, unread_};
    json_object *response = bc_uart_protocol_execute(op, &passive_io, &e);
    CHECK(response && json_object_get_boolean(bc_get(response, "matched")) && !passive.sent);
    json_object_put(response);
    json_object *capture = json_tokener_parse(
        "{\"schema\":1,\"name\":\"passive\",\"bus\":\"uart\",\"exchanges\":[{\"operation\":{"
        "\"write\":\"\",\"read\":8},\"received\":\"AA 55 C4 09 E4 0C 01 BD\"}]}");
    response = bc_replay_execute(capture, op, NULL, &e);
    CHECK(response && !strcmp(bc_str(response, "status"), "pass"));
    json_object_put(response);
    json_object_put(capture);
    json_object_put(op);
}

int main(void) {
    decode();
    wiring();
    registry();
    framing();
    printf("%u verificações de quadros, mapas e registro de compatibilidade.\n", checks);
    return 0;
}
