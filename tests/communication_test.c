#define _POSIX_C_SOURCE 200809L
#include "communication/internal.h"
#include <assert.h>
#include <errno.h>
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

typedef struct {
    uint8_t data[5000];
    size_t size;
} packet;

typedef struct {
    packet sent[1800], received[128];
    size_t ns, nr, at;
    unsigned controls;
    int short_write, fail_read;
    uint8_t feature[14];
} wire;

static int send(void *context, const uint8_t *p, size_t n, unsigned timeout, bc_error *e) {
    wire *w = context;
    (void)e;
    CHECK(timeout > 0 && timeout <= 10000);
    CHECK(w->ns < 1800 && n <= sizeof w->sent[0].data);
    memcpy(w->sent[w->ns].data, p, n);
    w->sent[w->ns++].size = n;
    if (w->short_write) {
        w->short_write = 0;
        return (int)n - 1;
    }
    return (int)n;
}

static int receive(void *context, uint8_t *p, size_t n, unsigned timeout, bc_error *e) {
    wire *w = context;
    (void)timeout;
    if (w->fail_read) {
        errno = w->fail_read;
        return bc_fail(e, "Falha injetada na resposta.");
    }
    CHECK(w->at < w->nr);
    packet *r = &w->received[w->at++];
    CHECK(r->size <= n);
    memcpy(p, r->data, r->size);
    return (int)r->size;
}

static int feature(void *context, uint8_t *p, size_t n, int get, unsigned timeout, bc_error *e) {
    wire *w = context;
    (void)e;
    (void)timeout;
    if (p[0] == 5) {
        CHECK(get && n == 3);
        p[1] = 0x0c;
        p[2] = 2;
    } else {
        CHECK(p[0] == 6 && n == 14);
        if (get)
            memcpy(p, w->feature, n);
        else
            memcpy(w->feature, p, n);
    }
    return (int)n;
}

static int control(void *context, unsigned request, unsigned value, unsigned index, bc_error *e) {
    wire *w = context;
    (void)e;
    CHECK(index == 0);
    const unsigned expected[][2] = {{0, 0}, {0, 1}, {0, 2}, {9, 2}, {11, 0}, {11, 0x0200}};
    CHECK(w->controls < 6);
    CHECK(expected[w->controls][0] == request && expected[w->controls][1] == value);
    w->controls++;
    return 0;
}

static void queue(wire *w, const uint8_t *p, size_t n) {
    CHECK(w->nr < 128);
    packet *r = &w->received[w->nr++];
    memcpy(r->data, p, n);
    r->size = n;
}

static void response(wire *w, unsigned cmd, unsigned status, unsigned state, unsigned size,
                     const uint8_t *data) {
    uint8_t p[64] = {0};
    p[0] = (uint8_t)cmd;
    p[1] = (uint8_t)status;
    if (cmd == 0x10)
        p[8] = (uint8_t)state;
    else if (cmd == 0x40) {
        p[2] = (uint8_t)state;
        p[3] = (uint8_t)size;
        if (size)
            memcpy(p + 4, data, size);
    } else if (cmd == 0x16) {
        p[2] = (uint8_t)state;
        p[5] = (uint8_t)(size >> 8);
        p[6] = (uint8_t)size;
    } else if (cmd == 0x13) {
        p[2] = (uint8_t)size;
        if (size)
            memcpy(p + 3, data, size);
    }
    queue(w, p, sizeof p);
}

static bc_comm session(wire *w, bc_comm_kind kind, const char *json) {
    bc_error e = {{0}};
    json_object *input = json_tokener_parse(json), *config = bc_comm_config(input, &e);
    CHECK(config);
    json_object_put(input);
    bc_comm s = {0};
    s.kind = kind;
    s.config = config;
    s.io = (bc_comm_io){w, send, receive, feature, control, NULL};
    bc_comm_deadline(&s);
    return s;
}

