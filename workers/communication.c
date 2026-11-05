#define _POSIX_C_SOURCE 200809L
#include "communication.h"
#include "lab.h"
#include "worker_api.h"
#include <fcntl.h>
#ifndef _WIN32
#include <sys/file.h>
#endif
#include <string.h>

static int cancelled(void *context) {
    (void)context;
    return worker_stopped;
}

int main(void) {
    json_object *q = worker_begin();
    if (!q)
        return 1;
    bc_error e = {{0}};
    bc_lab *session = NULL;
    int rc = 1, lock = -1;
    json_object *d = bc_get(q, "communication_definition"), *operations = bc_get(d, "operations"),
                *config = bc_get(q, "communication");
    if (strcmp(bc_str(q, "mode"), "hardware") || !d || bc_comm_declaration(d, &e) < 0 ||
        !operations || !config) {
        worker_event(
            "error",
            *e.text ? e.text
                    : "Rotina exige declaração e operações de comunicação API 1 em modo hardware.");
        goto done;
    }
    if (bc_num(config, "api") != 2 &&
        !json_object_get_boolean(bc_get(config, "connections_checked"))) {
        worker_event("error", "Conexões não conferidas.");
        goto done;
    }
    json_object *profile = json_object_new_object();
    json_object_object_add(profile, "communication", json_object_get(d));
    json_object *resolved = bc_comm_resolve(profile, NULL, config, &e);
    json_object_put(profile);
    if (!resolved) {
        worker_event("error", e.text);
        goto done;
    }
    json_object_object_add(q, "communication", resolved);
    config = resolved;
    lock = open(bc_bench_lock_path(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
        worker_event("error", "Outra operação está usando a bancada.");
        goto done;
    }
    session = bc_lab_new(config, d, cancelled, NULL, &e);
    if (!session) {
        worker_event("error", e.text);
        goto done;
    }
    for (size_t i = 0; i < json_object_array_length(operations); i++) {
        json_object *operation = json_object_array_get_idx(operations, i);
        json_object *result = bc_lab_execute(session, operation, &e);
        if (!result) {
            worker_event("error", e.text);
            goto done;
        }
        json_object *connected = json_object_new_object();
        json_object_object_add(connected, "type", json_object_new_string("status"));
        json_object_object_add(
            connected, "message",
            json_object_new_string("Canal selecionado; identidade efetiva registrada."));
        json_object_object_add(connected, "transport", bc_clone(bc_get(result, "transport")));
        puts(json_object_to_json_string_ext(connected, JSON_C_TO_STRING_PLAIN));
        fflush(stdout);
        json_object_put(connected);
        json_object *raw = bc_get(operation, "operation");
        if (raw)
            operation = raw;
        if (bc_get(operation, "kind")) {
            json_object *ev = json_object_new_object();
            json_object_object_add(ev, "type", json_object_new_string("measurement"));
            json_object_object_add(ev, "label",
                                   json_object_new_string(*bc_str(operation, "label")
                                                              ? bc_str(operation, "label")
                                                              : "Componente/protocolo"));
            json_object_object_add(ev, "value", bc_clone(bc_get(result, "value")));
            json_object_object_add(ev, "unit", json_object_new_string(bc_str(result, "unit")));
            json_object_object_add(ev, "source", json_object_new_string("Comunicação da PCB"));
            json_object_object_add(ev, "acquisition", bc_clone(result));
            json_object_object_add(
                ev, "communication_valid",
                json_object_new_boolean(json_object_get_boolean(bc_get(result, "matched"))));
            puts(json_object_to_json_string_ext(ev, JSON_C_TO_STRING_PLAIN));
            fflush(stdout);
            json_object_put(ev);
            int matched = json_object_get_boolean(bc_get(result, "matched"));
            json_object_put(result);
            if (!matched) {
                worker_event("error", "Componente/protocolo não atendeu ao critério.");
                goto done;
            }
            continue;
        }
        char message[15000];
        snprintf(message, sizeof message, "%s · recebido: %s",
                 *bc_str(operation, "label") ? bc_str(operation, "label") : "Transferência",
                 bc_str(result, "received"));
        worker_event("status", message);
        if (bc_get(result, "matched")) {
            int matched = json_object_get_boolean(bc_get(result, "matched"));
            json_object *measurement = json_object_new_object();
            json_object_object_add(measurement, "type", json_object_new_string("measurement"));
            json_object_object_add(measurement, "label",
                                   json_object_new_string(*bc_str(operation, "label")
                                                              ? bc_str(operation, "label")
                                                              : "Resposta esperada"));
            json_object_object_add(measurement, "value", json_object_new_int(matched));
            json_object_object_add(measurement, "unit", json_object_new_string("booleano"));
            json_object_object_add(measurement, "source",
                                   json_object_new_string("Comparação dos bytes recebidos"));
            json_object_object_add(measurement, "verdict",
                                   json_object_new_string(matched ? "pass" : "fail"));
            json_object_object_add(measurement, "expected", json_object_new_int(1));
            json_object_object_add(measurement, "tolerance_abs", json_object_new_int(0));
            json_object_object_add(measurement, "expected_bytes",
                                   json_object_get(bc_get(operation, "expect")));
            json_object_object_add(measurement, "received_bytes",
                                   json_object_get(bc_get(result, "received")));
            if (bc_get(operation, "mask"))
                json_object_object_add(measurement, "mask_bytes",
                                       json_object_get(bc_get(operation, "mask")));
            char criterion[25000];
            snprintf(criterion, sizeof criterion, "Esperado: %s; máscara: %s; recebido: %s",
                     bc_str(operation, "expect"),
                     bc_get(operation, "mask") ? bc_str(operation, "mask") : "FF em todos os bytes",
                     bc_str(result, "received"));
            json_object_object_add(measurement, "criterion", json_object_new_string(criterion));
            puts(json_object_to_json_string_ext(measurement, JSON_C_TO_STRING_PLAIN));
            fflush(stdout);
            json_object_put(measurement);
            if (!matched) {
                json_object_put(result);
                worker_event("error",
                             "Resposta difere do critério declarado; sequência interrompida.");
                goto done;
            }
        } else
            worker_measure(*bc_str(operation, "label") ? bc_str(operation, "label")
                                                       : "Bytes recebidos",
                           bc_num(result, "bytes_read"), "bytes",
                           "Adaptador selecionado; não há critério de aprovação declarado");
        json_object_put(result);
    }
    worker_event("done", "Sequência de comunicação concluída. O resultado se limita aos comandos e "
                         "critérios declarados.");
    rc = 0;
done:
    bc_lab_free(session);
    if (lock >= 0)
        close(lock);
    json_object_put(q);
    return rc;
}
