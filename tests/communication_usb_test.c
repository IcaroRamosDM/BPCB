#define _POSIX_C_SOURCE 200809L
#include "communication/internal.h"
#include <assert.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int devices = 1, active, starts, exits, opens, closes, claims, releases, detachments,
           attachments, controls, writes, read_error, short_write;
static unsigned mock_vid = 0x0403, mock_pid = 0x6014;
static int mock_interface;
static libusb_device *items[] = {(libusb_device *)(uintptr_t)1, (libusb_device *)(uintptr_t)2,
                                 NULL};
static uint8_t incoming[20000];
static size_t size, at;
static libusb_device_handle *last_handle;

int libusb_init(libusb_context **ctx) {
    starts++;
    *ctx = (libusb_context *)(uintptr_t)1;
    return 0;
}

void libusb_exit(libusb_context *ctx) {
    (void)ctx;
    exits++;
}

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***out) {
    (void)ctx;
    *out = items;
    return devices;
}

void libusb_free_device_list(libusb_device **list, int unref) {
    (void)list;
    (void)unref;
}

libusb_device *libusb_ref_device(libusb_device *d) {
    return d;
}

void libusb_unref_device(libusb_device *d) {
    (void)d;
}

int libusb_get_device_descriptor(libusb_device *d, struct libusb_device_descriptor *out) {
    (void)d;
    memset(out, 0, sizeof *out);
    out->idVendor = (uint16_t)mock_vid;
    out->idProduct = (uint16_t)mock_pid;
    out->iSerialNumber = 1;
    return 0;
}

uint8_t libusb_get_bus_number(libusb_device *d) {
    (void)d;
    return 1;
}

uint8_t libusb_get_device_address(libusb_device *d) {
    return (uint8_t)(uintptr_t)d;
}

int libusb_get_port_numbers(libusb_device *d, uint8_t *p, int cap) {
    assert(cap > 0);
    p[0] = (uint8_t)(uintptr_t)d;
    return 1;
}

int libusb_open(libusb_device *d, libusb_device_handle **h) {
    opens++;
    *h = (libusb_device_handle *)((uintptr_t)d + 10);
    last_handle = *h;
    return 0;
}

void libusb_close(libusb_device_handle *h) {
    (void)h;
    closes++;
}

int libusb_get_string_descriptor_ascii(libusb_device_handle *h, uint8_t index, unsigned char *data,
                                       int cap) {
    (void)index;
    assert(cap >= 2);
    data[0] = (unsigned char)(h == (libusb_device_handle *)(uintptr_t)11 ? 'A' : 'B');
    data[1] = 0;
    return 1;
}

int libusb_get_active_config_descriptor(libusb_device *d, struct libusb_config_descriptor **out) {
    (void)d;
    static const struct libusb_endpoint_descriptor endpoints[] = {
        {.bEndpointAddress = 0x02,
         .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK,
         .wMaxPacketSize = 512},
        {.bEndpointAddress = 0x82,
         .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK,
         .wMaxPacketSize = 512}};
    static struct libusb_interface_descriptor descriptor = {
        .bInterfaceNumber = 0, .bAlternateSetting = 0, .bNumEndpoints = 2, .endpoint = endpoints};
    static const struct libusb_interface interface = {.altsetting = &descriptor,
                                                      .num_altsetting = 1};
    static struct libusb_config_descriptor config = {.bNumInterfaces = 1, .interface = &interface};
    descriptor.bInterfaceNumber = (uint8_t)mock_interface;
    *out = &config;
    return 0;
}

void libusb_free_config_descriptor(struct libusb_config_descriptor *d) {
    (void)d;
}

int libusb_kernel_driver_active(libusb_device_handle *d, int interface) {
    (void)d;
    assert(interface == mock_interface);
    return active;
}

int libusb_detach_kernel_driver(libusb_device_handle *d, int interface) {
    (void)d;
    assert(interface == mock_interface);
    detachments++;
    return 0;
}

