#pragma once
#include <json-c/json.h>
#include <stddef.h>
#include "windows_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BC_VERSION "0.16.0"
#define BC_API 2

typedef struct {
    char text[768];
} bc_error;
typedef struct bc_job bc_job;
const char *bc_str(json_object *o, const char *key);
json_object *bc_get(json_object *o, const char *key);
double bc_num(json_object *o, const char *key);
int bc_fail(bc_error *e, const char *fmt, ...);
json_object *bc_clone(json_object *o);
json_object *bc_read_json(const char *path, bc_error *e);
int bc_write_json(const char *path, json_object *o, bc_error *e);
int bc_mkdirs(const char *path, bc_error *e);
int bc_path(char *out, size_t cap, const char *base, const char *tail, bc_error *e);
int bc_safe_path(const char *name);
json_object *bc_parse_json(const char *data, size_t size, bc_error *e);
int bc_utf8(const unsigned char *s, size_t n);
int bc_sha_file(const char *path, char out[65], bc_error *e);
int bc_component(const char *text, double *out);
int bc_parse_reading(const char *text, const char *base_unit, const char *display_unit, double *out,
                     bc_error *e);
void bc_local_time(const char *iso, char *out, size_t capacity);
json_object *bc_prepare(const char *root, const char *board, json_object *values,
                        int (*cancel)(void *), void *context, bc_error *e);
int bc_backup_create(const char *root, const char *destination, int include_history, bc_error *e);
json_object *bc_backup_inspect(const char *root, const char *path, bc_error *e);
int bc_backup_restore(const char *root, const char *path, const char *expected, int replace,
                      bc_error *e);
json_object *bc_recovery_list(const char *root, bc_error *e);
json_object *bc_recovery_inspect(const char *root, const char *id, bc_error *e);
int bc_recovery_apply(const char *root, const char *id, const char *token, int keep_current,
                      bc_error *e);
int bc_recovery_pending(const char *root, bc_error *e);
json_object *bc_diagnostic(const char *root, const char *board, json_object *context, bc_error *e);
int bc_export_json(json_object *value, const char *destination, bc_error *e);
int bc_plot_validate(json_object *plot, bc_error *e);
int bc_plot_csv(json_object *plot, const char *destination, bc_error *e);
int bc_plot_png(json_object *plot, const char *destination, bc_error *e);
int bc_report_pdf(json_object *record, const char *destination, bc_error *e);
json_object *bc_compare_records(json_object *left, json_object *right, bc_error *e);
int bc_eval(const char *expression, json_object *vars, double *out, bc_error *e);
int bc_validate(json_object *m, bc_error *e);
json_object *bc_apply(json_object *m, json_object *values, bc_error *e);
json_object *bc_test(json_object *m, const char *id);
json_object *bc_preview(json_object *m, json_object *test, bc_error *e);
int bc_pack(const char *folder, const char *destination, json_object *override, bc_error *e);
json_object *bc_inspect(const char *package, bc_error *e);
int bc_install(const char *root, const char *package, const char *expected, bc_error *e);
json_object *bc_profile(const char *root, const char *id, char folder[4096], bc_error *e);
json_object *bc_list(const char *root, bc_error *e);
int bc_boot(const char *root, const char *seed, bc_error *e);
int bc_save(const char *root, const char *id, json_object *values, bc_error *e);
int bc_export(const char *root, const char *id, const char *destination, json_object *values,
              bc_error *e);
int bc_verify(json_object *m, const char *folder, bc_error *e);
json_object *bc_browse(const char *path, int (*cancelled)(void *), void *context, bc_error *e);
json_object *bc_render_document(const char *root, const char *id, const char *name,
                                json_object *options, bc_error *e);
int bc_cal_validate(json_object *cal, bc_error *e);
int bc_cal_check(json_object *cal, json_object *components, double vref, bc_error *e);
double bc_cal_voltage(json_object *cal, unsigned channel, double nominal);
json_object *bc_cal_fit(json_object *profile, json_object *input, bc_error *e);
json_object *bc_cal_load(const char *root, const char *board, const char *unit, bc_error *e);
json_object *bc_cal_list(const char *root, const char *board, bc_error *e);
int bc_cal_save(const char *root, json_object *cal, bc_error *e);
json_object *bc_rigol_read(const char *endpoint, const char *field, int (*cancel)(void *),
                           void *context, bc_error *e);
json_object *bc_history_begin(const char *root, const char *board, const char *test,
                              json_object *identity, bc_error *e);
json_object *bc_record_new(const char *board, const char *test, json_object *identity, bc_error *e);
void bc_record_finish(json_object *record, json_object *end);
void bc_history_event(json_object *record, json_object *event);
int bc_history_finish(const char *root, json_object *record, json_object *end, bc_error *e);
json_object *bc_history_list(const char *root, bc_error *e);
json_object *bc_history_read(const char *root, const char *id, bc_error *e);
int bc_history_delete(const char *root, const char *id, bc_error *e);
int bc_history_export(const char *root, const char *id, const char *destination, bc_error *e);
int bc_job_command(bc_job *job, json_object *command);
void bc_assess(json_object *measurement);
int bc_open_document(const char *root, const char *id, const char *name, bc_error *e);
/* Operacoes demoradas e rotinas nativas ficam fora da thread da interface.
 * Cada evento retornado pertence ao chamador (json_object_put).
 * Uma rotina cancelada nunca equivale a desligamento fisico confirmado. */
bc_job *bc_job_start(const char *operation, const char *root, const char *id, const char *path,
                     const char *extra, json_object *values);
/* Avisos de novos eventos; permite integrar a GUI sem consultar por timer. */
int bc_job_notification_fd(bc_job *job);
void bc_job_acknowledge(bc_job *job);
json_object *bc_job_next(bc_job *job);
int bc_job_finished(bc_job *job);
void bc_job_cancel(bc_job *job);
void bc_job_reply(bc_job *job, const char *id, int accepted);
void bc_job_reply_value(bc_job *job, const char *id, int accepted, double value);
void bc_job_reply_acquired(bc_job *job, const char *id, int accepted, double value,
                           json_object *acquisition);
void bc_job_free(bc_job *job);
#ifdef __cplusplus
}
#endif
