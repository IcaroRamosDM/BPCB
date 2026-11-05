#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Encerra o processo DEPOIS de persistir um cadastro: reproduz uma queda
 * sem executar rollback, em vez de apenas construir um diário artificial. */
static int crash_after_state;
int __real_bc_write_json(const char *, json_object *, bc_error *);

int __wrap_bc_write_json(const char *path, json_object *value, bc_error *e) {
    int rc = __real_bc_write_json(path, value, e);
    if (!rc && crash_after_state && strstr(path, "/state/"))
        _exit(77);
    return rc;
}

static void install(const char *root, const char *build) {
    char file[4096];
    bc_error e = {{0}};
    snprintf(file, sizeof file, "%s/Demonstracao-V1.bancada", build);
    assert(!bc_install(root, file, "", &e));
}

static void component(const char *root, const char *value) {
    bc_error e = {{0}};
    json_object *v = json_object_new_object(), *c = json_object_new_object();
    json_object_object_add(c, "R1", json_object_new_string(value));
    json_object_object_add(v, "components", c);
    assert(!bc_save(root, "demonstracao-v1", v, &e));
    json_object_put(v);
}

static void expect(const char *root, const char *value) {
    char folder[4096];
    bc_error e = {{0}};
    json_object *p = bc_profile(root, "demonstracao-v1", folder, &e);
    assert(p && !strcmp(bc_str(bc_get(p, "components"), "R1"), value));
    json_object_put(p);
}

static void wait_crash(pid_t pid) {
    int status;
    assert(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
           WEXITSTATUS(status) == 77);
}

static json_object *pending(const char *root) {
    bc_error e = {{0}};
    json_object *list = bc_recovery_list(root, &e), *row = NULL;
    assert(list);
    for (size_t i = 0; i < json_object_array_length(list); i++) {
        json_object *item = json_object_array_get_idx(list, i);
        if (json_object_get_boolean(bc_get(item, "pending")))
            row = bc_clone(item);
    }
    json_object_put(list);
    assert(row);
    return row;
}

