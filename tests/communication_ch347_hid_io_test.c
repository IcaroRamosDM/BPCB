#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
/* Exercise the production HID framing/read-ahead over packet-preserving local
 * sockets. No hidraw device, USB adapter, or target PCB is opened. */
#include "../core/communication/hid.c"
#include <assert.h>
#include <sys/socket.h>

static int cancelled(void *context) {
    (void)context;
    return 1;
}

int main(void) {
    int sockets[2];
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0, sockets));
    bc_error e = {{0}};
    json_object *raw =
        json_tokener_parse("{\"adapter\":\"ch347\",\"bus\":\"uart\",\"ch347_mode\":2}");
    bc_comm s = {0};
    s.kind = BC_CH347;
    s.config = bc_comm_config(raw, &e);
    json_object_put(raw);
    assert(s.config);
    bc_comm_deadline(&s);
    hid_io h = {0};
    h.session = &s;
    h.fd = sockets[0];
    h.lock = -1;
    uint8_t tx[4096], rx[4096], packet[513];
    for (unsigned k = 0; k < 4096; k++)
        tx[k] = (uint8_t)k;
    assert(hid_write(&h, tx, sizeof tx, 1000, &e) == 4096);
    size_t at = 0;
    unsigned packets = 0;
    while (at < sizeof tx) {
        assert(read(sockets[1], packet, sizeof packet) == 513 && packet[0] == 0);
        unsigned n = packet[1] | ((unsigned)packet[2] << 8);
        assert(n == (sizeof tx - at > 510 ? 510 : sizeof tx - at));
        assert(!memcmp(packet + 3, tx + at, n));
        at += n;
        packets++;
    }
    assert(packets == 9);
    memset(packet, 0, 512);
    assert(write(sockets[1], packet, 512) == 512); /* Empty report is not EOF. */
    at = 0;
    while (at < sizeof tx) {
        size_t n = sizeof tx - at;
        if (n > 510)
            n = 510;
        memset(packet, 0, 512);
        packet[0] = (uint8_t)n;
        packet[1] = (uint8_t)(n >> 8);
        memcpy(packet + 2, tx + at, n);
        assert(write(sockets[1], packet, 512) == 512);
        at += n;
    }
    at = 0;
    while (at < sizeof rx) {
        int n = hid_read(&h, rx + at, 3, 1000, &e);
        assert(n > 0 && n <= 3);
        at += (size_t)n;
    }
    assert(!memcmp(rx, tx, 4096) && h.pending_at == h.pending_size);
    memset(packet, 0, 512);
    packet[0] = 0xff;
    packet[1] = 0x7f;
    assert(write(sockets[1], packet, 512) == 512);
    assert(hid_read(&h, rx, 1, 1000, &e) < 0 && errno == EIO);
    s.cancel = cancelled;
    assert(hid_read(&h, rx, 1, 1000, &e) < 0 && errno == ECANCELED);
    s.cancel = NULL;
    s.deadline = bc_comm_now() + 0.05;
    assert(hid_read(&h, rx, 1, 50, &e) < 0 && errno == ETIMEDOUT);
    bc_comm_deadline(&s);
    close(sockets[1]);
    assert(hid_read(&h, rx, 1, 1000, &e) < 0 && errno == ENODEV);
    close(sockets[0]);
    json_object_put(s.config);
    puts("CH347 HID I/O: quadros de 513 bytes/ID zero, UART 4096 bytes em nove relatórios, entrada "
         "vazia, leitura fragmentada sem perda, limite, cancelamento e desconexão verificados com "
         "sockets locais; sem hardware.");
    return 0;
}
