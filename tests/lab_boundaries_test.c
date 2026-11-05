#define _POSIX_C_SOURCE 200809L
#include "lab.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "BOUNDARY FAIL %d: %s\n", __LINE__, #x);                               \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    double now, extra;
    const uint8_t *response, *stale;
    size_t size, at, stale_size, stale_at, chunk;
    uint8_t pending[128];
    size_t used;
    int sent, cancelled, stall;
} stream;

static double tick(void *p) {
    return ((stream *)p)->now;
}

static int stopped(void *p) {
    return ((stream *)p)->cancelled;
}

static int wait_(void *p, unsigned ms, bc_error *e) {
    (void)e;
    stream *s = p;
    if (!s->stall)
        s->now += ms / 1000.0;
    return 0;
}

static json_object *tx(void *p, json_object *op, bc_error *e) {
    (void)e;
    stream *s = p;
    CHECK(bc_num(op, "read") == 0);
    s->sent++;
    return json_tokener_parse("{\"received\":\"\"}");
}

static int unread_(void *p, const uint8_t *b, size_t n, bc_error *e) {
    stream *s = p;
    if (s->used + n > sizeof s->pending)
        return bc_fail(e, "Fila cheia.");
    memmove(s->pending + n, s->pending, s->used);
    memcpy(s->pending, b, n);
    s->used += n;
    return 0;
}

static int poll_(void *p, uint8_t *out, size_t cap, unsigned ms, bc_error *e) {
    (void)e;
    stream *s = p;
    if (!s->stall)
        s->now += ms / 1000.0;
    const uint8_t *bytes = NULL;
    size_t n = 0;
    if (s->used) {
        n = s->used;
        if (n > cap)
            n = cap;
        memcpy(out, s->pending, n);
        memmove(s->pending, s->pending + n, s->used - n);
        s->used -= n;
        return (int)n;
    }
    size_t *at = s->sent ? &s->at : &s->stale_at;
    size_t size = s->sent ? s->size : s->stale_size;
    bytes = s->sent ? s->response : s->stale;
    n = size - *at;
    if (n > cap)
        n = cap;
    if (s->chunk && n > s->chunk)
        n = s->chunk;
    if (n) {
        memcpy(out, bytes + *at, n);
        *at += n;
        if (s->sent)
            s->now += s->extra;
    }
    return (int)n;
}

static json_object *op(const char *protocol) {
    json_object *o = json_object_new_object();
    json_object_object_add(o, "kind", json_object_new_string("uart_protocol"));
    json_object_object_add(o, "protocol", json_object_new_string(protocol));
    json_object_object_add(o, "write", json_object_new_string("41 54"));
    json_object_object_add(o, "delimiter", json_object_new_string("crlf"));
    json_object_object_add(o, "timeout_ms", json_object_new_int(500));
    json_object_object_add(o, "max_bytes", json_object_new_int(128));
    return o;
}

static json_object *execute(json_object *o, stream *s, bc_error *e) {
    bc_lab_io io = {s, tx, poll_, tick, wait_, stopped, unread_};
    e->text[0] = 0;
    return bc_uart_protocol_execute(o, &io, e);
}

static void modbus_vectors(void) {
    const unsigned functions[] = {1, 2, 3, 4, 5, 6, 15, 16};
    bc_error e = {{0}};
    for (unsigned at = 0; at < sizeof functions / sizeof *functions; at++) {
        unsigned f = functions[at], quantity = f <= 2 ? 10 : f == 5 || f == 6 ? 1 : 2;
        json_object *o =
            json_tokener_parse("{\"kind\":\"uart_protocol\",\"protocol\":\"modbus_rtu\",\"slave\":"
                               "7,\"start\":16,\"max_bytes\":128,\"timeout_ms\":500}");
        json_object_object_add(o, "function", json_object_new_int((int)f));
        json_object_object_add(o, "quantity", json_object_new_int((int)quantity));
        if (f > 4)
            json_object_object_add(o, "values",
                                   json_object_new_string(f == 5    ? "FF 00"
                                                          : f == 6  ? "12 34"
                                                          : f == 15 ? "01"
                                                                    : "12 34 AB CD"));
        /* Function 15 quantity 2 needs one packed data byte. */
        uint8_t rx[32] = {7, (uint8_t)f};
        size_t n;
        if (f <= 2) {
            rx[2] = 2;
            rx[3] = 0x55;
            rx[4] = 1;
            n = 5;
        } else if (f <= 4) {
            rx[2] = 4;
            rx[3] = 0x12;
            rx[4] = 0x34;
            rx[5] = 0xab;
            rx[6] = 0xcd;
            n = 7;
        } else {
            rx[2] = 0;
            rx[3] = 16;
            rx[4] = f == 5 ? 255 : f == 6 ? 0x12 : 0;
            rx[5] = f == 6 ? 0x34 : f == 5 ? 0 : (uint8_t)quantity;
            n = 6;
        }
        uint16_t crc = bc_modbus_crc(rx, n);
        rx[n] = (uint8_t)crc;
        rx[n + 1] = (uint8_t)(crc >> 8);
        n += 2;
        for (size_t chunk = 1; chunk <= 16; chunk++) {
            stream transport = {.response = rx, .size = n, .chunk = chunk};
            json_object *r = execute(o, &transport, &e);
            if (!r || !json_object_get_boolean(bc_get(r, "matched")))
                fprintf(stderr, "Modbus function %u fragment %zu: %s / %s\n", f, chunk, e.text,
                        bc_str(r, "message"));
            CHECK(r && json_object_get_boolean(bc_get(r, "matched")));
            CHECK(bc_num(r, "value") == (f <= 2 ? 1 : f <= 4 ? 4660 : 1));
            json_object_put(r);
        }
        rx[n - 1] ^= 1;
        stream transport = {.response = rx, .size = n};
        json_object *r = execute(o, &transport, &e);
        CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
        json_object_put(r);
        rx[n - 1] ^= 1;
        if (f > 4) {
            rx[5] ^= 1;
            crc = bc_modbus_crc(rx, n - 2);
            rx[n - 2] = (uint8_t)crc;
            rx[n - 1] = (uint8_t)(crc >> 8);
            transport = (stream){.response = rx, .size = n};
            r = execute(o, &transport, &e);
            CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
            json_object_put(r);
        }
        uint8_t exception[] = {7, (uint8_t)(f | 0x80u), 2, 0, 0};
        crc = bc_modbus_crc(exception, 3);
        exception[3] = (uint8_t)crc;
        exception[4] = (uint8_t)(crc >> 8);
        transport = (stream){.response = exception, .size = sizeof exception, .chunk = 1};
        r = execute(o, &transport, &e);
        CHECK(r && !json_object_get_boolean(bc_get(r, "matched")) && bc_num(r, "exception") == 2);
        json_object_put(r);
        json_object_put(o);
    }
}