int libusb_attach_kernel_driver(libusb_device_handle *d, int interface) {
    (void)d;
    assert(interface == mock_interface);
    attachments++;
    return 0;
}

int libusb_claim_interface(libusb_device_handle *d, int interface) {
    (void)d;
    assert(interface == mock_interface);
    claims++;
    return 0;
}

int libusb_release_interface(libusb_device_handle *d, int interface) {
    (void)d;
    assert(interface == mock_interface);
    releases++;
    return 0;
}

int libusb_control_transfer(libusb_device_handle *d, uint8_t type, uint8_t request, uint16_t value,
                            uint16_t index, unsigned char *data, uint16_t n, unsigned timeout) {
    (void)d;
    (void)request;
    (void)value;
    (void)data;
    (void)n;
    assert(type == 0x40 && index == 1 && timeout > 0);
    controls++;
    return 0;
}

int libusb_bulk_transfer(libusb_device_handle *d, unsigned char endpoint, unsigned char *data,
                         int n, int *actual, unsigned timeout) {
    (void)d;
    assert(timeout > 0);
    if (endpoint == 2) {
        writes++;
        *actual = short_write ? n - 1 : n;
        short_write = 0;
        return 0;
    }
    if (read_error) {
        *actual = 0;
        int rc = read_error;
        read_error = 0;
        return rc;
    }
    assert(at < size);
    size_t count = size - at;
    if (count > (size_t)n)
        count = (size_t)n;
    memcpy(data, incoming + at, count);
    at += count;
    *actual = (int)count;
    return 0;
}

static void payload(const uint8_t *data, size_t n) {
    at = size = 0;
    for (size_t i = 0; i < n;) {
        size_t chunk = n - i;
        if (chunk > 510)
            chunk = 510;
        incoming[size++] = 1;
        incoming[size++] = 0x60;
        memcpy(incoming + size, data + i, chunk);
        size += chunk;
        i += chunk;
    }
}

static json_object *config(const char *text) {
    return json_tokener_parse(text);
}

static int cancel(void *p) {
    (void)p;
    return 1;
}

