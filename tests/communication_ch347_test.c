#define _POSIX_C_SOURCE 200809L
#include "communication/internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "CH347 FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                     \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    uint8_t bytes[4200];
    size_t size;
} packet;

typedef struct {
    packet sent[300];
    size_t count;
    uint8_t input[16000];
    size_t used, at;
    unsigned feature_count;
} wire;

static int send_packet(void *context, const uint8_t *data, size_t n, unsigned timeout,
                       bc_error *e) {
    wire *w = context;
    (void)e;
    CHECK(timeout > 0 && n <= 4200 && w->count < 300);
    memcpy(w->sent[w->count].bytes, data, n);
    w->sent[w->count++].size = n;
    return (int)n;
}

static int read_packet(void *context, uint8_t *data, size_t n, unsigned timeout, bc_error *e) {
    wire *w = context;
    (void)e;
    CHECK(timeout > 0 && w->at < w->used);
    size_t count = w->used - w->at;
    if (count > n)
        count = n;
    if (count > 17)
        count = 17;
    memcpy(data, w->input + w->at, count);
    w->at += count;
    return (int)count;
}

static int feature(void *context, uint8_t *data, size_t n, int get, unsigned timeout, bc_error *e) {
    wire *w = context;
    (void)e;
    CHECK(!get && timeout > 0 && n == 12);
    CHECK(data[0] == 0 && data[1] == 0xcb && data[2] == 8 && data[3] == 0);
    CHECK(data[4] == 0 && data[5] == 0xc2 && data[6] == 1 && data[7] == 0); /* 115200 LE */
    CHECK(data[8] == 2 && data[9] == 2 && data[10] == 8 && data[11] == 32);
    w->feature_count++;
    return (int)n;
}

static void queue(wire *w, const uint8_t *data, size_t n) {
    CHECK(w->used + n <= sizeof w->input);
    memcpy(w->input + w->used, data, n);
    w->used += n;
}

static void reply(wire *w, uint8_t cmd, const uint8_t *data, size_t n) {
    uint8_t h[] = {cmd, (uint8_t)n, (uint8_t)(n >> 8)};
    queue(w, h, 3);
    queue(w, data, n);
}

static void init_reply(wire *w, int spi, int recent) {
    uint8_t info[] = {recent ? 0x41 : 0x40, 3, 0, 0}, zero = 0, cfg[26] = {0};
    cfg[21] = 0xef;
    cfg[25] = 0x5a;
    reply(w, 0xca, info, 4);
    if (spi) {
        if (recent)
            reply(w, 0xe1, &zero, 1);
        reply(w, 0xca, cfg, 26);
        reply(w, 0xc0, &zero, 1);
    }
}

static bc_comm session(wire *w, const char *text) {
    bc_error e = {{0}};
    json_object *o = json_tokener_parse(text), *c = bc_comm_config(o, &e);
    CHECK(c);
    json_object_put(o);
    bc_comm s = {0};
    s.kind = BC_CH347;
    s.config = c;
    s.ch347_hid = bc_num(c, "ch347_mode") == 2;
    s.io = (bc_comm_io){w, send_packet, read_packet, feature, NULL, NULL};
    bc_comm_deadline(&s);
    return s;
}

static unsigned count_command(wire *w, unsigned command) {
    unsigned count = 0;
    for (size_t k = 0; k < w->count; k++)
        if (w->sent[k].bytes[0] == command)
            count++;
    return count;
}

static int cancel(void *p) {
    (void)p;
    return 1;
}