static unsigned count_command(wire *w, unsigned a, unsigned b) {
    unsigned n = 0;
    for (size_t i = 0; i < w->ns; i++)
        if (w->sent[i].size >= 2 && w->sent[i].data[0] == a && w->sent[i].data[1] == b)
            n++;
    return n;
}

static void configs(void) {
    bc_error e = {{0}};
    json_object *c = bc_comm_config(NULL, &e);
    CHECK(c && !strcmp(bc_str(c, "adapter"), "ch341t") && bc_num(c, "frequency_hz") == 20000);
    CHECK(!bc_get(c, "ch347_mode"));
    json_object_put(c);
    const char *bad[] = {"{\"adapter\":\"ch340\",\"bus\":\"i2c\"}",
                         "{\"adapter\":\"ft232h\",\"bus\":\"spi\",\"spi_mode\":4}",
                         "{\"adapter\":\"mcp2221a\",\"frequency_hz\":20000}",
                         "{\"adapter\":\"ch341t\",\"frequency_hz\":12345}",
                         "{\"adapter\":\"serial\",\"bus\":\"uart\"}",
                         "{\"adapter\":\"ft232h\",\"timeout_ms\":0}",
                         "{\"adapter\":\"ft232h\",\"connections_checked\":1}",
                         "{\"adapter\":\"ft232h\",\"bus\":\"jtag\"}",
                         "{\"api\":2}",
                         "{\"unknown\":true}"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        json_object *o = json_tokener_parse(bad[i]);
        CHECK(!bc_comm_config(o, &e));
        json_object_put(o);
    }
    json_object *catalog = bc_comm_catalog();
    CHECK(json_object_array_length(catalog) == 9);
    json_object_put(catalog);
    json_object *profile =
                    json_tokener_parse("{\"communication\":{\"api\":1,\"bus\":\"i2c\",\"adapters\":"
                                       "[\"ch341t\",\"ft232h\"],\"max_hz\":100000}}"),
                *selection =
                    json_tokener_parse("{\"adapter\":\"ft232h\",\"connections_checked\":true}");
    c = bc_comm_resolve(profile, NULL, selection, &e);
    CHECK(c);
    json_object_put(c);
    json_object_object_add(selection, "frequency_hz", json_object_new_int(400000));
    CHECK(!bc_comm_resolve(profile, NULL, selection, &e));
    json_object_object_del(selection, "frequency_hz");
    json_object_object_add(bc_get(profile, "communication"), "max_timeout_ms",
                           json_object_new_int(1000));
    json_object_object_add(selection, "timeout_ms", json_object_new_int(5000));
    CHECK(!bc_comm_resolve(profile, NULL, selection, &e));
    json_object_object_del(selection, "timeout_ms");

    json_object_object_add(bc_get(profile, "communication"), "clock_stretching",
                           json_object_new_boolean(1));
    CHECK(!bc_comm_resolve(profile, NULL, selection, &e));
    json_object_object_del(bc_get(profile, "communication"), "clock_stretching");
    CHECK(!bc_comm_resolve(NULL, NULL, selection, &e));
    json_object_put(selection);
    json_object_put(profile);
    c = bc_comm_resolve(NULL, NULL, NULL, &e);
    CHECK(c);
    json_object_put(c);
    uint8_t bytes[4];
    size_t n = 0;
    CHECK(!bc_comm_hex("01a0 FF", bytes, sizeof bytes, &n, &e) && n == 3 && bytes[1] == 0xa0);
    CHECK(bc_comm_hex("0 1", bytes, sizeof bytes, &n, &e) < 0);
    CHECK(bc_comm_hex("0x48", bytes, sizeof bytes, &n, &e) < 0);
    CHECK(bc_comm_hex("0001020304", bytes, sizeof bytes, &n, &e) < 0);
    const char *invalid[] = {"{\"address\":0,\"write\":\"00\"}", "{\"address\":72,\"read\":4097}",
                             "{\"address\":72,\"read\":2,\"expect\":\"01\"}",
                             "{\"address\":72,\"read\":1,\"mask\":\"FF\"}",
                             "{\"address\":72,\"read\":1,\"repeat\":true}"};
    for (unsigned i = 0; i < 5; i++) {
        json_object *o = json_tokener_parse(invalid[i]);
        CHECK(bc_comm_operation_validate("i2c", o, &e) < 0);
        json_object_put(o);
    }
}

