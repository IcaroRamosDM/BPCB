#define _POSIX_C_SOURCE 200809L
#include "lab.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static json_object *parse(const char *s) {
    json_object *r = json_tokener_parse(s);
    CHECK(r);
    return r;
}

static void text(json_object *o, const char *k, const char *s) {
    json_object_object_add(o, k, json_object_new_string(s));
}

typedef struct {
    uint8_t registers[256];
    double now;
    unsigned transfers, oled_bytes;
    int nack, cancelled, oled, ina;
    uint16_t words[6];
} fake;

static double tick(void *ctx) {
    return ((fake *)ctx)->now;
}

static int wait_ms(void *ctx, unsigned ms, bc_error *e) {
    (void)e;
    ((fake *)ctx)->now += ms / 1000.0;
    return 0;
}

static int cancel(void *ctx) {
    return ((fake *)ctx)->cancelled;
}

static json_object *transaction(void *ctx, json_object *op, bc_error *e) {
    fake *f = ctx;
    f->transfers++;
    if (f->nack) {
        errno = ENXIO;
        bc_fail(e, "NACK de referência.");
        return NULL;
    }
    uint8_t bytes[64], data[32];
    size_t n = 0;
    CHECK(bc_comm_hex(bc_str(op, "write"), bytes, sizeof bytes, &n, e) == 0);
    size_t read = (size_t)bc_num(op, "read");
    CHECK(read <= sizeof data && n >= 1);
    if (f->oled) {
        CHECK(read == 0 && n <= 25);
        CHECK(bytes[0] == 0 || bytes[0] == 0x40);
        if (bytes[0] == 0x40) {
            CHECK(n == 17);
            f->oled_bytes += (unsigned)n - 1;
        }
    } else if (f->ina) {
        CHECK(bytes[0] < 6);
        if (read) {
            CHECK(read == 2 && n == 1);
            data[0] = (uint8_t)(f->words[bytes[0]] >> 8);
            data[1] = (uint8_t)f->words[bytes[0]];
        } else {
            CHECK(n == 3);
            f->words[bytes[0]] = (uint16_t)(((unsigned)bytes[1] << 8) | bytes[2]);
        }
    } else if (read) {
        CHECK(n == 1 && (size_t)bytes[0] + read <= sizeof f->registers);
        memcpy(data, f->registers + bytes[0], read);
    } else {
        CHECK(n >= 2 && (size_t)bytes[0] + n - 1 <= sizeof f->registers);
        memcpy(f->registers + bytes[0], bytes + 1, n - 1);
    }
    json_object *r = json_object_new_object();
    char *hex = bc_comm_format_bytes(data, read, 1);
    text(r, "received", hex);
    free(hex);
    json_object_object_add(r, "matched", json_object_new_boolean(1));
    return r;
}

static void le(fake *f, unsigned reg, int value) {
    unsigned v = (unsigned)(uint16_t)value;
    f->registers[reg] = (uint8_t)v;
    f->registers[reg + 1] = (uint8_t)(v >> 8);
}

static void be(fake *f, unsigned reg, unsigned value) {
    if (f->ina) {
        CHECK(reg < 6);
        f->words[reg] = (uint16_t)value;
        return;
    }
    f->registers[reg] = (uint8_t)(value >> 8);
    f->registers[reg + 1] = (uint8_t)value;
}

static void adc(fake *f, unsigned reg, unsigned value) {
    f->registers[reg] = (uint8_t)(value >> 12);
    f->registers[reg + 1] = (uint8_t)(value >> 4);
    f->registers[reg + 2] = (uint8_t)(value << 4);
}

