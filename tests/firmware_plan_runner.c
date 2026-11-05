#define _POSIX_C_SOURCE 200809L
#include "lab.h"
#include <stdio.h>
#include <string.h>

json_object *__wrap_bc_openocd_run(json_object *request, int (*cancel)(void *), void *ctx,
                                   bc_error *e) {
    (void)cancel;
    (void)ctx;
    const char *file = bc_str(request, "firmware");
    if (!strstr(file, "bpcb-firmware-") || !strstr(file, "combined.hex")) {
        bc_fail(e, "Teste recusou arquivo fora do snapshot HEX.");
        return NULL;
    }
    FILE *f = fopen(file, "rb");
    if (!f)
        return NULL;
    char data[8192];
    size_t n = fread(data, 1, sizeof data - 1, f);
    int more = fgetc(f);
    fclose(f);
    if (more != EOF) {
        bc_fail(e, "Fixture HEX excedida.");
        return NULL;
    }
    data[n] = 0;
    json_object *r = json_object_new_object();
    json_object_object_add(r, "ok", json_object_new_boolean(1));
    json_object_object_add(r, "exit_code", json_object_new_int(0));
    json_object_object_add(r, "hex_fixture", json_object_new_string(data));
    json_object_object_add(
        r, "log",
        json_object_new_string("OpenOCD substituído por fixture; sem acesso ao hardware."));
    return r;
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    bc_error e = {{0}};
    json_object *q = bc_read_json(argv[1], &e), *r = q ? bc_firmware_run(q, NULL, NULL, &e) : NULL;
    json_object_put(q);
    if (!r) {
        fprintf(stderr, "%s\n", e.text);
        return 1;
    }
    puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
    json_object_put(r);
    return 0;
}