static void configs(void) {
    const char *buses[] = {"uart", "i2c", "spi", "jtag"};
    const int modes[4][4] = {{1, 0, 0, 0}, {1, 1, 1, 0}, {1, 1, 1, 0}, {1, 1, 0, 1}};
    bc_error e = {{0}};
    for (int mode = 0; mode < 4; mode++)
        for (int bus = 0; bus < 4; bus++) {
            CHECK(bc_comm_ch347_supports(mode, buses[bus]) == modes[mode][bus]);
            char text[240];
            snprintf(text, sizeof text,
                     "{\"adapter\":\"ch347\",\"bus\":\"%s\",\"ch347_mode\":%d,\"device\":\"%s\","
                     "\"frequency_hz\":%d}",
                     buses[bus], mode, mode == 2 ? "/dev/hidraw9" : "COM9",
                     bus == 2   ? 1000000
                     : bus == 3 ? 1875000
                                : 20000);
            json_object *o = json_tokener_parse(text), *c = bc_comm_config(o, &e);
            CHECK((c != NULL) == modes[mode][bus]);
            if (c)
                json_object_put(c);
            json_object_put(o);
        }
    CHECK(bc_comm_ch347_mode(0x1a86, 0x55d9) == 0);
    CHECK(bc_comm_ch347_mode(0x1a86, 0x55db) == 1);
    CHECK(bc_comm_ch347_mode(0x1a86, 0x55dc) == 2);
    CHECK(bc_comm_ch347_mode(0x1a86, 0x55dd) == 3);
    CHECK(bc_comm_ch347_mode(0x1a86, 0x55de) == -1);
    CHECK(!strcmp(bc_comm_serial_adapter(0x1a86, 0x55d9), "ch347"));
    CHECK(!strcmp(bc_comm_serial_adapter(0x1a86, 0x55db), "ch347"));
    CHECK(!strcmp(bc_comm_serial_adapter(0x1a86, 0x55dd), "ch347"));
    CHECK(!strcmp(bc_comm_serial_adapter(0x1a86, 0x55dc), "serial"));
    const char *bad[] = {
        "{\"adapter\":\"ch347\",\"bus\":\"spi\",\"frequency_hz\":100000}",
        "{\"adapter\":\"ch347\",\"chip_select\":3}",
        "{\"adapter\":\"ch347\",\"frequency_hz\":50000}",
        "{\"adapter\":\"ch347\",\"bus\":\"uart\",\"ch347_mode\":2,\"rts\":true}",
        "{\"adapter\":\"ch347\",\"bus\":\"uart\",\"ch347_mode\":2,\"flow_control\":\"rtscts\"}",
        "{\"adapter\":\"ft232h\",\"ch347_mode\":1}"};
    for (unsigned k = 0; k < sizeof bad / sizeof *bad; k++) {
        json_object *o = json_tokener_parse(bad[k]);
        CHECK(!bc_comm_config(o, &e));
        json_object_put(o);
    }
}

static void hid_frames(void) {
    bc_error e = {{0}};
    uint8_t data[510], packet[513], result[510];
    for (unsigned k = 0; k < 510; k++)
        data[k] = (uint8_t)k;
    CHECK(bc_comm_ch347_hid_encode(data, 510, packet, &e) == 513);
    CHECK(packet[0] == 0 && packet[1] == 0xfe && packet[2] == 1 && packet[512] == 0xfd);
    CHECK(bc_comm_ch347_hid_decode(packet, 513, 1, result, &e) == 510 &&
          !memcmp(data, result, 510));
    CHECK(bc_comm_ch347_hid_decode(packet + 1, 512, 0, result, &e) == 510 &&
          !memcmp(data, result, 510));
    CHECK(bc_comm_ch347_hid_decode(packet, 512, 1, result, &e) < 0);
    packet[0] = 1;
    CHECK(bc_comm_ch347_hid_decode(packet, 513, 1, result, &e) < 0);
    packet[0] = 0;
    packet[1] = 0xff;
    packet[2] = 0x7f;
    CHECK(bc_comm_ch347_hid_decode(packet, 513, 1, result, &e) < 0);
    CHECK(bc_comm_ch347_hid_encode(data, 1, packet, &e) == 513 && packet[1] == 1 && packet[4] == 0);
    CHECK(bc_comm_ch347_hid_encode(data, 511, packet, &e) < 0);
    CHECK(bc_comm_ch347_hid_encode(data, 0, packet, &e) < 0);
}