static void sensors(void) {
    fake f = {0};
    bc_lab_io io = {&f, transaction, NULL, tick, wait_ms, cancel, NULL};
    bc_error e = {{0}};
    f.registers[0xd0] = 0x58;
    int coefficients[] = {27504, 26435, -1000, 36477, -10685, 3024,
                          2855,  140,   -7,    15500, -14600, 6000};
    for (unsigned i = 0; i < 12; i++)
        le(&f, 0x88 + i * 2, coefficients[i]);
    adc(&f, 0xfa, 519888);
    adc(&f, 0xf7, 415148);
    json_object *op = parse("{\"kind\":\"peripheral\",\"device\":\"bmp280\",\"action\":\"read\","
                            "\"metric\":\"temperature\",\"address\":118}");
    json_object *r = bc_peripheral_execute(op, &io, &e);
    CHECK(r);
    CHECK(fabs(bc_num(r, "value") - 25.0824779308) < 1e-7);
    CHECK(fabs(bc_num(bc_get(bc_get(r, "readings"), "pressure"), "value") - 100653.2667758) < 1e-5);
    json_object_put(r);
    text(op, "action", "measure");
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(r && f.registers[0xf4] == 0x25 && f.now >= 0.012);
    json_object_put(r);
    f.registers[0xd0] = 0x60;
    f.registers[0xa1] = 75;
    le(&f, 0xe1, 362);
    f.registers[0xe3] = 0;
    f.registers[0xe4] = 20;
    f.registers[0xe5] = 0x26;
    f.registers[0xe6] = 3;
    f.registers[0xe7] = 30;
    be(&f, 0xfd, 30000);
    text(op, "device", "bme280");
    text(op, "metric", "humidity");
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(r);
    CHECK(bc_num(r, "value") >= 0 && bc_num(r, "value") <= 100);
    CHECK(f.registers[0xf2] == 1);
    json_object_put(r);
    le(&f, 0x8e, 0);
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(!r);
    CHECK(strstr(e.text, "Coeficientes"));
    json_object_put(op);
    memset(&f, 0, sizeof f);
    f.registers[0x75] = 0x68;
    be(&f, 0x3f, 16384);
    be(&f, 0x43, 131);
    op = parse("{\"kind\":\"peripheral\",\"device\":\"mpu6050\",\"action\":\"measure\",\"metric\":"
               "\"accel_z\",\"address\":104}");
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(r);
    CHECK(fabs(bc_num(r, "value") - 9.80665) < 1e-9);
    CHECK(fabs(bc_num(bc_get(bc_get(r, "readings"), "gyro_x"), "value") - 1) < 1e-9);
    CHECK(fabs(bc_num(bc_get(bc_get(r, "readings"), "temperature"), "value") - 36.53) < 1e-9);
    json_object_put(r);
    f.registers[0x75] = 0;
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(!r);
    json_object_put(op);
    memset(&f, 0, sizeof f);
    f.ina = 1;
    be(&f, 0, 0x399f);
    be(&f, 2, 10002);
    be(&f, 1, 1000);
    be(&f, 5, 4096);
    be(&f, 4, 1000);
    be(&f, 3, 20);
    op = parse("{\"kind\":\"peripheral\",\"device\":\"ina219\",\"action\":\"read\",\"metric\":"
               "\"current\",\"address\":64,\"shunt_ohms\":0.1}");
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(r);
    CHECK(fabs(bc_num(r, "value") - 0.1) < 1e-9);
    CHECK(fabs(bc_num(bc_get(bc_get(r, "readings"), "bus_voltage"), "value") - 5) < 1e-9);
    json_object_put(r);
    be(&f, 5, 0);
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(!r);
    json_object_put(op);
    memset(&f, 0, sizeof f);
    f.oled = 1;
    op = parse("{\"kind\":\"peripheral\",\"device\":\"ssd1306\",\"action\":\"pattern\",\"metric\":"
               "\"acknowledged\",\"address\":60,\"height\":64,\"pattern\":\"checkerboard\"}");
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(r);
    CHECK(f.oled_bytes == 1024 && f.transfers == 74);
    CHECK(json_object_get_boolean(bc_get(r, "requires_visual_confirmation")));
    json_object_put(r);
    f.cancelled = 1;
    r = bc_peripheral_execute(op, &io, &e);
    CHECK(!r);
    json_object_put(op);
}

static json_object *transcript(const char *tx, const uint8_t *rx, size_t n) {
    json_object *spec = json_object_new_object(), *rows = json_object_new_array(),
                *row = json_object_new_object(), *op = json_object_new_object();
    json_object_object_add(spec, "schema", json_object_new_int(1));
    text(spec, "bus", "uart");
    text(spec, "name", "Referência independente");
    json_object_object_add(spec, "exchanges", rows);
    json_object_array_add(rows, row);
    json_object_object_add(row, "operation", op);
    text(op, "write", tx);
    json_object_object_add(op, "read", json_object_new_int64((int64_t)n));
    char *hex = bc_comm_format_bytes(rx, n, 1);
    text(row, "received", hex);
    free(hex);
    return spec;
}