static void ch341(void) {
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    bc_error e = {{0}};
    bc_comm s = session(w, BC_CH341, "{}");
    CHECK(!bc_comm_protocol_init(&s, &e));
    CHECK(w->sent[0].size == 3 && !memcmp(w->sent[0].data, (uint8_t[]){0xaa, 0x60, 0}, 3));
    uint8_t zero = 0, answer = 0x5a;
    for (int i = 0; i < 3; i++)
        queue(w, &zero, 1);
    queue(w, &answer, 1);
    uint8_t reg = 1, result = 0;
    bc_comm_i2c_message m[] = {{0x48, 0, 1, &reg}, {0x48, BC_COMM_I2C_READ, 1, &result}};
    CHECK(!bc_comm_i2c(&s, m, 2, &e) && result == 0x5a);
    CHECK(count_command(w, 0xaa, 0x74) == 2 && count_command(w, 0xaa, 0x75) == 1);
    CHECK(w->at == w->nr);
    uint8_t nack = 0x80;
    queue(w, &nack, 1);
    size_t before = w->ns;
    CHECK(bc_comm_i2c(&s, m, 1, &e) < 0 && errno == ENXIO);
    CHECK(w->ns == before + 3 && count_command(w, 0xaa, 0x75) == 2);
    before = w->ns;
    m[0].size = 33;
    CHECK(bc_comm_i2c(&s, m, 1, &e) < 0 && w->ns == before);
    m[0].size = 1;
    w->short_write = 1;
    CHECK(bc_comm_i2c(&s, m, 1, &e) < 0 && s.lost);
    before = w->ns;
    CHECK(bc_comm_i2c(&s, m, 1, &e) < 0 && w->ns == before);
    json_object_put(s.config);
    free(w);
}

