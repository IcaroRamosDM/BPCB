#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include "plot.h"
#include <cairo.h>
#include <cairo-pdf.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    cairo_t *cr;
    double y;
    unsigned page;
    int fd;
    const char *heading;
} report;

static cairo_status_t output(void *context, const unsigned char *data, unsigned length) {
    report *r = context;
    size_t used = 0;
    while (used < length) {
        ssize_t n = write(r->fd, data + used, length - used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return CAIRO_STATUS_WRITE_ERROR;
        used += (size_t)n;
    }
    return CAIRO_STATUS_SUCCESS;
}

static void font(report *r, double size, int bold) {
    cairo_select_font_face(r->cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(r->cr, size);
}

static void page(report *r) {
    if (r->page)
        cairo_show_page(r->cr);
    r->page++;
    r->y = 102;
    cairo_set_source_rgb(r->cr, 0.08, 0.32, 0.27);
    cairo_rectangle(r->cr, 0, 0, 595, 70);
    cairo_fill(r->cr);
    cairo_set_source_rgb(r->cr, 1, 1, 1);
    font(r, 21, 1);
    cairo_move_to(r->cr, 40, 43);
    cairo_show_text(r->cr, r->heading);
    char footer[100];
    snprintf(footer, sizeof footer, "BPCB %s  |  Página %u", BC_VERSION, r->page);
    cairo_set_source_rgb(r->cr, 0.38, 0.4, 0.44);
    font(r, 9, 0);
    cairo_move_to(r->cr, 40, 812);
    cairo_show_text(r->cr, footer);
}

static void line(report *r, const char *text, int title) {
    if (!text)
        text = "";
    char *valid = g_utf8_make_valid(text, -1), *cursor = valid;
    if (title)
        r->y += 10;
    do {
        if (r->y > 770)
            page(r);
        font(r, title ? 12 : 10, title);
        cairo_set_source_rgb(r->cr, title ? 0.08 : 0.14, title ? 0.32 : 0.16, title ? 0.27 : 0.19);
        char *end = cursor, *last_space = NULL;
        while (*end && *end != '\n') {
            char *next = g_utf8_next_char(end), saved = *next;
            *next = 0;
            cairo_text_extents_t ext;
            cairo_text_extents(r->cr, cursor, &ext);
            *next = saved;
            if (ext.x_advance > 510 && end > cursor)
                break;
            if (*end == ' ')
                last_space = end;
            end = next;
        }
        if (*end && *end != '\n' && last_space && last_space > cursor)
            end = last_space;
        char saved = *end;
        *end = 0;
        cairo_move_to(r->cr, 40, r->y);
        cairo_show_text(r->cr, cursor);
        *end = saved;
        r->y += 15;
        cursor = end;
        if (*cursor == '\n' || *cursor == ' ')
            cursor++;
    } while (*cursor);
    g_free(valid);
}

static const char *status_name(const char *s) {
    return !strcmp(s, "ok")          ? "Concluído"
           : !strcmp(s, "error")     ? "Falhou"
           : !strcmp(s, "cancelled") ? "Interrompido"
                                     : "Incompleto";
}

static void pair(report *r, const char *label, const char *value) {
    char *s = g_strdup_printf("%s: %s", label, value && *value ? value : "Não informado");
    line(r, s, 0);
    g_free(s);
}

static int record_valid(json_object *record, int plots, bc_error *e) {
    if (!record || strcmp(bc_str(record, "format"), "bpcb.result") ||
        !json_object_is_type(bc_get(record, "events"), json_type_array))
        return bc_fail(e, "Não há resultado válido para exportar.");
    return plots && bc_get(record, "plot") ? bc_plot_validate(bc_get(record, "plot"), e) : 0;
}

static void connection_report(report *r, json_object *connection, double actual_hz) {
    pair(r, "Adaptador", bc_str(connection, "adapter"));
    pair(r, "Barramento", bc_str(connection, "bus"));
    pair(r, "Caminho/porta", bc_str(connection, "device"));
    pair(r, "Série", bc_str(connection, "serial"));
    char setting[256];
    if (!strcmp(bc_str(connection, "bus"), "uart"))
        snprintf(setting, sizeof setting,
                 "%.0f baud; %.0f bits; paridade %s; %.0f stop; fluxo %s; DTR %s; RTS %s",
                 bc_num(connection, "baud"), bc_num(connection, "data_bits"),
                 bc_str(connection, "parity"), bc_num(connection, "stop_bits"),
                 bc_str(connection, "flow_control"),
                 json_object_get_boolean(bc_get(connection, "dtr")) ? "1" : "0",
                 json_object_get_boolean(bc_get(connection, "rts")) ? "1" : "0");
    else if (!strcmp(bc_str(connection, "bus"), "spi"))
        snprintf(setting, sizeof setting,
                 "Clock solicitado %.0f Hz; timeout %.0f ms; modo SPI %.0f; CS %s%.0f",
                 bc_num(connection, "frequency_hz"), bc_num(connection, "timeout_ms"),
                 bc_num(connection, "spi_mode"),
                 !strcmp(bc_str(connection, "adapter"), "ch347") ? "" : "AD",
                 bc_num(connection, "chip_select"));
    else
        snprintf(setting, sizeof setting, "Clock solicitado %.0f Hz; timeout %.0f ms",
                 bc_num(connection, "frequency_hz"), bc_num(connection, "timeout_ms"));
    if (!strcmp(bc_str(connection, "adapter"), "ch347") && bc_num(connection, "ch347_mode") == 2 &&
        !strcmp(bc_str(connection, "bus"), "uart"))
        snprintf(
            setting, sizeof setting,
            "%.0f baud; 8 bits; paridade %s; %.0f stop; linhas de modem não configuradas via HID",
            bc_num(connection, "baud"), bc_str(connection, "parity"),
            bc_num(connection, "stop_bits"));
    pair(r, "Configuração", setting);
    if (!strcmp(bc_str(connection, "adapter"), "ch347")) {
        snprintf(setting, sizeof setting, "M%.0f%s", bc_num(connection, "ch347_mode"),
                 bc_num(connection, "ch347_mode") == 2 ? " (HID)" : "");
        pair(r, "Modo CH347T", setting);
    }
    if (actual_hz > 0) {
        snprintf(setting, sizeof setting, "%.0f Hz (configurado; não medido)", actual_hz);
        pair(r, "Clock efetivo", setting);
    }
}

static void acquisition_report(report *r, json_object *event) {
    json_object *a = bc_get(event, "acquisition"), *op = bc_get(a, "operation");
    if (!a)
        return;
    if (*bc_str(a, "channel"))
        pair(r, "Canal", bc_str(a, "channel"));
    if (*bc_str(op, "kind")) {
        if (*bc_str(op, "device"))
            pair(r, "Componente", bc_str(op, "device"));
        if (*bc_str(op, "metric"))
            pair(r, "Grandeza", bc_str(op, "metric"));
        if (*bc_str(op, "protocol"))
            pair(r, "Protocolo", bc_str(op, "protocol"));
        pair(r, "Resposta válida", json_object_get_boolean(bc_get(a, "matched")) ? "Sim" : "Não");
        if (*bc_str(a, "message"))
            pair(r, "Detalhe", bc_str(a, "message"));
        const char *fields[] = {"transmitted", "received"},
                   *labels[] = {"Bytes enviados", "Bytes recebidos"};
        for (unsigned i = 0; i < 2; i++)
            if (*bc_str(a, fields[i])) {
                const char *v = bc_str(a, fields[i]);
                char bounded[440];
                snprintf(bounded, sizeof bounded, "%.384s%s", v,
                         strlen(v) > 384 ? " … (ver JSON para os demais bytes)" : "");
                pair(r, labels[i], bounded);
            }
        if (json_object_get_boolean(bc_get(a, "requires_visual_confirmation")))
            line(r, "O ACK do display exige conferência visual do operador.", 0);
        if (!strcmp(bc_str(a, "simulation_source"), "author-declared"))
            line(r, "Valor de exemplo declarado pelo autor; sem leitura física.", 0);
        if (bc_get(a, "hardware_accessed") &&
            !json_object_get_boolean(bc_get(a, "hardware_accessed")))
            line(r, "Reprodução offline de captura; sem acesso a hardware.", 0);
    }
}

static void write_record(report *r, json_object *record, int include_plots) {
    json_object *who = bc_get(record, "identity"), *snap = bc_get(record, "snapshot");
    char local[128];
    bc_local_time(bc_str(record, "started_at"), local, sizeof local);
    line(r, bc_str(bc_get(snap, "test_definition"), "title"), 1);
    pair(r, "Data local", local);
    pair(r, "Início UTC", bc_str(record, "started_at"));
    pair(r, "Executor do ensaio", bc_str(record, "bpcb_version"));
    pair(r, "Bancada", bc_str(record, "board_id"));
    pair(r, "Revisão", bc_str(snap, "revision"));
    pair(r, "Pacote", bc_str(snap, "version"));
    pair(r, "Placa física", bc_str(who, "physical_board"));
    pair(r, "Dispositivo", bc_str(who, "device"));
    pair(r, "Operador", bc_str(who, "operator"));
    pair(r, "Execução", status_name(bc_str(record, "status")));
    line(r, bc_str(record, "message"), 0);
    line(r, "Medições e critérios", 1);
    json_object *events = bc_get(record, "events");
    unsigned measurements = 0;
    for (size_t i = 0; i < json_object_array_length(events); i++) {
        json_object *event = json_object_array_get_idx(events, i);
        if (strcmp(bc_str(event, "type"), "measurement"))
            continue;
        measurements++;
        const char *verdict = bc_str(event, "verdict");
        char *text = g_strdup_printf("%s: %.9g %s | %s", bc_str(event, "label"),
                                     bc_num(event, "value"), bc_str(event, "unit"),
                                     !strcmp(verdict, "pass")   ? "Dentro da faixa"
                                     : !strcmp(verdict, "fail") ? "Fora da faixa"
                                                                : "Sem comparação");
        line(r, text, 0);
        g_free(text);
        pair(r, "Origem", bc_str(event, "source"));
        if (*bc_str(event, "criterion"))
            pair(r, "Critério", bc_str(event, "criterion"));
        if (bc_get(event, "received_bytes")) {
            if (bc_get(event, "transmitted_bytes"))
                pair(r, "Bytes transmitidos", bc_str(event, "transmitted_bytes"));
            if (bc_get(event, "expected_bytes"))
                pair(r, "Bytes esperados", bc_str(event, "expected_bytes"));
            pair(r, "Bytes recebidos", bc_str(event, "received_bytes"));
            if (bc_get(event, "mask_bytes"))
                pair(r, "Máscara", bc_str(event, "mask_bytes"));
        }
        acquisition_report(r, event);
        r->y += 5;
    }
    if (!measurements)
        line(r, "Nenhuma medição concluída foi registrada.", 0);
    json_object *connection = bc_get(snap, "communication");
    if (connection) {
        line(r, "Comunicação utilizada", 1);
        if (bc_num(connection, "api") == 2) {
            json_object *channels = bc_get(connection, "channels"),
                        *clocks = bc_get(snap, "communication_actual_hz");
            if (json_object_is_type(channels, json_type_object)) {
                json_object_object_foreach(channels, id, config) {
                    pair(r, "Canal", id);
                    connection_report(r, config, bc_num(clocks, id));
                }
            }
        } else
            connection_report(r, connection, bc_num(snap, "communication_actual_hz"));
    }
    line(r, "Condições do ensaio", 1);
    json_object *params = bc_get(snap, "parameters"),
                *definitions = bc_get(bc_get(snap, "test_definition"), "parameters");
    if (json_object_is_type(params, json_type_object)) {
        json_object_object_foreach(params, key, value) {
            const char *label = key, *unit = "";
            for (size_t i = 0; json_object_is_type(definitions, json_type_array) &&
                               i < json_object_array_length(definitions);
                 i++) {
                json_object *d = json_object_array_get_idx(definitions, i);
                if (!strcmp(key, bc_str(d, "id"))) {
                    label = bc_str(d, "label");
                    unit = bc_str(d, "unit");
                    break;
                }
            }
            char numeric[64];
            const char *display = json_object_get_string(value);
            if (json_object_is_type(value, json_type_double) ||
                json_object_is_type(value, json_type_int)) {
                snprintf(numeric, sizeof numeric, "%.12g", json_object_get_double(value));
                display = numeric;
            }
            char *text = g_strdup_printf("%s: %s %s", label, display, unit);
            line(r, text, 0);
            g_free(text);
        }
    }
    json_object *cal = bc_get(snap, "calibration");
    pair(r, "Calibração",
         *bc_str(cal, "physical_id") ? bc_str(cal, "physical_id") : "Sem calibração");
    if (cal)
        pair(r, "Data da calibração", bc_str(cal, "updated"));
    line(r, "Componentes utilizados", 1);
    json_object *components = bc_get(snap, "components");
    if (json_object_is_type(components, json_type_object)) {
        json_object_object_foreach(components, key, value) {
            pair(r, key, json_object_get_string(value));
        }
    }
    line(r, "Conferências e observações", 1);
    const char *reply_unit = "";
    for (size_t i = 0; i < json_object_array_length(events); i++) {
        json_object *event = json_object_array_get_idx(events, i);
        if (!strcmp(bc_str(event, "type"), "confirm")) {
            line(r, bc_str(event, "message"), 0);
            reply_unit = bc_str(event, "unit");
        } else if (!strcmp(bc_str(event, "type"), "operator_reply")) {
            char text[180];
            const char *answer =
                json_object_get_boolean(bc_get(event, "accepted")) ? "Confirmado" : "Recusado";
            if (bc_get(event, "value"))
                snprintf(text, sizeof text, "%s | Leitura: %.9g %s", answer, bc_num(event, "value"),
                         reply_unit);
            else
                snprintf(text, sizeof text, "%s", answer);
            line(r, text, 0);
        }
    }
    if (json_object_get_boolean(bc_get(record, "events_truncated")))
        line(r, "Registro parcial: limite de eventos atingido.", 0);
    line(r,
         "Conclusão de execução e comparação de medidas não equivalem à aprovação global da PCB. "
         "Resultados de simulação não validam a montagem física.",
         0);
    json_object *plot = bc_get(record, "plot");
    if (include_plots && plot) {
        json_object *series = bc_get(plot, "series");
        for (size_t i = 0; i < json_object_array_length(series); i++) {
            if (r->y + 295 > 780)
                page(r);
            bc_plot_draw(r->cr, json_object_array_get_idx(series, i), NULL, 40, r->y, 515, 280);
            r->y += 295;
        }
        line(r, "Gráficos: amostras retidas em memória. Não substituem a captura do osciloscópio.",
             0);
    }
}

int bc_report_pdf(json_object *record, const char *destination, bc_error *e) {
    int session = !strcmp(bc_str(record, "format"), "bpcb.session");
    int plots = json_object_get_boolean(bc_get(record, "include_plots"));
    json_object *steps = bc_get(record, "steps");
    if (session) {
        if (!json_object_is_type(steps, json_type_array) || !json_object_array_length(steps) ||
            json_object_array_length(steps) > 16)
            return bc_fail(e, "Sessão sem etapas válidas.");
        for (size_t i = 0; i < json_object_array_length(steps); i++) {
            json_object *item = bc_get(json_object_array_get_idx(steps, i), "record");
            if (item && record_valid(item, plots, e))
                return -1;
        }
    } else if (record_valid(record, plots, e))
        return -1;
    int fd = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return bc_fail(e, "Escolha um arquivo PDF novo: %s.", strerror(errno));
    report r = {.fd = fd,
                .heading = session ? "BPCB | Relatório da sessão" : "BPCB | Relatório de ensaio"};
    cairo_surface_t *surface = cairo_pdf_surface_create_for_stream(output, &r, 595, 842);
    r.cr = cairo_create(surface);
    page(&r);
    if (session) {
        char local[128];
        bc_local_time(bc_str(record, "started_at"), local, sizeof local);
        line(&r, "Resumo da sessão guiada", 1);
        pair(&r, "Bancada", bc_str(record, "board_id"));
        pair(&r, "Início local", local);
        pair(&r, "Estado", bc_str(record, "state"));
        for (size_t i = 0; i < json_object_array_length(steps); i++) {
            json_object *step = json_object_array_get_idx(steps, i);
            char *row = g_strdup_printf("%u. %s - %s", (unsigned)(i + 1), bc_str(step, "title"),
                                        bc_str(step, "summary"));
            line(&r, row, 0);
            g_free(row);
            if (!bc_get(step, "record"))
                line(&r, "Sem resultado: etapa pendente ou não executada.", 0);
        }
        line(&r,
             "Relatório solicitado pelo operador. Etapas pendentes não equivalem a aprovadas. A "
             "sessão não reinicia automaticamente ensaios interrompidos.",
             0);
        for (size_t i = 0; i < json_object_array_length(steps); i++) {
            json_object *step = json_object_array_get_idx(steps, i), *item = bc_get(step, "record");
            if (!item)
                continue;
            page(&r);
            char label[220];
            snprintf(label, sizeof label, "Etapa %u - %.180s", (unsigned)(i + 1),
                     bc_str(step, "title"));
            line(&r, label, 1);
            write_record(&r, item, plots);
        }
    } else
        write_record(&r, record, plots);
    cairo_status_t status = cairo_status(r.cr);
    cairo_destroy(r.cr);
    cairo_surface_finish(surface);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        status = cairo_surface_status(surface);
    cairo_surface_destroy(surface);
    int synced = fsync(fd), closed = close(fd);
    if (status != CAIRO_STATUS_SUCCESS || synced || closed) {
        unlink(destination);
        return bc_fail(e, "Não foi possível concluir o PDF.");
    }
    return 0;
}

static json_object *measure_map(json_object *record) {
    json_object *result = json_object_new_object(), *events = bc_get(record, "events");
    for (size_t i = 0; i < json_object_array_length(events); i++) {
        json_object *event = json_object_array_get_idx(events, i);
        if (strcmp(bc_str(event, "type"), "measurement"))
            continue;
        char *key = g_strdup_printf("%s\t%s", bc_str(event, "label"), bc_str(event, "unit"));
        json_object_object_add(result, key, bc_clone(event));
        g_free(key);
    }
    return result;
}

json_object *bc_compare_records(json_object *left, json_object *right, bc_error *e) {
    if (!left || !right || strcmp(bc_str(left, "format"), "bpcb.result") ||
        strcmp(bc_str(right, "format"), "bpcb.result") ||
        strcmp(bc_str(left, "board_id"), bc_str(right, "board_id")) ||
        strcmp(bc_str(left, "test_id"), bc_str(right, "test_id"))) {
        bc_fail(e, "Escolha dois resultados da mesma bancada e do mesmo teste.");
        return NULL;
    }
    json_object *result = json_object_new_object(), *notes = json_object_new_array(),
                *rows = json_object_new_array();
    json_object_object_add(result, "warnings", notes);
    json_object_object_add(result, "rows", rows);
    json_object *a = bc_get(left, "snapshot"), *b = bc_get(right, "snapshot");
    const char *fields[] = {"parameters", "calibration", "components", "version"};
    const char *labels[] = {"Parâmetros diferentes.", "Calibrações diferentes.",
                            "Componentes diferentes.", "Versões de pacote diferentes."};
    for (size_t i = 0; i < 4; i++)
        if (!json_object_equal(bc_get(a, fields[i]), bc_get(b, fields[i])))
            json_object_array_add(notes, json_object_new_string(labels[i]));
    const char *device_a = bc_str(bc_get(left, "identity"), "device"),
               *device_b = bc_str(bc_get(right, "identity"), "device");
    if (!*device_a || !*device_b || strcmp(device_a, device_b))
        json_object_array_add(
            notes, json_object_new_string("Identificação do dispositivo ausente ou diferente; "
                                          "confira se são ensaios do mesmo dispositivo."));
    const char *pcb_a = bc_str(bc_get(left, "identity"), "physical_board"),
               *pcb_b = bc_str(bc_get(right, "identity"), "physical_board");
    if (!*pcb_a || !*pcb_b || strcmp(pcb_a, pcb_b))
        json_object_array_add(
            notes, json_object_new_string("Identificação da placa física ausente ou diferente."));
    if (strcmp(bc_str(left, "status"), "ok") || strcmp(bc_str(right, "status"), "ok"))
        json_object_array_add(
            notes,
            json_object_new_string("Uma execução falhou, foi interrompida ou está incompleta."));
    json_object *ma = measure_map(left), *mb = measure_map(right);
    json_object_object_foreach(ma, key, x) {
        json_object *y = bc_get(mb, key);
        if (!y)
            continue;
        json_object *row = json_object_new_object();
        json_object_object_add(row, "label", json_object_get(bc_get(x, "label")));
        json_object_object_add(row, "unit", json_object_get(bc_get(x, "unit")));
        double first = bc_num(x, "value"), last = bc_num(y, "value");
        json_object_object_add(row, "before", json_object_new_double(first));
        json_object_object_add(row, "after", json_object_new_double(last));
        json_object_object_add(row, "difference", json_object_new_double(last - first));
        if (first != 0)
            json_object_object_add(row, "percent",
                                   json_object_new_double(100 * (last - first) / fabs(first)));
        json_object_array_add(rows, row);
    }
    json_object_put(ma);
    json_object_put(mb);
    if (!json_object_array_length(rows))
        json_object_array_add(
            notes, json_object_new_string(
                       "Não há medições com o mesmo nome e unidade nos dois registros."));
    return result;
}