static void protocols(void) {
    bc_error e = {{0}};
    const uint8_t crc_input[] = {1, 3, 0, 0, 0, 2};
    CHECK(bc_modbus_crc(crc_input, sizeof crc_input) == 0x0bc4);
    json_object *op = parse("{\"kind\":\"uart_protocol\",\"protocol\":\"at\",\"write\":\"41 "
                            "54\",\"delimiter\":\"crlf\",\"timeout_ms\":1000,\"max_bytes\":1024}");
    const char *response = "AT\r\nWIFI CONNECTED\r\n+DATA: 42\r\nOK\r\n";
    json_object *spec = transcript("41 54 0D 0A", (const uint8_t *)response, strlen(response));
    for (unsigned chunk = 1; chunk <= 64; chunk++) {
        json_object *fault = json_object_new_object();
        text(fault, "type", "fragment");
        json_object_object_add(fault, "fragment_size", json_object_new_int((int)chunk));
        json_object *r = bc_replay_execute(spec, op, fault, &e);
        CHECK(r);
        CHECK(!strcmp(bc_str(r, "status"), "pass"));
        CHECK(json_object_array_length(bc_get(r, "unsolicited")) == 1);
        CHECK(json_object_array_length(bc_get(r, "lines")) == 1);
        json_object_put(r);
        json_object_put(fault);
    }
    const char *failures[] = {"ERROR\r\n", "+CME ERROR: 1\r\n",          "busy p...\r\n",
                              "\r\nOK",    "+IPD,6:\r\nOK\r\nERROR\r\n", "ERROR\r\nOK\r\n"};
    for (unsigned i = 0; i < sizeof failures / sizeof *failures; i++) {
        json_object *s =
            transcript("41 54 0D 0A", (const uint8_t *)failures[i], strlen(failures[i]));
        json_object *r = bc_replay_execute(s, op, NULL, &e);
        CHECK(r);
        CHECK(!strcmp(bc_str(r, "status"), "fail"));
        json_object_put(r);
        json_object_put(s);
    }
    json_object *fault = parse("{\"type\":\"timeout\"}");
    json_object *r = bc_replay_execute(spec, op, fault, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "fail") &&
          json_object_get_boolean(bc_get(r, "timed_out")));
    json_object_put(r);
    json_object_put(fault);
    json_object_put(spec);
    json_object_put(op);
    op = parse("{\"kind\":\"uart_protocol\",\"protocol\":\"modbus_rtu\",\"slave\":1,\"function\":3,"
               "\"start\":0,\"quantity\":2,\"max_bytes\":256,\"timeout_ms\":1000}");
    uint8_t rx[] = {1, 3, 4, 0, 42, 0, 43, 0, 0};
    uint16_t crc = bc_modbus_crc(rx, 7);
    rx[7] = (uint8_t)crc;
    rx[8] = (uint8_t)(crc >> 8);
    spec = transcript("01 03 00 00 00 02 C4 0B", rx, sizeof rx);
    r = bc_replay_execute(spec, op, NULL, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "pass") && bc_num(r, "value") == 42);
    json_object_put(r);
    fault = parse("{\"type\":\"corrupt\",\"offset\":8,\"xor\":1}");
    r = bc_replay_execute(spec, op, fault, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "fail"));
    CHECK(strstr(bc_str(r, "message"), "CRC"));
    json_object_put(r);
    json_object_put(fault);
    fault = parse("{\"type\":\"truncate\"}");
    r = bc_replay_execute(spec, op, fault, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "fail"));
    json_object_put(r);
    json_object_put(fault);
    json_object_put(spec);
    json_object_put(op);
}