static void ft232h(void) {
    bc_error e = {{0}};
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    bc_comm s = session(w, BC_FT232H, "{\"adapter\":\"ft232h\"}");
    queue(w, (uint8_t[]){0xfa, 0xaa}, 2);
    CHECK(!bc_comm_protocol_init(&s, &e) && w->controls == 6 && s.actual_hz == 100000);
    uint8_t reg = 0, result[2] = {0};
    bc_comm_i2c_message m[] = {{0x48, 0, 1, &reg}, {0x48, 1, 2, result}};
    for (int i = 0; i < 3; i++)
        queue(w, (uint8_t[]){0}, 1);
    queue(w, (uint8_t[]){0x12}, 1);
    queue(w, (uint8_t[]){0x34}, 1);
    CHECK(!bc_comm_i2c(&s, m, 2, &e) && result[0] == 0x12 && result[1] == 0x34);
    CHECK(w->at == w->nr);
    unsigned starts = 0, stops = 0, ack = 0, nack = 0;
    for (size_t i = 0; i < w->ns; i++) {
        packet *p = &w->sent[i];
        if (p->size == 3072 && p->data[1] == 2 && p->data[769] == 3 && p->data[1537] == 1 &&
            p->data[2305] == 0)
            starts++;
        if (p->size == 2304 && p->data[1] == 0 && p->data[769] == 1 && p->data[1537] == 3)
            stops++;
        if (p->size == 9 && p->data[3] == 0x13) {
            if (p->data[5] == 0xff)
                nack++;
            else
                ack++;
        }
    }
    CHECK(starts == 2 && stops == 1 && ack == 1 && nack == 1);
    // Each data output must explicitly enable AD1 again after releasing SDA for ACK.
    unsigned writes = 0;
    for (size_t i = 0; i < w->ns; i++) {
        packet *p = &w->sent[i];
        if (p->size == 13 && p->data[3] == 0x11) {
            CHECK(p->data[0] == 0x80 && p->data[1] == 0 && p->data[2] == 3 && p->data[7] == 0x80 &&
                  p->data[9] == 1 && p->data[10] == 0x22);
            writes++;
        }
    }
    CHECK(writes == 3);
    queue(w, (uint8_t[]){1}, 1);
    size_t previous = w->ns;
    CHECK(bc_comm_i2c(&s, m, 1, &e) < 0 && errno == ENXIO && !s.lost);
    CHECK(w->ns > previous && w->sent[w->ns - 1].size == 2304);

    json_object_put(s.config);
    free(w);
    for (unsigned mode = 0; mode < 4; mode++)
        for (unsigned lsb = 0; lsb < 2; lsb++) {
            w = calloc(1, sizeof *w);
            CHECK(w);
            char text[180];
            snprintf(text, sizeof text,
                     "{\"adapter\":\"ft232h\",\"bus\":\"spi\",\"frequency_hz\":1000000,\"spi_"
                     "mode\":%u,\"lsb_first\":%s,\"chip_select\":4}",
                     mode, lsb ? "true" : "false");
            s = session(w, BC_FT232H, text);
            queue(w, (uint8_t[]){0xfa, 0xaa}, 2);
            CHECK(!bc_comm_protocol_init(&s, &e));
            queue(w, (uint8_t[]){0, 0xef, 0x40, 0x16}, 4);
            uint8_t tx[] = {0x9f, 0, 0, 0}, rx[4];
            CHECK(!bc_comm_spi(&s, tx, rx, 4, &e) && rx[1] == 0xef);
            packet *p = &w->sent[w->ns - 2];
            unsigned op = 0x30u | ((mode == 0 || mode == 3) ? 1u : 4u) | (lsb ? 8u : 0u);
            CHECK(p->size == 8 && p->data[0] == op && p->data[1] == 3 && p->data[3] == 0x9f);
            CHECK(w->sent[w->ns - 3].data[1] == ((mode & 2) ? 1 : 0));
            CHECK(w->sent[w->ns - 1].data[1] == (0x10 | ((mode & 2) ? 1 : 0)));
            json_object_put(s.config);
            free(w);
        }
}

static void mcp2221(void) {
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    bc_error e = {{0}};
    bc_comm s = session(w, BC_MCP2221, "{\"adapter\":\"mcp2221a\"}");
    uint8_t init[64] = {0x10, 0, 0, 0x20};
    queue(w, init, 64);
    CHECK(!bc_comm_protocol_init(&s, &e));
    CHECK(w->sent[0].data[4] == 118);
    response(w, 0x94, 0, 0, 0, NULL);
    response(w, 0x10, 0, 0x45, 0, NULL);
    response(w, 0x93, 0, 0, 0, NULL);
    response(w, 0x40, 0, 0x54, 1, (uint8_t[]){0x12});
    response(w, 0x40, 0, 0x55, 1, (uint8_t[]){0x34});
    response(w, 0x10, 0, 0, 0, NULL);
    uint8_t reg = 1, rx[2];
    bc_comm_i2c_message m[] = {{0x48, 0, 1, &reg}, {0x48, 1, 2, rx}};
    CHECK(!bc_comm_i2c(&s, m, 2, &e) && rx[0] == 0x12 && rx[1] == 0x34);
    CHECK(w->sent[1].data[0] == 0x94 && w->sent[1].data[3] == 0x90 && w->sent[1].data[4] == 1);
    CHECK(w->sent[3].data[0] == 0x93 && w->sent[3].data[3] == 0x91);
    CHECK(w->at == w->nr);
    response(w, 0x90, 1, 0, 0, NULL);
    response(w, 0x10, 0, 0, 0, NULL);
    CHECK(bc_comm_i2c(&s, m, 1, &e) < 0 && errno == EBUSY && s.lost);
    size_t before = w->ns;
    CHECK(bc_comm_i2c(&s, m, 1, &e) < 0 && w->ns == before);
    json_object_put(s.config);
    free(w);
}