static void i2c(int mode, int recent) {
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    char text[180];
    snprintf(text, sizeof text,
             "{\"adapter\":\"ch347\",\"bus\":\"i2c\",\"ch347_mode\":%d,\"frequency_hz\":20000}",
             mode);
    bc_comm s = session(w, text);
    bc_error e = {{0}};
    init_reply(w, 0, recent);
    CHECK(!bc_comm_protocol_init(&s, &e));
    CHECK(s.actual_hz == 20000);
    CHECK(count_command(w, 0xaa) == (unsigned)(recent ? 2 : 1));
    uint8_t prefix[] = {0x12, 0x34}, rx[2], acks[] = {1, 1, 1, 1}, answer[] = {0x56, 0x78};
    queue(w, acks, 4);
    queue(w, answer, 2);
    bc_comm_i2c_message m[] = {{0x48, 0, 2, prefix}, {0x48, BC_COMM_I2C_READ, 2, rx}};
    size_t start = w->count;
    CHECK(!bc_comm_i2c(&s, m, 2, &e));
    CHECK(!memcmp(rx, answer, 2));
    CHECK(w->sent[start].bytes[1] == 0x74 && w->sent[start + 1].bytes[1] == 0x81 &&
          w->sent[start + 1].bytes[2] == 0x90);
    CHECK(w->sent[start + 2].bytes[1] == 0x82 && w->sent[start + 3].bytes[1] == 0x74 &&
          w->sent[start + 4].bytes[2] == 0x91);
    CHECK(w->sent[start + 5].bytes[1] == 0xc1 && w->sent[start + 6].bytes[1] == 0xc0 &&
          w->sent[start + 7].bytes[1] == 0x75);
    uint8_t nack = 0;
    queue(w, &nack, 1);
    CHECK(bc_comm_i2c(&s, m, 2, &e) < 0 && errno == ENXIO);
    CHECK(w->sent[w->count - 1].bytes[1] == 0x75);
    size_t before = w->count;
    s.cancel = cancel;
    CHECK(bc_comm_i2c(&s, m, 2, &e) < 0 && errno == ECANCELED && w->count == before);
    json_object_put(s.config);
    free(w);
}

static void spi(int mode, int recent) {
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    char text[200];
    snprintf(text, sizeof text,
             "{\"adapter\":\"ch347\",\"bus\":\"spi\",\"ch347_mode\":%d,\"frequency_hz\":1000000,"
             "\"chip_select\":1,\"spi_mode\":3,\"lsb_first\":true}",
             mode);
    bc_comm s = session(w, text);
    bc_error e = {{0}};
    init_reply(w, 1, recent);
    CHECK(!bc_comm_protocol_init(&s, &e));
    CHECK(s.actual_hz == 937500);
    packet *cfg = &w->sent[w->count - 2];
    CHECK(cfg->size == 29 && cfg->bytes[0] == 0xc0 && cfg->bytes[9] == 2 && cfg->bytes[11] == 1 &&
          cfg->bytes[15] == 48 && cfg->bytes[17] == 0x80 && cfg->bytes[24] == 0x2f &&
          cfg->bytes[28] == 0x5a);
    uint8_t tx[4096], rx[4096];
    for (unsigned k = 0; k < 4096; k++)
        tx[k] = (uint8_t)k;
    size_t block = mode == 2 ? 507 : 4093;
    for (size_t at = 0; at < 4096;) {
        size_t n = 4096 - at;
        if (n > block)
            n = block;
        reply(w, 0xc2, tx + at, n);
        at += n;
    }
    CHECK(!bc_comm_spi(&s, tx, rx, 4096, &e) && !memcmp(tx, rx, 4096));
    CHECK(count_command(w, 0xc2) == (mode == 2 ? 9u : 2u));
    CHECK(w->sent[w->count - 1].bytes[8] == 0xc0);
    uint8_t bad[] = {0xc2, 2, 0};
    queue(w, bad, 3);
    CHECK(bc_comm_spi(&s, tx, rx, 1, &e) < 0 && s.lost == EIO);
    CHECK(w->sent[w->count - 1].bytes[8] == 0xc0);
    size_t before = w->count;
    CHECK(bc_comm_spi(&s, tx, rx, 1, &e) < 0 && w->count == before);
    json_object_put(s.config);
    free(w);
}

static void uart(void) {
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    bc_comm s = session(w, "{\"adapter\":\"ch347\",\"bus\":\"uart\",\"ch347_mode\":2,\"baud\":"
                           "115200,\"stop_bits\":2,\"parity\":\"even\"}");
    bc_error e = {{0}};
    CHECK(!bc_comm_protocol_init(&s, &e) && w->feature_count == 1 && s.actual_hz == 0);
    uint8_t tx[] = {0xa5, 0x5a}, rx[2];
    queue(w, tx, 2);
    CHECK(!bc_comm_uart(&s, tx, 2, rx, 2, &e) && !memcmp(tx, rx, 2));
    CHECK(w->at == w->used);
    json_object_put(s.config);
    free(w);
}

