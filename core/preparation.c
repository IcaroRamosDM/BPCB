#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include "communication.h"
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>

static void check(json_object *list, const char *name, const char *status, const char *detail) {
    json_object *item = json_object_new_object();
    json_object_object_add(item, "name", json_object_new_string(name));
    json_object_object_add(item, "status", json_object_new_string(status));
    json_object_object_add(item, "detail", json_object_new_string(detail));
    json_object_array_add(list, item);
}

json_object *bc_prepare(const char *root, const char *board, json_object *values,
                        int (*cancel)(void *), void *context, bc_error *e) {
    json_object *result = json_object_new_object(), *checks = json_object_new_array();
    json_object_object_add(result, "checks", checks);
    check(checks, "Escopo", "informação",
          "Conferência de software e acesso aos instrumentos. Não aciona saídas, não mede descarga "
          "e não aprova a montagem.");
    if (*board) {
        char folder[4096];
        bc_error local = {{0}};
        json_object *stored = bc_profile(root, board, folder, &local);
        json_object *profile = stored ? bc_apply(stored, values, &local) : NULL;
        if (stored)
            json_object_put(stored);
        if (!profile)
            check(checks, "Bancada", "problema", local.text);
        else {
            char text[500];
            snprintf(text, sizeof text, "%s · revisão %s · pacote %s · BPCB %s",
                     bc_str(profile, "pcb"), bc_str(profile, "revision"),
                     bc_str(profile, "version"), BC_VERSION);
            check(checks, "Bancada selecionada", "conferido", text);
            int integrity = bc_verify(profile, folder, &local);
            check(checks, "Integridade do pacote", integrity ? "problema" : "conferido",
                  integrity ? local.text : "Arquivos conferidos com o manifesto instalado.");
            const char *cal_id = bc_str(values, "calibration_id");
            if (!*cal_id)
                check(checks, "Calibração", "atenção",
                      "Nenhum perfil selecionado. Se necessário, selecione a placa física no "
                      "diálogo Calibração.");
            else {
                local.text[0] = 0;
                json_object *cal = bc_cal_load(root, board, cal_id, &local);
                json_object *test = bc_test(profile, bc_str(values, "test_id")),
                            *params = bc_get(test, "parameters");
                double vref = 0;
                for (size_t i = 0; i < json_object_array_length(params); i++) {
                    json_object *p = json_object_array_get_idx(params, i);
                    if (!strcmp(bc_str(p, "id"), "vref_mv"))
                        vref = bc_num(p, "default");
                }
                int invalid = !cal || !test ||
                              strcmp(bc_str(cal, "revision"), bc_str(profile, "revision")) ||
                              bc_cal_check(cal, bc_get(profile, "components"), vref, &local);
                check(checks, "Calibração", invalid ? "problema" : "conferido",
                      invalid ? (local.text[0] ? local.text
                                               : "Selecione um teste com referência ADC para "
                                                 "conferir a calibração.")
                              : cal_id);
                if (cal)
                    json_object_put(cal);
            }
            json_object_put(profile);
        }
    } else
        check(checks, "Bancada", "atenção",
              "Abra uma bancada para conferir versão, integridade e calibração.");
    if (cancel && cancel(context)) {
        json_object_put(result);
        bc_fail(e, "Conferência cancelada.");
        return NULL;
    }
    bc_error communication_error = {{0}};
    json_object *selected = bc_get(values, "communication"),
                *connection = bc_comm_probe(selected, cancel, context, &communication_error);
    check(checks, "Comunicação da PCB", connection ? "conferido" : "problema",
          connection ? bc_str(connection, "scope") : communication_error.text);
    if (connection) {
        json_object_object_add(result, "communication", connection);
    }
    if (*board) {
        char folder[4096];
        bc_error local = {{0}};
        json_object *profile = bc_profile(root, board, folder, &local);
        json_object *test = profile ? bc_test(profile, bc_str(values, "test_id")) : NULL;
        if (test && !strcmp(bc_str(test, "mode"), "hardware")) {
            json_object *resolved = bc_comm_resolve(profile, test, selected, &local);
            check(checks, "Adaptador e procedimento", resolved ? "conferido" : "problema",
                  resolved ? "Barramento e capacidades declaradas são compatíveis. A conferência "
                             "física das conexões continua necessária."
                           : local.text);
            if (resolved)
                json_object_put(resolved);
        }
        if (profile)
            json_object_put(profile);
    }
    const char *endpoint = bc_str(values, "rigol_endpoint");
    if (!*endpoint || !strcmp(endpoint, "tcp://"))
        check(checks, "Rigol", "não configurado",
              "Abra Rigol e informe IP/porta ou o dispositivo USBTMC.");
    else {
        bc_error local = {{0}};
        json_object *scope = bc_rigol_read(endpoint, "identify", cancel, context, &local);
        check(checks, "Rigol", scope ? "conferido" : "problema",
              scope ? bc_str(scope, "instrument") : local.text);
        if (scope)
            json_object_put(scope);
    }
    return result;
}