static void cp2112(void) {
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    bc_error e = {{0}};
    bc_comm s = session(w, BC_CP2112, "{\"adapter\":\"cp2112\"}");
    CHECK(!bc_comm_protocol_init(&s, &e));
    CHECK(w->feature[0] == 6 && w->feature[1] == 0 && w->feature[2] == 1 && w->feature[3] == 0x86 &&
          w->feature[4] == 0xa0 && w->feature[6] == 0 && w->feature[11] == 1 &&
          w->feature[13] == 1);
    response(w, 0x16, 1, 2, 0, NULL);
    response(w, 0x16, 2, 5, 2, NULL);
    response(w, 0x13, 2, 0, 2, (uint8_t[]){0xab, 0xcd});
    uint8_t prefix[] = {0x12, 0x34}, rx[2];
    bc_comm_i2c_message m[] = {{0x50, 0, 2, prefix}, {0x50, 1, 2, rx}};
    CHECK(!bc_comm_i2c(&s, m, 2, &e) && rx[0] == 0xab && rx[1] == 0xcd);
    CHECK(!memcmp(w->sent[0].data, (uint8_t[]){0x11, 0xa0, 0, 2, 2, 0x12, 0x34}, 7));
    CHECK(w->sent[3].data[0] == 0x12 && w->sent[3].data[2] == 2);
    CHECK(w->at == w->nr);
    size_t before = w->ns;
    m[0].size = 17;
    CHECK(bc_comm_i2c(&s, m, 2, &e) < 0 && w->ns == before);
    m[0].size = 2;
    response(w, 0x16, 3, 0, 0, NULL);
    CHECK(bc_comm_i2c(&s, m, 1, &e) < 0 && errno == ENXIO && s.lost);
    json_object_put(s.config);
    free(w);
}

static int cancelled(void *p) {
    (void)p;
    return 1;
}

static void deadlines(void) {
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    bc_comm s = session(w, BC_CH341, "{}");
    bc_error e = {{0}};
    uint8_t b = 0;
    bc_comm_i2c_message m = {0x48, 0, 1, &b};
    s.cancel = cancelled;
    CHECK(bc_comm_i2c(&s, &m, 1, &e) < 0 && errno == ECANCELED && w->ns == 0);
    s.cancel = NULL;
    s.deadline = bc_comm_now() - 1;
    CHECK(bc_comm_check(&s, &e) < 0 && errno == ETIMEDOUT);
    json_object_put(s.config);
    free(w);
}

static void hid_frames(void) {
    bc_error e = {{0}};
    uint8_t p[64] = {0x16, 2, 5, 0, 0, 0, 2};
    CHECK(bc_comm_hid_normalize(BC_CP2112, p, 7, 64, &e) == 64);
    CHECK(bc_comm_hid_normalize(BC_CP2112, p, 6, 64, &e) < 0);
    memset(p, 0, sizeof p);
    p[0] = 0x13;
    p[1] = 2;
    p[2] = 2;
    p[3] = 0xab;
    p[4] = 0xcd;
    CHECK(bc_comm_hid_normalize(BC_CP2112, p, 5, 64, &e) == 64);
    CHECK(bc_comm_hid_normalize(BC_CP2112, p, 4, 64, &e) < 0);
    p[2] = 62;
    CHECK(bc_comm_hid_normalize(BC_CP2112, p, 64, 64, &e) < 0);
    CHECK(bc_comm_hid_normalize(BC_MCP2221, p, 63, 64, &e) < 0);
}

int main(void) {
    hid_frames();
    configs();
    ch341();
    ft232h();
    mcp2221();
    cp2112();
    deadlines();
    printf("Comunicação: %u verificações de contrato, vetores de protocolo, NACK, partial I/O, "
           "clock, repeated START, CS e cancelamento. Sem hardware.\n",
           checks);
    return 0;
}