static void jtag(int newer) {
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    bc_comm s = session(
        w, "{\"adapter\":\"ch347\",\"bus\":\"jtag\",\"ch347_mode\":3,\"frequency_hz\":1875000}");
    bc_error e = {{0}};
    uint8_t support = (uint8_t)newer;
    reply(w, 0xd0, &support, 1);
    reply(w, 0xd0, &support, 1);
    CHECK(!bc_comm_protocol_init(&s, &e) && s.actual_hz == 1875000);
    CHECK(w->sent[0].size == 9 && w->sent[0].bytes[4] == 9);
    CHECK(w->sent[1].bytes[4] == (newer ? 2 : 0));
    CHECK(w->sent[2].bytes[0] == 0xd1 && w->sent[2].bytes[1] == 15 && w->sent[2].bytes[3] == 2 &&
          w->sent[2].bytes[4] == 3 && w->sent[2].bytes[15] == 0);
    uint8_t samples[13];
    for (unsigned k = 0; k < 13; k++)
        samples[k] = (uint8_t)((0x1aa5u >> k) & 1u);
    reply(w, 0xd2, samples, 13);
    json_object *o = json_tokener_parse("{\"register\":\"ir\",\"bits\":13,\"write\":\"A5 "
                                        "1A\",\"expect\":\"A5 FA\",\"idle_cycles\":3}");
    size_t start = w->count;
    json_object *r = bc_comm_transaction(&s, o, &e);
    CHECK(r && json_object_get_boolean(bc_get(r, "matched")) &&
          !strcmp(bc_str(r, "received"), "A5 1A"));
    CHECK(w->sent[start].bytes[1] == 9 && w->sent[start].bytes[3] == 2 &&
          w->sent[start].bytes[5] == 2);
    CHECK(w->sent[start + 1].bytes[0] == 0xd2 && w->sent[start + 1].bytes[1] == 27 &&
          (w->sent[start + 1].bytes[27] & 2));
    json_object_put(o);
    json_object_put(r);
    uint8_t low = 0;
    reply(w, 0xd2, &low, 1);
    o = json_tokener_parse("{\"register\":\"dr\",\"bits\":1}");
    start = w->count;
    r = bc_comm_transaction(&s, o, &e);
    CHECK(r && !strcmp(bc_str(r, "received"), "00"));
    CHECK(w->sent[start].bytes[1] == 7);
    json_object_put(o);
    json_object_put(r);
    uint8_t instruction_samples[5] = {0};
    reply(w, 0xd2, instruction_samples, 5);
    reply(w, 0xd2, &low, 1);
    o = json_tokener_parse("{\"register\":\"dr\",\"bits\":1,\"ir_bits\":5,\"ir_write\":\"09\"}");
    start = w->count;
    r = bc_comm_transaction(&s, o, &e);
    CHECK(r && !strcmp(bc_str(r, "received"), "00") && count_command(w, 0xd0) == 2 &&
          bc_num(r, "bytes_written") == 2 && bc_num(r, "ir_bits") == 5 && bc_num(r, "bits") == 1);
    CHECK(w->sent[start].bytes[1] == 9 && w->sent[start + 3].bytes[1] == 7);
    json_object_put(o);
    json_object_put(r);
    o = json_tokener_parse("{\"register\":\"reset\"}");
    r = bc_comm_transaction(&s, o, &e);
    CHECK(r && bc_num(r, "bytes_read") == 0);
    json_object_put(o);
    json_object_put(r);
    const char *bad[] = {"{\"bits\":1,\"ir_bits\":5,\"ir_write\":\"FF\"}",
                         "{\"register\":\"ir\",\"bits\":1,\"ir_bits\":5,\"ir_write\":\"09\"}",
                         "{\"bits\":1e300}",
                         "{\"bits\":\"13\"}",
                         "{\"bits\":0}",
                         "{\"bits\":32769}",
                         "{\"bits\":13,\"write\":\"A5 FA\"}",
                         "{\"bits\":13,\"read\":1}",
                         "{\"register\":\"reset\",\"write\":\"00\"}",
                         "{\"register\":\"idle\",\"idle_cycles\":0}",
                         "{\"register\":\"dr\",\"bits\":8,\"address\":1}"};
    for (unsigned k = 0; k < sizeof bad / sizeof *bad; k++) {
        o = json_tokener_parse(bad[k]);
        CHECK(bc_comm_operation_check(s.config, NULL, o, &e) < 0);
        json_object_put(o);
    }
    json_object_put(s.config);
    free(w);
}