static void blocked_run(const char *root) {
    bc_job *job = bc_job_start("run", root, "demonstracao-v1", "corrente", "", NULL);
    assert(job);
    int blocked = 0;
    for (unsigned tries = 0; tries < 1000; tries++) {
        json_object *event;
        while ((event = bc_job_next(job))) {
            assert(strcmp(bc_str(event, "type"), "measurement"));
            if (!strcmp(bc_str(event, "type"), "finished"))
                blocked = !strcmp(bc_str(event, "status"), "error") &&
                          strstr(bc_str(event, "message"), "Recuperação assistida");
            json_object_put(event);
        }
        if (bc_job_finished(job) && blocked)
            break;
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    bc_job_free(job);
    assert(blocked);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    bc_error e = {{0}};
    char root[4096], target[4096], backup[4096], path[4096];
    snprintf(root, sizeof root, "%s/qa-continuity-XXXXXX", argv[1]);
    assert(mkdtemp(root));
    assert(!bc_path(target, sizeof target, root, "destino", &e));
    install(root, argv[1]);
    install(target, argv[1]);
    component(root, "2k");
    component(target, "4k");
    assert(!bc_path(backup, sizeof backup, root, "cadastros.bpcbbackup", &e));
    assert(!bc_backup_create(root, backup, 0, &e));
    json_object *review = bc_backup_inspect(target, backup, &e);
    assert(review);
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        crash_after_state = 1;
        bc_backup_restore(target, backup, bc_str(review, "token"), 1, &e);
        _exit(99);
    }
    wait_crash(pid);
    json_object_put(review);
    assert(bc_recovery_pending(target, &e) == 1);
    expect(target, "2k");
    blocked_run(target);
    json_object *row = pending(target);
    char id[64];
    snprintf(id, sizeof id, "%s", bc_str(row, "id"));
    json_object_put(row);
    review = bc_recovery_inspect(target, id, &e);
    assert(review && !bc_num(review, "conflicts"));
    assert(bc_recovery_apply(target, id, "prévia antiga", 0, &e) < 0);
    expect(target, "2k");
    pid = fork();
    assert(pid >= 0);
    if (!pid) {
        crash_after_state = 1;
        bc_recovery_apply(target, id, bc_str(review, "token"), 0, &e);
        _exit(99);
    }
    wait_crash(pid);
    json_object_put(review);
    expect(target, "4k");
    assert(bc_recovery_pending(target, &e) == 1);
    review = bc_recovery_inspect(target, id, &e);
    assert(review && !bc_recovery_apply(target, id, bc_str(review, "token"), 0, &e));
    json_object_put(review);
    assert(!bc_recovery_pending(target, &e));
    expect(target, "4k");
    review = bc_backup_inspect(target, backup, &e);
    assert(review);
    pid = fork();
    assert(pid >= 0);
    if (!pid) {
        crash_after_state = 1;
        bc_backup_restore(target, backup, bc_str(review, "token"), 1, &e);
        _exit(99);
    }
    wait_crash(pid);
    json_object_put(review);
    row = pending(target);
    snprintf(id, sizeof id, "%s", bc_str(row, "id"));
    json_object_put(row);
    review = bc_recovery_inspect(target, id, &e);
    assert(review);
    component(target, "5k");
    assert(bc_recovery_apply(target, id, bc_str(review, "token"), 1, &e) < 0);
    json_object_put(review);
    review = bc_recovery_inspect(target, id, &e);
    assert(review && bc_num(review, "conflicts") == 1);
    assert(bc_recovery_apply(target, id, bc_str(review, "token"), 0, &e) < 0);
    expect(target, "5k");
    assert(!bc_recovery_apply(target, id, bc_str(review, "token"), 1, &e));
    json_object_put(review);
    assert(!bc_recovery_pending(target, &e));
    expect(target, "5k");
    char journal_path[4096], relative[128];
    snprintf(relative, sizeof relative, "%s/index.json", id);
    assert(!bc_path(journal_path, sizeof journal_path, target, relative, &e));
    json_object *journal = bc_read_json(journal_path, &e);
    assert(journal);
    json_object *bad = bc_clone(journal);
    json_object_object_add(json_object_array_get_idx(bc_get(bad, "items"), 0), "path",
                           json_object_new_string("../fora.json"));
    assert(!bc_write_json(journal_path, bad, &e));
    assert(!bc_recovery_inspect(target, id, &e));
    assert(!bc_write_json(journal_path, journal, &e));
    json_object_put(bad);
    json_object_put(journal);
    json_object *plot =
        json_tokener_parse("{\"selected\":\"HV "
                           "estimada\",\"view\":{\"from\":0,\"to\":4,\"cursor_a\":1,\"cursor_b\":3}"
                           ",\"series\":[{\"name\":\"HV "
                           "estimada\",\"unit\":\"V\",\"target\":100,\"limit\":120,\"dropped\":300,"
                           "\"points\":[[0,0],[1,60],[2,85],[3,94],[4,99]]},{\"name\":\"=sinal de "
                           "teste\",\"unit\":\"V\",\"points\":[[0,1],[4,1.2]]}]}");
    assert(!bc_plot_validate(plot, &e));
    assert(!bc_path(path, sizeof path, root, "grafico.png", &e));
    assert(!bc_plot_png(plot, path, &e));
    assert(bc_plot_png(plot, path, &e) < 0);
    assert(!bc_path(path, sizeof path, root, "amostras.csv", &e));
    assert(!bc_plot_csv(plot, path, &e));
    assert(bc_plot_csv(plot, path, &e) < 0);
    FILE *f = fopen(path, "r");
    assert(f);
    char csv[4096];
    size_t size = fread(csv, 1, sizeof csv - 1, f);
    csv[size] = 0;
    fclose(f);
    assert(strstr(csv, "\"'=sinal de teste\"") && strstr(csv, ",1,60,100,120,300"));
    bad = bc_clone(plot);
    json_object_object_add(json_object_array_get_idx(bc_get(bad, "series"), 0), "points",
                           json_object_new_string("inválido"));
    assert(bc_plot_validate(bad, &e) < 0);
    json_object_put(bad);
    bad = bc_clone(plot);
    json_object_array_put_idx(
        json_object_array_get_idx(
            bc_get(json_object_array_get_idx(bc_get(bad, "series"), 0), "points"), 2),
        0, json_object_new_double(.5));
    assert(bc_plot_validate(bad, &e) < 0);
    json_object_put(bad);
    json_object *record = json_tokener_parse(
        "{\"format\":\"bpcb.result\",\"bpcb_version\":\"0.10.0\",\"board_id\":\"demonstracao-v1\","
        "\"status\":\"ok\",\"started_at\":\"2026-10-04T15:00:00Z\",\"snapshot\":{\"test_"
        "definition\":{\"title\":\"Ensaio simulado para validar o "
        "relatório\"},\"parameters\":{\"voltage\":100}},\"events\":[{\"type\":\"measurement\","
        "\"label\":\"Tensão "
        "simulada\",\"unit\":\"V\",\"value\":99,\"verdict\":\"pass\",\"source\":\"Simulação "
        "offline\",\"criterion\":\"Referência 100 V, tolerância 5 V\"}]}");
    json_object_object_add(record, "plot", bc_clone(plot));
    json_object *session = json_tokener_parse(
        "{\"format\":\"bpcb.session\",\"board_id\":\"demonstracao-v1\",\"started_at\":\"2026-10-"
        "04T15:00:00Z\",\"state\":\"Encerrada com etapa interrompida e "
        "pendente\",\"include_plots\":true,\"steps\":[{\"title\":\"Carga "
        "simulada\",\"summary\":\"Concluído\"},{\"title\":\"Medição "
        "interrompida\",\"summary\":\"Interrompido\"},{\"title\":\"Descarga não "
        "iniciada\",\"summary\":\"Pendente\"}]}");
    json_object *steps = bc_get(session, "steps");
    json_object_object_add(json_object_array_get_idx(steps, 0), "record", record);
    json_object *partial = bc_clone(record);
    json_object_object_del(partial, "plot");
    json_object_object_add(partial, "status", json_object_new_string("cancelled"));
    json_object_object_add(partial, "events", json_object_new_array());
    json_object_object_add(json_object_array_get_idx(steps, 1), "record", partial);
    assert(!bc_path(path, sizeof path, root, "sessao-parcial.pdf", &e));
    assert(!bc_report_pdf(session, path, &e));
    assert(bc_report_pdf(session, path, &e) < 0);
    json_object_put(session);
    json_object_put(plot);
    json_object *context =
        json_tokener_parse("{\"history_enabled\":false,\"messages\":[{\"operation\":\"rigol\","
                           "\"status\":\"error\",\"message\":\"Tempo de leitura "
                           "excedido\"}],\"instrument\":{\"endpoint\":\"tcp://"
                           "192.0.2.1:5555\",\"instrument\":\"RIGOL,DEMO,SERIAL-TESTE\"},"
                           "\"password\":\"NUNCA COPIAR\",\"results\":[1,2,3]}");
    json_object *diagnostic = bc_diagnostic(root, "demonstracao-v1", context, &e);
    assert(diagnostic && !bc_get(diagnostic, "instrument") && !bc_get(diagnostic, "password") &&
           !bc_get(diagnostic, "results") &&
           !json_object_get_boolean(bc_get(diagnostic, "history_enabled")));
    json_object_put(diagnostic);
    json_object_object_add(context, "include_instrument", json_object_new_boolean(1));
    diagnostic = bc_diagnostic(root, "demonstracao-v1", context, &e);
    assert(diagnostic && bc_get(diagnostic, "instrument"));
    assert(!bc_path(path, sizeof path, root, "diagnostico.json", &e));
    assert(!bc_export_json(diagnostic, path, &e));
    bad = bc_read_json(path, &e);
    assert(bad && json_object_equal(bad, diagnostic));
    assert(bc_export_json(diagnostic, path, &e) < 0);
    json_object_put(bad);
    json_object_put(diagnostic);
    json_object_put(context);
    assert(!bc_path(path, sizeof path, root, "results", &e) && access(path, F_OK) < 0);
    printf("Continuidade OK: queda durante restauração e recuperação, repetição idempotente, "
           "conflitos, prévia obsoleta, bloqueio de ensaio, caminhos, PNG/CSV, sessão parcial em "
           "PDF e diagnóstico opcional.\nEvidências: %s\n",
           root);
    return 0;
}