int main(void) {
    bc_error e = {{0}};
    json_object *c = config("{\"adapter\":\"ft232h\",\"bus\":\"spi\",\"device\":\"usb:0403:6014:1-"
                            "2\",\"connections_checked\":true}");
    bc_comm *s = NULL;
    devices = 2;
    json_object *p = bc_comm_probe(c, NULL, NULL, &e);
    assert(p && controls == 0 && claims == 0 &&
           last_handle == (libusb_device_handle *)(uintptr_t)12);
    json_object_put(p);
    json_object_object_del(c, "device");
    assert(bc_comm_open(&s, c, NULL, NULL, &e) < 0 && errno == EEXIST && s == NULL &&
           controls == 0);
    json_object_object_add(c, "serial", json_object_new_string("B"));
    payload((uint8_t[]){0xfa, 0xaa}, 2);
    assert(!bc_comm_open(&s, c, NULL, NULL, &e) &&
           last_handle == (libusb_device_handle *)(uintptr_t)12 && controls == 6);
    json_object *information = bc_comm_information(s);
    assert(!strcmp(bc_str(bc_get(information, "configuration"), "device"), "usb:0403:6014:1-2"));
    assert(!strcmp(bc_str(bc_get(information, "configuration"), "serial"), "B"));
    json_object_put(information);
    uint8_t tx[1024], rx[1024], answer[1024];
    memset(tx, 0, sizeof tx);
    for (unsigned i = 0; i < 1024; i++)
        answer[i] = (uint8_t)i;
    payload(answer, sizeof answer);
    assert(!bc_comm_spi(s, tx, rx, sizeof rx, &e) && !memcmp(rx, answer, sizeof rx));
    read_error = LIBUSB_ERROR_NO_DEVICE;
    assert(bc_comm_spi(s, tx, rx, 1, &e) < 0 && errno == ENODEV);
    int before = writes;
    assert(bc_comm_spi(s, tx, rx, 1, &e) < 0 && writes == before);
    bc_comm_close(s);
    s = NULL;
    active = 1;
    assert(bc_comm_open(&s, c, NULL, NULL, &e) < 0 && errno == EBUSY && detachments == 0);
    json_object_object_add(c, "allow_kernel_detach", json_object_new_boolean(1));
    payload((uint8_t[]){0xfa, 0xaa}, 2);
    assert(!bc_comm_open(&s, c, NULL, NULL, &e) && detachments == 1);
    bc_comm_close(s);
    s = NULL;
    assert(attachments == 1);
    active = 0;
    short_write = 1;
    assert(bc_comm_open(&s, c, NULL, NULL, &e) < 0 && s == NULL);
    int previous = controls;
    assert(!bc_comm_probe(c, cancel, NULL, &e) && controls == previous);
    json_object_put(c);
    mock_vid = 0x1a86;
    mock_pid = 0x55db;
    mock_interface = 2;
    devices = 1;
    c = config("{\"adapter\":\"ch347\",\"bus\":\"i2c\",\"frequency_hz\":20000}");
    before = writes;
    previous = claims;
    p = bc_comm_probe(c, NULL, NULL, &e);
    assert(p && writes == before && claims == previous &&
           bc_num(bc_get(p, "configuration"), "ch347_mode") == 1);
    json_object_put(p);
    devices = 2;
    assert(!bc_comm_probe(c, NULL, NULL, &e) && errno == EEXIST && claims == previous &&
           writes == before);
    devices = 1;
    at = 0;
    size = 7;
    memcpy(incoming, (uint8_t[]){0xca, 4, 0, 0x41, 3, 0, 0}, 7);
    assert(!bc_comm_open(&s, c, NULL, NULL, &e) && claims == previous + 1 && s->kind == BC_CH347 &&
           bc_num(s->config, "ch347_mode") == 1);
    uint8_t reg = 0;
    bc_comm_i2c_message message = {0x48, 0, 1, &reg};
    at = 0;
    size = 2;
    incoming[0] = incoming[1] = 1;
    assert(!bc_comm_i2c(s, &message, 1, &e));
    bc_comm_close(s);
    s = NULL;
    json_object_put(c);
    mock_pid = 0x55dd;
    c = config(
        "{\"adapter\":\"ch347\",\"bus\":\"jtag\",\"ch347_mode\":3,\"frequency_hz\":1875000}");
    at = 0;
    size = 8;
    memcpy(incoming, (uint8_t[]){0xd0, 1, 0, 0, 0xd0, 1, 0, 0}, 8);
    assert(!bc_comm_open(&s, c, NULL, NULL, &e) && bc_num(s->config, "ch347_mode") == 3 &&
           s->actual_hz == 1875000);
    bc_comm_close(s);
    s = NULL;
    mock_pid = 0x55db;
    previous = claims;
    before = writes;
    assert(!bc_comm_probe(c, NULL, NULL, &e) && claims == previous && writes == before);
    json_object_put(c);
    mock_pid = 0x55d9;
    c = config("{\"adapter\":\"ch347\",\"bus\":\"i2c\"}");
    assert(!bc_comm_probe(c, NULL, NULL, &e) && claims == previous && writes == before);
    json_object_put(c);
    mock_pid = 0x55dc;
    c = config("{\"adapter\":\"ch347\",\"bus\":\"spi\",\"frequency_hz\":1000000}");
    assert(!bc_comm_probe(c, NULL, NULL, &e) && claims == previous && writes == before);
    json_object_put(c);
    assert(starts == exits && opens == closes && claims == releases && detachments == attachments);
    puts("USB simulado: identidade por porta/série, ambiguidade recusada, probe sem comandos, "
         "status FTDI a cada pacote, desconexão sem reconexão, detach explícito e recursos "
         "liberados. CH347T usa somente a interface 2; M1/M3 compatíveis e M0/M2 sem fallback "
         "vendor.");
    return 0;
}
