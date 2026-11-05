#define _POSIX_C_SOURCE 200809L
#include "communication/internal.h"
#include "lab.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned opens, closes, checks;
static int fail_next;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "POOL FAIL %d: %s\n", __LINE__, #x);                                   \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

int __wrap_bc_comm_open(bc_comm **out, json_object *config, int (*cancel)(void *), void *context,
                        bc_error *e) {
    bc_comm *s = calloc(1, sizeof *s);
    CHECK(s);
    s->config = bc_comm_config(config, e);
    CHECK(s->config);
    s->cancel = cancel;
    s->cancel_context = context;
    *out = s;
    opens++;
    return 0;
}

void __real_bc_comm_close(bc_comm *);

void __wrap_bc_comm_close(bc_comm *s) {
    if (s)
        closes++;
    __real_bc_comm_close(s);
}

json_object *__wrap_bc_comm_transaction(bc_comm *s, json_object *op, bc_error *e) {
    if (fail_next) {
        fail_next = 0;
        s->lost = 5;
        bc_fail(e, "Desconexão simulada.");
        return NULL;
    }
    json_object *r = json_object_new_object();
    json_object_object_add(
        r, "received", json_object_new_string(*bc_str(op, "expect") ? bc_str(op, "expect") : "60"));
    json_object_object_add(r, "matched", json_object_new_boolean(1));
    return r;
}

static json_object *parse(const char *s) {
    json_object *r = json_tokener_parse(s);
    CHECK(r);
    return r;
}

static json_object *operation(const char *channel, const char *body) {
    json_object *r = json_object_new_object();
    json_object_object_add(r, "channel", json_object_new_string(channel));
    json_object_object_add(r, "operation", parse(body));
    return r;
}

int main(void) {
    bc_error e = {{0}};
    json_object *d =
        parse("{\"api\":2,\"channels\":{\"i2c\":{\"api\":1,\"bus\":\"i2c\",\"max_transfer\":32},"
              "\"spi\":{\"api\":1,\"bus\":\"spi\",\"max_transfer\":32}}}");
    json_object *c =
        parse("{\"api\":2,\"channels\":{\"i2c\":{\"adapter\":\"ft232h\",\"bus\":\"i2c\",\"device\":"
              "\"usb:fixture\",\"connections_checked\":true},\"spi\":{\"adapter\":\"ft232h\","
              "\"bus\":\"spi\",\"device\":\"usb:fixture\",\"connections_checked\":true}}}");
    bc_lab *lab = bc_lab_new(c, d, NULL, NULL, &e);
    CHECK(lab);
    json_object *i2c = operation("i2c", "{\"write\":\"D0\",\"read\":1,\"address\":118}"),
                *spi = operation("spi", "{\"write\":\"A5\",\"read\":1,\"expect\":\"A5\"}");
    for (unsigned i = 0; i < 20; i++) {
        json_object *r = bc_lab_execute(lab, i % 2 ? spi : i2c, &e);
        if (!r)
            fprintf(stderr, "%s\n", e.text);
        CHECK(r);
        CHECK(!strcmp(bc_str(bc_get(r, "transport"), "channel"), i % 2 ? "spi" : "i2c"));
        json_object_put(r);
    }
    CHECK(opens == 20 && closes == 19);
    bc_lab_free(lab);
    CHECK(closes == opens);
    opens = closes = 0;
    json_object_object_add(bc_get(bc_get(c, "channels"), "spi"), "device",
                           json_object_new_string("usb:other"));
    lab = bc_lab_new(c, d, NULL, NULL, &e);
    CHECK(lab);
    json_object *r = bc_lab_execute(lab, i2c, &e);
    CHECK(r);
    json_object_put(r);
    fail_next = 1;
    CHECK(!bc_lab_execute(lab, i2c, &e));
    CHECK(!bc_lab_execute(lab, spi, &e));
    CHECK(opens == 1);
    bc_lab_free(lab);
    CHECK(closes == 1);
    json_object_put(c);
    json_object_put(d);
    json_object_put(i2c);
    json_object_put(spi);
    d = parse("{\"api\":2,\"channels\":{\"i2c\":{\"api\":1,\"bus\":\"i2c\"},\"jtag\":{\"api\":1,"
              "\"bus\":\"jtag\"}}}");
    c = parse("{\"api\":2,\"channels\":{\"i2c\":{\"adapter\":\"ch347\",\"bus\":\"i2c\",\"ch347_"
              "mode\":3,\"device\":\"usb:fixture\",\"connections_checked\":true},\"jtag\":{"
              "\"adapter\":\"ch347\",\"bus\":\"jtag\",\"ch347_mode\":3,\"device\":\"usb:fixture\","
              "\"connections_checked\":true}}}");
    lab = bc_lab_new(c, d, NULL, NULL, &e);
    CHECK(lab);
    json_object *ir = operation("jtag",
                                "{\"register\":\"ir\",\"bits\":8,\"write\":\"01\",\"read\":1}"),
                *dr = operation("jtag",
                                "{\"register\":\"dr\",\"bits\":8,\"write\":\"00\",\"read\":1}");
    i2c = operation("i2c", "{\"address\":118,\"write\":\"D0\",\"read\":1}");
    r = bc_lab_execute(lab, ir, &e);
    CHECK(r);
    json_object_put(r);
    r = bc_lab_execute(lab, i2c, &e);
    CHECK(r);
    json_object_put(r);
    CHECK(!bc_lab_execute(lab, dr, &e));
    json_object *raw = bc_get(dr, "operation");
    json_object_object_add(raw, "ir_bits", json_object_new_int(8));
    json_object_object_add(raw, "ir_write", json_object_new_string("01"));
    r = bc_lab_execute(lab, dr, &e);
    CHECK(r);
    json_object_put(r);
    bc_lab_free(lab);
    json_object_put(c);
    json_object_put(d);
    json_object_put(ir);
    json_object_put(dr);
    json_object_put(i2c);
    CHECK(opens == closes);
    printf("%u verificações de canais: sessões persistentes, interface compartilhada, falha sem "
           "reconexão e restauração IR explícita.\n",
           checks);
    return 0;
}