static void identify_chain(void) {
    bc_error e = {{0}};
    wire *w = calloc(1, sizeof *w);
    CHECK(w);
    bc_comm s = session(w, "{\"adapter\":\"ch347\",\"bus\":\"jtag\",\"ch347_mode\":3,\"frequency_"
                           "hz\":1875000,\"connections_checked\":true}");
    uint8_t packed[20];
    memset(packed, 255, sizeof packed);
    uint32_t ids[] = {0x4ba00477, 0, 0x12345679};
    unsigned bit = 0;
    for (unsigned i = 0; i < 3; i++) {
        unsigned bits = ids[i] ? 32 : 1;
        for (unsigned k = 0; k < bits; k++, bit++) {
            packed[bit / 8] &= (uint8_t)~(1u << (bit % 8));
            if ((ids[i] >> k) & 1u)
                packed[bit / 8] |= (uint8_t)(1u << (bit % 8));
        }
    }
    for (unsigned at = 0; at < 160;) {
        unsigned n = 160 - at;
        if (n > 128)
            n = 128;
        uint8_t samples[128];
        for (unsigned k = 0; k < n; k++)
            samples[k] = (uint8_t)((packed[(at + k) / 8] >> ((at + k) % 8)) & 1u);
        reply(w, 0xd2, samples, n);
        at += n;
    }
    json_object *r = bc_comm_jtag_identify(&s, 4, &e);
    CHECK(r && json_object_get_boolean(bc_get(r, "complete")));
    json_object *taps = bc_get(r, "taps");
    CHECK(json_object_array_length(taps) == 3 &&
          !strcmp(bc_str(json_object_array_get_idx(taps, 0), "idcode"), "0x4BA00477") &&
          json_object_get_boolean(bc_get(json_object_array_get_idx(taps, 1), "bypass")) &&
          !strcmp(bc_str(json_object_array_get_idx(taps, 2), "idcode"), "0x12345679"));
    json_object_put(r);
    CHECK(w->at == w->used);
    CHECK(!bc_comm_jtag_identify(&s, 0, &e));
    CHECK(!bc_comm_jtag_identify(&s, 33, &e));
    int stop = 1;
    s.cancel = cancel;
    s.cancel_context = &stop;
    CHECK(!bc_comm_jtag_identify(&s, 4, &e) && errno == ECANCELED);
    json_object_put(s.config);
    free(w);
    for (unsigned high = 0; high < 2; high++) {
        w = calloc(1, sizeof *w);
        s = session(w, "{\"adapter\":\"ch347\",\"bus\":\"jtag\",\"ch347_mode\":3}");
        uint8_t samples[96];
        memset(samples, high, sizeof samples);
        reply(w, 0xd2, samples, sizeof samples);
        r = bc_comm_jtag_identify(&s, 2, &e);
        CHECK(r && !json_object_get_boolean(bc_get(r, "complete")));
        json_object_put(r);
        json_object_put(s.config);
        free(w);
    }
}

int main(void) {
    identify_chain();
    configs();
    hid_frames();
    for (int mode = 1; mode <= 3; mode++)
        for (int recent = 0; recent <= 1; recent++)
            i2c(mode, recent);
    spi(1, 0);
    spi(1, 1);
    spi(2, 1);
    uart();
    jtag(0);
    jtag(1);
    printf("CH347T: %u verificações; quatro modos, I²C/ACK/repeated START, SPI/CS, HID, UART e "
           "JTAG/TAP/IR/DR, fragmentação, falha e cancelamento; sem hardware.\n",
           checks);
    return 0;
}