static void contracts(void) {
    bc_error e = {{0}};
    json_object *d =
        parse("{\"api\":2,\"channels\":{\"sensor\":{\"api\":1,\"bus\":\"i2c\",\"max_transfer\":32},"
              "\"modem\":{\"api\":1,\"bus\":\"uart\",\"max_transfer\":4096}}}");
    CHECK(bc_comm_declaration(d, &e) == 0);
    json_object *c = parse(
        "{\"api\":2,\"channels\":{\"sensor\":{\"adapter\":\"ch341t\",\"bus\":\"i2c\",\"device\":"
        "\"usb:reference\",\"connections_checked\":true},\"modem\":{\"adapter\":\"serial\",\"bus\":"
        "\"uart\",\"device\":\"/dev/pts/999\",\"connections_checked\":true}}}");
    json_object *r = bc_lab_resolve(d, c, &e);
    CHECK(r);
    json_object_put(r);
    json_object *op = parse(
        "{\"channel\":\"sensor\",\"operation\":{\"write\":\"D0\",\"read\":1,\"address\":118}}");
    CHECK(bc_comm_operation_check(c, d, op, &e) == 0);
    text(op, "channel", "absent");
    CHECK(bc_comm_operation_check(c, d, op, &e) < 0);
    json_object_put(op);
    json_object_object_add(bc_get(bc_get(c, "channels"), "modem"), "connections_checked",
                           json_object_new_boolean(0));
    r = bc_lab_resolve(d, c, &e);
    CHECK(!r);
    json_object_put(c);
    json_object_put(d);
    c = parse("{\"adapter\":\"serial\",\"bus\":\"uart\",\"device\":\"/dev/ttyQA_FIXTURE\"}");
    json_object *plan = bc_qualification_plan(c, "uart_loopback", &e);
    CHECK(plan);
    json_object *a = parse("{\"model_revision\":true,\"connections\":true,\"levels\":true,\"native_"
                           "os\":\"Linux fixture\"}");
    text(a, "fingerprint", bc_str(plan, "fingerprint"));
    r = bc_qualification_assess(plan, a, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "pending"));
    json_object_put(r);
    json_object *evidence = parse("{\"received\":\"A5 5A 00 FF\",\"transmitted\":\"A5 5A 00 "
                                  "FF\",\"matched\":true,\"simulation\":true}");
    json_object *transport = json_object_new_object();
    json_object_object_add(transport, "configuration", bc_clone(bc_get(plan, "configuration")));
    json_object_object_add(evidence, "transport", transport);
    json_object_object_add(a, "functional", evidence);
    r = bc_qualification_assess(plan, a, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "fail"));
    json_object_put(r);
    json_object_object_add(evidence, "simulation", json_object_new_boolean(0));
    r = bc_qualification_assess(plan, a, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "pass"));
    json_object_put(r);
    json_object_object_add(evidence, "requires_visual_confirmation", json_object_new_boolean(1));
    r = bc_qualification_assess(plan, a, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "pending"));
    json_object_put(r);
    json_object_object_add(a, "display_visual", json_object_new_boolean(0));
    r = bc_qualification_assess(plan, a, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "fail"));
    json_object_put(r);
    json_object_object_add(a, "display_visual", json_object_new_boolean(1));
    r = bc_qualification_assess(plan, a, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "pass"));
    json_object_put(r);
    json_object_put(a);
    json_object_put(plan);
    json_object_put(c);
}

static void fuzz(void) {
    bc_error e = {{0}};
    uint32_t state = 0x15732a4u;
    const char *buses[] = {"i2c", "spi", "uart", "jtag", "invalid"};
    for (unsigned i = 0; i < 20000; i++) {
        state = state * 1664525u + 1013904223u;
        json_object *op = json_object_new_object();
        const char *key = i % 5 == 0   ? "kind||device"
                          : i % 5 == 1 ? "max_bytes"
                          : i % 5 == 2 ? "address"
                          : i % 5 == 3 ? "write"
                                       : "read";
        json_object *v = i % 3 == 0   ? json_object_new_int64((int64_t)state - 2147483648LL)
                         : i % 3 == 1 ? json_object_new_double((double)state / 13.0)
                                      : json_object_new_string("00 FF 0 bad");
        json_object_object_add(op, key, v);
        CHECK(bc_comm_operation_validate(buses[i % 5], op, &e) < 0);
        json_object_put(op);
    }
    for (unsigned i = 0; i < 30000; i++) {
        json_object *op = json_tokener_parse(
            i % 2 ? "{\"kind\":\"uart_protocol\",\"protocol\":\"at\",\"write\":\"41 54\"}"
                  : "{\"kind\":\"peripheral\",\"device\":\"bmp280\",\"action\":\"read\",\"metric\":"
                    "\"temperature\"}");
        const char *key = i % 2 ? "max_bytes" : "address";
        json_object *value = i % 3 == 0   ? json_object_new_array()
                             : i % 3 == 1 ? json_object_new_double(1e300)
                                          : json_object_new_int64(-((int64_t)i + 1));
        json_object_object_add(op, key, value);
        CHECK(bc_comm_operation_validate(i % 2 ? "uart" : "i2c", op, &e) < 0);
        json_object_put(op);
    }
    json_object *bad = parse("{\"kind\":\"peripheral\",\"device\":\"bmp280\",\"action\":\"read\","
                             "\"metric\":\"temperature\",\"kind||device\":0}");
    CHECK(bc_peripheral_validate("i2c", bad, &e) < 0);
    json_object_put(bad);
}

int main(void) {
    sensors();
    protocols();
    contracts();
    fuzz();
    printf("%u verificações: componentes, UART/AT, Modbus/CRC, captura, contratos, qualificação e "
           "entradas adversariais.\n",
           checks);
    return 0;
}
