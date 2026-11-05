#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include "communication.h"
#include "lab.h"
#include "workbench.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile sig_atomic_t stop;

static void cancel(int s) {
    (void)s;
    stop = 1;
}

static int communication_cancelled(void *context) {
    (void)context;
    return stop;
}

int main(int argc, char **argv) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    signal(SIGTERM, cancel);
    signal(SIGINT, cancel);
    bc_error e = {{0}};
    if (argc == 3 && !strcmp(argv[1], "firmware-bundle-inspect")) {
        json_object *r = bc_firmware_bundle_inspect(argv[2], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "firmware-bundle-export")) {
        json_object *m = bc_read_json(argv[2], &e);
        int rc = m ? bc_firmware_bundle_export(m, argv[3], &e) : -1;
        json_object_put(m);
        if (rc)
            fprintf(stderr, "%s\n", e.text);
        return rc ? 1 : 0;
    }
    if (argc == 5 && !strcmp(argv[1], "firmware-bundle-import")) {
        json_object *r = bc_firmware_bundle_import(argv[2], argv[3], argv[4], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "frame-decode")) {
        json_object *s = bc_read_json(argv[2], &e);
        uint8_t bytes[4096];
        size_t n = 0;
        json_object *r = s && bc_comm_hex(argv[3], bytes, sizeof bytes, &n, &e) == 0
                             ? bc_frame_decode(s, bytes, n, "", &e)
                             : NULL;
        json_object_put(s);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        int rc = !json_object_get_boolean(bc_get(r, "matched"));
        json_object_put(r);
        return rc;
    }
    if (argc == 3 && !strcmp(argv[1], "compatibility-list")) {
        json_object *r = bc_compatibility_list(argv[2], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "lab-catalog")) {
        json_object *r = bc_peripheral_catalog();
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 3 && (!strcmp(argv[1], "firmware-plan") || !strcmp(argv[1], "firmware-run") ||
                      !strcmp(argv[1], "replay-import"))) {
        json_object *input = bc_read_json(argv[2], &e), *r = NULL;
        if (input)
            r = !strcmp(argv[1], "firmware-plan") ? bc_firmware_plan(input, &e)
                : !strcmp(argv[1], "firmware-run")
                    ? bc_firmware_run(input, communication_cancelled, NULL, &e)
                    : bc_replay_from_capture(input, &e);
        json_object_put(input);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        int failed = bc_get(r, "ok") && !json_object_get_boolean(bc_get(r, "ok"));
        json_object_put(r);
        return failed;
    }
    if ((argc == 4 || argc == 5) && !strcmp(argv[1], "replay")) {
        json_object *capture = bc_read_json(argv[2], &e),
                    *op = capture ? bc_read_json(argv[3], &e) : NULL,
                    *fault = argc == 5 && op ? bc_read_json(argv[4], &e) : NULL,
                    *r = op && (argc != 5 || fault) ? bc_replay_execute(capture, op, fault, &e)
                                                    : NULL;
        json_object_put(capture);
        json_object_put(op);
        json_object_put(fault);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        int failed = strcmp(bc_str(r, "status"), "pass") != 0;
        json_object_put(r);
        return failed;
    }
    if (argc == 4 && (!strcmp(argv[1], "qualify-plan") || !strcmp(argv[1], "qualify-assess"))) {
        json_object *input = bc_read_json(argv[2], &e),
                    *answers = !strcmp(argv[1], "qualify-assess") && input
                                   ? bc_read_json(argv[3], &e)
                                   : NULL,
                    *r = input ? (!strcmp(argv[1], "qualify-plan")
                                      ? bc_qualification_plan(input, argv[3], &e)
                                  : answers ? bc_qualification_assess(input, answers, &e)
                                            : NULL)
                               : NULL;
        json_object_put(input);
        json_object_put(answers);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }

    if (argc == 3 && !strcmp(argv[1], "firmware-bundle-inspect")) {
        json_object *r = bc_firmware_bundle_inspect(argv[2], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "firmware-bundle-export")) {
        json_object *m = bc_read_json(argv[2], &e);
        int rc = m ? bc_firmware_bundle_export(m, argv[3], &e) : -1;
        json_object_put(m);
        if (rc)
            fprintf(stderr, "%s\n", e.text);
        return rc ? 1 : 0;
    }
    if (argc == 5 && !strcmp(argv[1], "firmware-bundle-import")) {
        json_object *r = bc_firmware_bundle_import(argv[2], argv[3], argv[4], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "frame-decode")) {
        json_object *s = bc_read_json(argv[2], &e);
        uint8_t bytes[4096];
        size_t n = 0;
        json_object *r = s && bc_comm_hex(argv[3], bytes, sizeof bytes, &n, &e) == 0
                             ? bc_frame_decode(s, bytes, n, "", &e)
                             : NULL;
        json_object_put(s);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        int rc = !json_object_get_boolean(bc_get(r, "matched"));
        json_object_put(r);
        return rc;
    }
    if (argc == 3 && !strcmp(argv[1], "compatibility-list")) {
        json_object *r = bc_compatibility_list(argv[2], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "comm-catalog") || !strcmp(argv[1], "comm-list"))) {
        json_object *r = !strcmp(argv[1], "comm-catalog")
                             ? bc_comm_catalog()
                             : bc_comm_list(communication_cancelled, NULL, &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "comm-probe")) {
        json_object *c = bc_read_json(argv[2], &e),
                    *r = c ? bc_comm_probe(c, communication_cancelled, NULL, &e) : NULL;
        if (c)
            json_object_put(c);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "comm-exchange")) {
        json_object *c = bc_read_json(argv[2], &e), *o = c ? bc_read_json(argv[3], &e) : NULL,
                    *r = o ? bc_comm_exchange(c, o, communication_cancelled, NULL, &e) : NULL;
        if (c)
            json_object_put(c);
        if (o)
            json_object_put(o);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        int rc = bc_get(r, "matched") && !json_object_get_boolean(bc_get(r, "matched"));
        json_object_put(r);
        return rc;
    }
    if (argc == 3 && (!strcmp(argv[1], "openocd-plan") || !strcmp(argv[1], "openocd-run") ||
                      !strcmp(argv[1], "comm-profiles"))) {
        json_object *request = NULL, *r = NULL;
        if (!strcmp(argv[1], "comm-profiles"))
            r = bc_comm_profiles(argv[2], &e);
        else {
            request = bc_read_json(argv[2], &e);
            if (request)
                r = !strcmp(argv[1], "openocd-plan")
                        ? bc_openocd_plan(request, &e)
                        : bc_openocd_run(request, communication_cancelled, NULL, &e);
        }
        json_object_put(request);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        int rc = bc_get(r, "ok") && !json_object_get_boolean(bc_get(r, "ok"));
        json_object_put(r);
        return rc;
    }
    if (argc == 3 && !strcmp(argv[1], "firmware-bundle-inspect")) {
        json_object *r = bc_firmware_bundle_inspect(argv[2], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "firmware-bundle-export")) {
        json_object *m = bc_read_json(argv[2], &e);
        int rc = m ? bc_firmware_bundle_export(m, argv[3], &e) : -1;
        json_object_put(m);
        if (rc)
            fprintf(stderr, "%s\n", e.text);
        return rc ? 1 : 0;
    }
    if (argc == 5 && !strcmp(argv[1], "firmware-bundle-import")) {
        json_object *r = bc_firmware_bundle_import(argv[2], argv[3], argv[4], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "frame-decode")) {
        json_object *s = bc_read_json(argv[2], &e);
        uint8_t bytes[4096];
        size_t n = 0;
        json_object *r = s && bc_comm_hex(argv[3], bytes, sizeof bytes, &n, &e) == 0
                             ? bc_frame_decode(s, bytes, n, "", &e)
                             : NULL;
        json_object_put(s);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        int rc = !json_object_get_boolean(bc_get(r, "matched"));
        json_object_put(r);
        return rc;
    }
    if (argc == 3 && !strcmp(argv[1], "compatibility-list")) {
        json_object *r = bc_compatibility_list(argv[2], &e);
        if (!r) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 2 &&
        (!strcmp(argv[1], "comm-register-maps") || !strcmp(argv[1], "openocd-targets"))) {
        json_object *r =
            !strcmp(argv[1], "comm-register-maps") ? bc_comm_register_maps() : bc_openocd_targets();
        puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY));
        json_object_put(r);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "report-record")) {
        json_object *record = bc_read_json(argv[2], &e);
        int rc = record ? bc_report_pdf(record, argv[3], &e) : -1;
        if (record)
            json_object_put(record);
        if (rc)
            fprintf(stderr, "%s\n", e.text);
        return rc ? 1 : 0;
    }
    if (argc == 4 && !strcmp(argv[1], "rigol")) {
        json_object *result = bc_rigol_read(argv[2], argv[3], NULL, NULL, &e);
        if (!result) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(result, JSON_C_TO_STRING_PRETTY));
        json_object_put(result);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "pack")) {
        int rc = bc_pack(argv[2], argv[3], NULL, &e);
        if (rc)
            fprintf(stderr, "%s\n", e.text);
        return rc ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "inspect")) {
        json_object *m = bc_inspect(argv[2], &e);
        if (!m) {
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        puts(json_object_to_json_string_ext(m, JSON_C_TO_STRING_PRETTY));
        json_object_put(m);
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr,
                "Uso: bancada-core pack PASTA ARQUIVO | inspect ARQUIVO | boot RAIZ SEMENTE | "
                "install RAIZ PACOTE | run RAIZ PCB TESTE [CONFIG_JSON] | report-record "
                "RESULTADO_JSON SAIDA_PDF | comm-catalog | comm-list | comm-probe CONFIG_JSON | "
                "comm-exchange CONFIG_JSON OPERACAO_JSON | comm-tools RAIZ CONFIG_JSON PEDIDO_JSON "
                "| comm-register-maps | comm-profiles RAIZ | openocd-targets | openocd-plan "
                "PEDIDO_JSON | openocd-run PEDIDO_JSON | lab-catalog | firmware-plan PEDIDO_JSON | "
                "firmware-run PEDIDO_JSON | replay CAPTURA_JSON OPERACAO_JSON [FALHA_JSON] | "
                "replay-import CAPTURA_JSON | qualify-plan CONFIG_JSON PROCEDIMENTO | "
                "qualify-assess PLANO_JSON RESPOSTAS_JSON\n");
        return 1;
    }
    const char *op = argv[1], *root = argv[2], *id = "", *path = argv[3];
    if (argc == 5 && !strcmp(op, "comm-tools")) {
        json_object *config = bc_read_json(argv[3], &e),
                    *options = config ? bc_read_json(argv[4], &e) : NULL;
        if (!options) {
            json_object_put(config);
            fprintf(stderr, "%s\n", e.text);
            return 1;
        }
        op = !strcmp(bc_str(options, "tool"), "terminal")   ? "communication_terminal"
             : !strcmp(bc_str(options, "tool"), "scan")     ? "communication_scan"
             : !strcmp(bc_str(options, "tool"), "identify") ? "communication_identify"
                                                            : "";
        json_object *v = json_object_new_object();
        json_object_object_add(v, "communication", config);
        json_object_object_add(v, "operation", options);
        bc_job *tool = bc_job_start(op, root, "", "", "", v);
        json_object_put(v);
        if (!tool)
            return 1;
        int result = 1;
        for (;;) {
            if (stop)
                bc_job_cancel(tool);
            json_object *ev;
            while ((ev = bc_job_next(tool))) {
                puts(json_object_to_json_string_ext(ev, JSON_C_TO_STRING_PLAIN));
                fflush(stdout);
                if (!strcmp(bc_str(ev, "type"), "finished"))
                    result = strcmp(bc_str(ev, "status"), "ok") ? 1 : 0;
                json_object_put(ev);
            }
            if (bc_job_finished(tool)) {
                while ((ev = bc_job_next(tool))) {
                    puts(json_object_to_json_string_ext(ev, JSON_C_TO_STRING_PLAIN));
                    if (!strcmp(bc_str(ev, "type"), "finished"))
                        result = strcmp(bc_str(ev, "status"), "ok") ? 1 : 0;
                    json_object_put(ev);
                }
                break;
            }
            struct timespec t = {0, 10000000};
            nanosleep(&t, NULL);
        }
        bc_job_free(tool);
        return result;
    }
    if (!strcmp(op, "run")) {
        if (argc != 5 && argc != 6)
            return 1;
        id = argv[3];
        path = argv[4];
    }
    json_object *v = json_object_new_object();
    if (!strcmp(op, "run") && argc == 6) {
        json_object *c = bc_read_json(argv[5], &e);
        if (!c) {
            fprintf(stderr, "%s\n", e.text);
            json_object_put(v);
            return 1;
        }
        json_object_object_add(v, "communication", c);
    }
    bc_job *j = bc_job_start(op, root, id, path, "", v);
    json_object_put(v);
    if (!j)
        return 1;
    int result = 1;
    for (;;) {
        if (stop)
            bc_job_cancel(j);
        json_object *o;
        while ((o = bc_job_next(j))) {
            puts(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN));
            fflush(stdout);
            if (!strcmp(bc_str(o, "type"), "finished"))
                result = strcmp(bc_str(o, "status"), "ok") ? 1 : 0;
            json_object_put(o);
        }
        if (bc_job_finished(j)) {
            while ((o = bc_job_next(j))) {
                puts(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN));
                if (!strcmp(bc_str(o, "type"), "finished"))
                    result = strcmp(bc_str(o, "status"), "ok") ? 1 : 0;
                json_object_put(o);
            }
            break;
        }
        struct timespec ts = {0, 10000000};
        nanosleep(&ts, NULL);
    }
    bc_job_free(j);
    return result;
}