int main(void) {
    bc_error e = {{0}};
    json_object *o = op("delimited");
    const char *bytes = "ABC\r\nSECOND\r\n";
    for (size_t chunk = 1; chunk <= 64; chunk++) {
        stream s = {.response = (const uint8_t *)bytes, .size = strlen(bytes), .chunk = chunk};
        json_object *r = execute(o, &s, &e);
        CHECK(r && json_object_get_boolean(bc_get(r, "matched")));
        CHECK(!strcmp(bc_str(r, "received"), "41 42 43 0D 0A"));
        uint8_t left[64];
        size_t used = 0;
        int n;
        while ((n = poll_(&s, left + used, sizeof left - used, 0, &e)) > 0)
            used += (size_t)n;
        CHECK(used == 8 && !memcmp(left, "SECOND\r\n", 8));
        json_object_put(r);
    }
    stream s = {.response = (const uint8_t *)"OK\r\n", .size = 4, .extra = 1};
    json_object *r = execute(o, &s, &e);
    CHECK(r && !json_object_get_boolean(bc_get(r, "matched")) &&
          json_object_get_boolean(bc_get(r, "timed_out")));
    json_object_put(r);
    json_object_put(o);
    o = op("at");
    s = (stream){.stale = (const uint8_t *)"OK\r\n", .stale_size = 4};
    r = execute(o, &s, &e);
    CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
    CHECK(!strcmp(bc_str(r, "preexisting_bytes"), "4F 4B 0D 0A"));
    CHECK(s.sent == 1);
    json_object_put(r);
    s = (stream){.response = (const uint8_t *)"ERROR\r\nOK\r\n", .size = 11};
    r = execute(o, &s, &e);
    CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
    json_object_put(r);
    s = (stream){.response = (const uint8_t *)"OK\r\n", .size = 4, .extra = 1};
    r = execute(o, &s, &e);
    CHECK(r && !json_object_get_boolean(bc_get(r, "matched")));
    json_object_put(r);
    s = (stream){.stall = 1};
    r = execute(o, &s, &e);
    CHECK(!r && strstr(e.text, "avançou"));
    s = (stream){.now = NAN};
    r = execute(o, &s, &e);
    CHECK(!r && strstr(e.text, "Relógio"));
    s = (stream){.cancelled = 1};
    r = execute(o, &s, &e);
    CHECK(!r);
    json_object_put(o);
    const char *bad[] = {
        "{\"kind\":\"uart_protocol\",\"protocol\":\"at\",\"write\":\"41 54\",\"success\":123}",
        "{\"kind\":\"uart_protocol\",\"protocol\":\"at\",\"write\":\"41 00 54\"}",
        "{\"kind\":\"uart_protocol\",\"protocol\":\"modbus_rtu\",\"slave\":0}",
        "{\"kind\":\"uart_protocol\",\"protocol\":\"modbus_rtu\",\"function\":16,\"quantity\":2,"
        "\"values\":\"00 01\"}"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        o = json_tokener_parse(bad[i]);
        CHECK(bc_uart_protocol_validate(o, &e) < 0);
        json_object_put(o);
    }
    json_object *spec =
        json_tokener_parse("{\"schema\":1,\"bus\":\"uart\",\"exchanges\":[{\"operation\":{"
                           "\"write\":\"41 54 0D 0A\",\"read\":4},\"received\":\"4F 4B 0D 0A\"}]}");
    o = op("at");
    json_object *fault = json_tokener_parse("{\"type\":\"fragment\",\"exchange\":1}");
    CHECK(!bc_replay_execute(spec, o, fault, &e));
    json_object_object_add(fault, "exchange", json_object_new_int(0));
    json_object_object_add(fault, "type", json_object_new_string("nack"));
    CHECK(!bc_replay_execute(spec, o, fault, &e));
    json_object_put(fault);
    json_object_object_add(spec, "preamble", json_object_new_string("4F 4B 0D 0A"));
    r = bc_replay_execute(spec, o, NULL, &e);
    CHECK(r && !strcmp(bc_str(r, "status"), "pass") && *bc_str(r, "preexisting_bytes"));
    json_object_put(r);
    json_object_put(spec);
    json_object_put(o);
    modbus_vectors();
    printf("%u verificações de fronteiras: fragmentação independente, fila preservada, resposta "
           "tardia, OK antigo, tipos, cancelamento e falhas.\n",
           checks);
    return 0;
}
