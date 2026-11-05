#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include "communication.h"
#include "lab.h"
#include "workbench.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#ifdef _WIN32
#undef isfinite
#define isfinite __builtin_isfinite
#endif
#ifndef _WIN32
#include <poll.h>
#endif
#include <pthread.h>
#include <signal.h>
#ifndef _WIN32
#include <spawn.h>
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/file.h>
#endif
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <time.h>
#include <unistd.h>
#ifndef _WIN32
extern char **environ;
#endif
typedef struct event {
    json_object *o;
    struct event *next;
} event;

struct bc_job {
    pthread_t thread;
    pthread_mutex_t mutex;
    atomic_int stop, finished;
    event *first, *last;
    unsigned queued;
    int notify_fd[2];
    char *operation, *root, *id, *path, *extra;
    json_object *values;
    char reply_id[128];
    int reply, reply_ready, reply_has_value;
    double reply_value;
    json_object *command, *record, *reply_acquisition;
    int save_record;
    double started;
};

static double now(void);

static void notify(bc_job *j) {
#ifndef _WIN32
    const unsigned char byte = 1;
    /* Pipe nao bloqueante: varios eventos podem compartilhar o mesmo aviso. */
    while (write(j->notify_fd[1], &byte, 1) < 0 && errno == EINTR) {
    }
#else
    (void)j;
#endif
}

static void emit(bc_job *j, json_object *o) {
    if (!strcmp(j->operation, "run"))
        json_object_object_add(o, "elapsed_seconds", json_object_new_double(now() - j->started));
    bc_history_event(j->record, o);
    event *ev = calloc(1, sizeof *ev);
    if (!ev) {
        json_object_put(o);
        atomic_store(&j->stop, 1);
        return;
    }
    ev->o = o;
    pthread_mutex_lock(&j->mutex);
    if (j->queued > 512) {
        pthread_mutex_unlock(&j->mutex);
        free(ev);
        json_object_put(o);
        atomic_store(&j->stop, 1);
        return;
    }
    if (j->last)
        j->last->next = ev;
    else
        j->first = ev;
    j->last = ev;
    j->queued++;
    pthread_mutex_unlock(&j->mutex);
    notify(j);
}

static json_object *message(const char *type, const char *text) {
    json_object *o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string(type));
    json_object_object_add(o, "message", json_object_new_string(text));
    return o;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int sample_valid(json_object *event) {
    json_object *values = bc_get(event, "values");
    if (!json_object_is_type(values, json_type_object) || json_object_object_length(values) > 16 ||
        !*bc_str(event, "unit") || strlen(bc_str(event, "unit")) > 32)
        return 0;
    json_object_object_foreach(values, key, value) {
        if (!*key || strlen(key) > 160 ||
            (!json_object_is_type(value, json_type_double) &&
             !json_object_is_type(value, json_type_int)) ||
            !isfinite(json_object_get_double(value)))
            return 0;
    }
    const char *keys[] = {"elapsed", "target", "limit"};
    for (unsigned i = 0; i < 3; i++) {
        json_object *v = bc_get(event, keys[i]);
        if (v &&
            ((!json_object_is_type(v, json_type_double) &&
              !json_object_is_type(v, json_type_int)) ||
             !isfinite(json_object_get_double(v)) || (i == 0 && json_object_get_double(v) < 0)))
            return 0;
    }
    return 1;
}

static int native_run(bc_job *j, json_object **result, bc_error *e) {
    char folder[4096], exe[4096], lockfile[4096];
    json_object *m = bc_profile(j->root, j->id, folder, e);
    if (!m)
        return -1;
    json_object *profile = bc_apply(m, j->values, e);
    json_object_put(m);
    if (!profile)
        return -1;
    json_object *t = bc_test(profile, j->path);
    int rc = -1, lock = -1, in[2] = {-1, -1}, out[2] = {-1, -1}, status = 0, reaped = 0, ended = 0,
        pending = 0, eof = 0;
#ifdef _WIN32
    bc_win_child child = {0};
#else
    pid_t pid = -1;
#endif
    char pending_id[128] = "";
    char *send = NULL;
    size_t send_at = 0, send_size = 0;
    json_object *snapshot = NULL, *request = NULL;
    double stop_at = 0;
    if (!t || !json_object_get_boolean(bc_get(t, "enabled"))) {
        bc_fail(e, "Esta rotina ainda nao esta habilitada no pacote.");
        goto done;
    }
    if (bc_verify(profile, folder, e) < 0 ||
        bc_path(exe, sizeof exe, folder, bc_str(t, "entry"), e) < 0 ||
        bc_path(lockfile, sizeof lockfile, j->root, "execution.lock", e) < 0)
        goto done;
    lock = open(lockfile, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
        bc_fail(e, "Ja existe um teste em execucao.");
        goto done;
    }
    int recovery = bc_recovery_pending(j->root, e);
    if (recovery) {
        if (recovery > 0)
            bc_fail(e, "Restauração interrompida: confira Recuperação assistida antes de executar "
                       "ensaios.");
        goto done;
    }
    if (atomic_load(&j->stop)) {
        rc = 1;
        goto done;
    }
    request = json_object_new_object();
    json_object_object_add(request, "api", json_object_new_int(BC_API));
    json_object_object_add(request, "test", json_object_new_string(j->path));
    json_object_object_add(request, "package_dir", json_object_new_string(folder));
    json_object_object_add(request, "components", json_object_get(bc_get(profile, "components")));
    json_object_object_add(request, "mode", json_object_new_string(bc_str(t, "mode")));
    json_object *parameters = json_object_new_object(), *params = bc_get(t, "parameters");
    for (size_t i = 0; i < json_object_array_length(params); i++) {
        json_object *p = json_object_array_get_idx(params, i);
        json_object_object_add(parameters, bc_str(p, "id"), json_object_get(bc_get(p, "default")));
    }
    json_object_object_add(request, "parameters", parameters);
    if (bc_get(j->values, "studio_debug") &&
        !json_object_is_type(bc_get(j->values, "studio_debug"), json_type_boolean)) {
        bc_fail(e, "Opção de depuração deve ser booleana.");
        goto done;
    }
    int studio_debug = json_object_get_boolean(bc_get(j->values, "studio_debug"));
    if (studio_debug) {
        if (strcmp(bc_str(t, "mode"), "simulation") || !bc_get(profile, "studio")) {
            bc_fail(e, "Depuração exige simulação do Studio.");
            goto done;
        }
        json_object_object_add(request, "studio_debug", json_object_new_boolean(1));
    }
    if (!strcmp(bc_str(t, "mode"), "hardware")) {
        json_object *communication =
            bc_comm_resolve(profile, t, bc_get(j->values, "communication"), e);
        if (!communication)
            goto done;
        json_object_object_add(request, "communication", communication);
        json_object *definition = bc_get(t, "communication");
        if (!definition)
            definition = bc_get(profile, "communication");
        if (definition)
            json_object_object_add(request, "communication_definition", bc_clone(definition));
    }

    if (*j->extra) {
        json_object *cal = bc_cal_load(j->root, j->id, j->extra, e);
        if (!cal)
            goto done;
        if (strcmp(bc_str(cal, "revision"), bc_str(profile, "revision")) ||
            bc_cal_check(cal, bc_get(profile, "components"), bc_num(parameters, "vref_mv"), e) <
                0) {
            json_object_put(cal);
            if (!e->text[0])
                bc_fail(e, "Calibração de outra revisão.");
            goto done;
        }
        json_object_object_add(request, "calibration", cal);
    }

    snapshot = bc_clone(request);
    json_object_object_add(snapshot, "test_definition", bc_clone(t));
    json_object_object_add(snapshot, "pcb", json_object_new_string(bc_str(profile, "pcb")));
    json_object_object_add(snapshot, "revision",
                           json_object_new_string(bc_str(profile, "revision")));
    json_object_object_add(snapshot, "version", json_object_new_string(bc_str(profile, "version")));
    json_object *start = message("snapshot", bc_str(t, "title"));
    json_object_object_add(start, "data", json_object_get(snapshot));
    emit(j, start);
    const char *text = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
    send_size = strlen(text) + 1;
    send = malloc(send_size + 1);
    if (!send) {
        bc_fail(e, "Memoria insuficiente.");
        goto done;
    }
    snprintf(send, send_size + 1, "%s\n", text);
#ifdef _WIN32
    if (bc_win_spawn(exe, &in[1], &out[0], &child, e->text, sizeof e->text))
        goto done;
#else
    if (pipe(in) < 0 || pipe(out) < 0) {
        bc_fail(e, "Criar canal da rotina: %s", strerror(errno));
        goto done;
    }
    for (int i = 0; i < 2; i++) {
        fcntl(in[i], F_SETFD, FD_CLOEXEC);
        fcntl(out[i], F_SETFD, FD_CLOEXEC);
    }
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&attr);
    posix_spawn_file_actions_adddup2(&fa, in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&fa, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, out[1], STDERR_FILENO);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    char *argv[] = {exe, NULL};
    int spawn = posix_spawn(&pid, exe, &fa, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&fa);
    close(in[0]);
    in[0] = -1;
    close(out[1]);
    out[1] = -1;
    if (spawn) {
        pid = -1;
        bc_fail(e, "Executar rotina: %s", strerror(spawn));
        goto done;
    }
    fcntl(in[1], F_SETFL, O_NONBLOCK);
    fcntl(out[0], F_SETFL, O_NONBLOCK);
#endif
    double timeout = bc_num(t, "timeout");
    if (timeout <= 0 || timeout > 3600)
        timeout = 60;
    double deadline = now() + timeout, debug_last = now(), debug_hard = now() + 7200;
    int debug_pending = 0;
    char buffer[65536];
    size_t used = 0;
    unsigned measures = 0;
    while (!(reaped && eof)) {
        double instant = now();
        if (debug_pending)
            deadline += instant - debug_last;
        debug_last = instant;
        if (studio_debug && instant > debug_hard) {
            bc_fail(e, "Limite de duas horas da sessão de depuração atingido.");
            atomic_store(&j->stop, 1);
        }
        if (instant > deadline && !stop_at) {
            bc_fail(e, "Tempo limite da rotina excedido; estado fisico nao confirmado.");
            atomic_store(&j->stop, 1);
        }
        if (atomic_load(&j->stop) && !stop_at) {
            stop_at = instant;
#ifdef _WIN32
            bc_win_stop(&child, 0);
#else
            kill(-pid, SIGTERM);
#endif
            emit(j, message("status", "Parada solicitada. Estado fisico nao confirmado."));
        }
#ifdef _WIN32
        if (stop_at && instant - stop_at > 8.0)
            bc_win_stop(&child, 1);
#else
        if (stop_at && instant - stop_at > 8.0)
            kill(-pid, SIGKILL);
#endif
        if (stop_at && instant - stop_at > 9.0)
            break;
        if (send_at < send_size && in[1] >= 0) {
            ssize_t n = write(in[1], send + send_at, send_size - send_at);
            if (n > 0)
                send_at += (size_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                close(in[1]);
                in[1] = -1;
            }
        }
        pthread_mutex_lock(&j->mutex);
        if (j->reply_ready) {
            if (pending && !strcmp(j->reply_id, pending_id) && send_at == send_size) {
                json_object *reply = json_object_new_object();
                json_object_object_add(reply, "reply_to", json_object_new_string(j->reply_id));
                json_object_object_add(reply, "accepted", json_object_new_boolean(j->reply));
                if (j->reply_has_value)
                    json_object_object_add(reply, "value", json_object_new_double(j->reply_value));
                const char *s = json_object_to_json_string_ext(reply, JSON_C_TO_STRING_PLAIN);
                free(send);
                send_size = strlen(s) + 1;
                send = malloc(send_size + 1);
                send_at = 0;
                if (send)
                    snprintf(send, send_size + 1, "%s\n", s);
                else {
                    send_size = 0;
                    atomic_store(&j->stop, 1);
                }
                if (j->reply_acquisition)
                    json_object_object_add(reply, "acquisition", bc_clone(j->reply_acquisition));
                json_object_object_add(reply, "type", json_object_new_string("operator_reply"));
                bc_history_event(j->record, reply);
                json_object_put(reply);
                pending = 0;
                debug_pending = 0;
                if (!j->reply)
                    atomic_store(&j->stop, 1);
            }
            j->reply_ready = 0;
        }
        if (j->command && !pending && send_at == send_size) {
            const char *s = json_object_to_json_string_ext(j->command, JSON_C_TO_STRING_PLAIN);
            free(send);
            send_size = strlen(s) + 1;
            send = malloc(send_size + 1);
            send_at = 0;
            if (send)
                snprintf(send, send_size + 1, "%s\n", s);
            else {
                send_size = 0;
                atomic_store(&j->stop, 1);
            }
            json_object *logged = bc_clone(j->command);
            json_object_object_add(logged, "type", json_object_new_string("manual_command"));
            bc_history_event(j->record, logged);
            json_object_put(logged);
            json_object_put(j->command);
            j->command = NULL;
        }
        pthread_mutex_unlock(&j->mutex);
        struct pollfd poller = {out[0], POLLIN, 0};
        poll(&poller, 1, 20);
        if (!eof) {
            ssize_t n = read(out[0], buffer + used, sizeof buffer - 1 - used);
            if (n > 0)
                used += (size_t)n;
            else if (n == 0)
                eof = 1;
            else if (errno != EAGAIN && errno != EINTR)
                eof = 1;
            buffer[used] = 0;
            char *line;
            while ((line = memchr(buffer, '\n', used))) {
                size_t count = (size_t)(line - buffer);
                *line = 0;
                json_object *ev = json_tokener_parse(buffer);
                if (!ev || !json_object_is_type(ev, json_type_object) || ended) {
                    if (ev)
                        json_object_put(ev);
                    bc_fail(e, "Protocolo de rotina invalido.");
                    atomic_store(&j->stop, 1);
                } else {
                    const char *kind = bc_str(ev, "type");
                    if (!strcmp(kind, "measurement")) {
                        json_object *value = bc_get(ev, "value");
                        if (++measures > 256 ||
                            (!json_object_is_type(value, json_type_double) &&
                             !json_object_is_type(value, json_type_int)) ||
                            !isfinite(json_object_get_double(value)) || !*bc_str(ev, "label") ||
                            !*bc_str(ev, "unit")) {
                            bc_fail(e, "Medicao invalida recebida.");
                            atomic_store(&j->stop, 1);
                        } else {
                            if (!strcmp(bc_str(t, "mode"), "simulation"))
                                json_object_object_add(ev, "source",
                                                       json_object_new_string("Simulação"));
                            bc_assess(ev);
                            time_t wall = time(NULL);
                            struct tm local;
                            localtime_r(&wall, &local);
                            char stamp[64];
                            strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S %z", &local);
                            json_object_object_add(ev, "timestamp", json_object_new_string(stamp));
                            emit(j, json_object_get(ev));
                        }
                    } else if (!strcmp(kind, "confirm")) {
                        if (pending || !*bc_str(ev, "id") ||
                            strlen(bc_str(ev, "id")) >= sizeof pending_id) {
                            bc_fail(e, "Confirmacao externa invalida.");
                            atomic_store(&j->stop, 1);
                        } else if (json_object_get_boolean(bc_get(ev, "debug")) && !studio_debug) {
                            bc_fail(e, "Pausa de depuração não autorizada para esta execução.");
                            atomic_store(&j->stop, 1);
                        } else {
                            debug_pending = json_object_get_boolean(bc_get(ev, "debug"));
                            pending = 1;
                            snprintf(pending_id, sizeof pending_id, "%s", bc_str(ev, "id"));
                            emit(j, json_object_get(ev));
                        }
                    } else if (!strcmp(kind, "sample") && sample_valid(ev))
                        emit(j, json_object_get(ev));
                    else if (!strcmp(kind, "status")) {
                        json_object *transport = bc_get(ev, "transport");
                        if (transport) {
                            json_object *reported =
                                            bc_comm_config(bc_get(transport, "configuration"), e),
                                        *selection = bc_get(request, "communication"),
                                        *selected =
                                            bc_num(selection, "api") == 2
                                                ? bc_lab_channel_config(
                                                      selection, bc_str(transport, "channel"))
                                                : selection;
                            int valid =
                                reported && selected &&
                                !strcmp(bc_str(reported, "adapter"), bc_str(selected, "adapter")) &&
                                !strcmp(bc_str(reported, "bus"), bc_str(selected, "bus")) &&
                                (!*bc_str(selected, "device") ||
                                 !strcmp(bc_str(selected, "device"), bc_str(reported, "device"))) &&
                                (!*bc_str(selected, "serial") ||
                                 !strcmp(bc_str(selected, "serial"), bc_str(reported, "serial")));
                            if (valid && !strcmp(bc_str(selected, "adapter"), "ch347") &&
                                bc_num(selected, "ch347_mode") != -1 &&
                                bc_num(reported, "ch347_mode") != bc_num(selected, "ch347_mode"))
                                valid = 0;
                            if (!valid) {
                                if (reported)
                                    json_object_put(reported);
                                bc_fail(e, "Identidade efetiva do adaptador diverge da seleção.");
                                atomic_store(&j->stop, 1);
                            } else {
                                if (bc_num(selection, "api") == 2) {
                                    json_object *bundle = bc_get(snapshot, "communication");
                                    if (bc_num(bundle, "api") != 2) {
                                        bundle = bc_clone(selection);
                                        json_object_object_add(snapshot, "communication", bundle);
                                    }
                                    json_object_object_add(bc_get(bundle, "channels"),
                                                           bc_str(transport, "channel"), reported);
                                    json_object *clocks =
                                        bc_get(snapshot, "communication_actual_hz");
                                    if (!json_object_is_type(clocks, json_type_object)) {
                                        clocks = json_object_new_object();
                                        json_object_object_add(snapshot, "communication_actual_hz",
                                                               clocks);
                                    }
                                    json_object_object_add(
                                        clocks, bc_str(transport, "channel"),
                                        json_object_new_double(bc_num(transport, "actual_hz")));
                                } else {
                                    json_object_object_add(snapshot, "communication", reported);
                                    json_object_object_add(
                                        snapshot, "communication_actual_hz",
                                        json_object_new_double(bc_num(transport, "actual_hz")));
                                }
                                json_object *update = message("snapshot", bc_str(t, "title"));
                                json_object_object_add(update, "data", bc_clone(snapshot));
                                emit(j, update);
                            }
                        }
                        emit(j, json_object_get(ev));
                    } else if (!strcmp(kind, "done")) {
                        ended = 1;
                        json_object_object_add(snapshot, "message",
                                               json_object_new_string(bc_str(ev, "message")));
                    } else if (!strcmp(kind, "error")) {
                        bc_fail(e, "%s", bc_str(ev, "message"));
                        atomic_store(&j->stop, 1);
                    } else {
                        bc_fail(e, "Evento de rotina desconhecido.");
                        atomic_store(&j->stop, 1);
                    }
                    json_object_put(ev);
                }
                size_t consumed = count + 1;
                used -= consumed;
                memmove(buffer, buffer + consumed, used);
                buffer[used] = 0;
            }
            if (used == sizeof buffer - 1) {
                bc_fail(e, "Mensagem de rotina grande demais.");
                atomic_store(&j->stop, 1);
            }
        }
#ifdef _WIN32
        if (!reaped)
            reaped = bc_win_exited(&child, &status);
#else
        if (!reaped) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid)
                reaped = 1;
            else if (w < 0 && errno != EINTR) {
                bc_fail(e, "Falha ao aguardar a rotina.");
                break;
            }
        }
#endif
    }
    if (e->text[0])
        rc = -1;
    else if (atomic_load(&j->stop))
        rc = 1;
    else if (!reaped || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || !ended || pending || used)
        bc_fail(e, "Rotina terminou sem confirmar a conclusao; estado fisico nao confirmado.");
    else {
        rc = 0;
        *result = json_object_get(snapshot);
    }
done:
#ifdef _WIN32
    bc_win_close_child(&child);
#else
    if (pid > 0) {
        kill(-pid, SIGKILL);
        if (!reaped)
            while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
            }
    }
#endif
    for (int i = 0; i < 2; i++) {
        if (in[i] >= 0)
            close(in[i]);
        if (out[i] >= 0)
            close(out[i]);
    }
    if (lock >= 0)
        close(lock);
    free(send);
    if (request)
        json_object_put(request);
    if (snapshot)
        json_object_put(snapshot);
    json_object_put(profile);
    return rc;
}

static int browse_cancelled(void *arg) {
    return atomic_load(&((bc_job *)arg)->stop);
}

static int comm_progress(void *context, json_object *data) {
    bc_job *j = context;
    json_object *event = message("communication_scan_row", "");
    json_object_object_add(event, "data", bc_clone(data));
    emit(j, event);
    return atomic_load(&j->stop) ? -1 : 0;
}

static void terminal_event(bc_job *j, const char *direction, const uint8_t *data, size_t size,
                           int complete) {
    char *hex = bc_comm_format_bytes(data, size, 1), *ascii = bc_comm_format_bytes(data, size, 0);
    if (!hex || !ascii) {
        free(hex);
        free(ascii);
        atomic_store(&j->stop, 1);
        return;
    }
    json_object *event = message("communication_terminal_data", "");
    json_object_object_add(event, "direction", json_object_new_string(direction));
    json_object_object_add(event, "hex", json_object_new_string(hex));
    json_object_object_add(event, "ascii", json_object_new_string(ascii));
    json_object_object_add(event, "complete", json_object_new_boolean(complete));
    json_object_object_add(event, "elapsed", json_object_new_double(now() - j->started));
    json_object_object_add(event, "session", bc_clone(bc_get(j->values, "terminal_session")));
    json_object_object_add(event, "transport", bc_clone(bc_get(j->values, "terminal_transport")));
    free(hex);
    free(ascii);
    emit(j, event);
}

static json_object *terminal_run(bc_job *j, bc_comm *s, bc_error *e) {
    j->started = now();
    struct timespec started;
    clock_gettime(CLOCK_REALTIME, &started);
    char session[80];
    snprintf(session, sizeof session, "uart-%lld-%09ld", (long long)started.tv_sec,
             started.tv_nsec);
    json_object_object_add(j->values, "terminal_session", json_object_new_string(session));
    json_object_object_add(j->values, "terminal_transport", bc_comm_information(s));
    json_object *options = bc_get(j->values, "operation");
    const char *delim = bc_str(options, "delimiter");
    const char *end = !strcmp(delim, "none")   ? ""
                      : !strcmp(delim, "lf")   ? "\n"
                      : !strcmp(delim, "cr")   ? "\r"
                      : !strcmp(delim, "crlf") ? "\r\n"
                                               : NULL;
    int seconds =
        bc_get(options, "seconds") ? json_object_get_int(bc_get(options, "seconds")) : 300;
    if (!end || !json_object_is_type(bc_get(options, "seconds"), json_type_int) || seconds < 1 ||
        seconds > 3600) {
        bc_fail(e, "Terminal: duração 1..3600 segundos e delimitador none/lf/cr/crlf.");
        return NULL;
    }
    uint8_t pending[BC_COMM_MAX], bytes[BC_COMM_MAX];
    size_t used = 0, total = 0;
    double deadline = now() + seconds;
    int failed = 0;
    while (!atomic_load(&j->stop) && now() < deadline) {
        pthread_mutex_lock(&j->mutex);
        json_object *command = j->command;
        j->command = NULL;
        pthread_mutex_unlock(&j->mutex);
        if (command) {
            size_t n = 0;
            int rc = bc_comm_hex(bc_str(command, "write"), bytes, sizeof bytes, &n, e);
            json_object_put(command);
            if (rc < 0 || !n || bc_comm_uart(s, bytes, n, NULL, 0, e) < 0) {
                failed = 1;
                break;
            }
            terminal_event(j, "tx", bytes, n, 1);
        }
        int n = bc_comm_uart_poll(s, bytes, sizeof bytes, 50, e);
        if (n < 0) {
            if (!atomic_load(&j->stop))
                failed = 1;
            break;
        }
        total += (size_t)n;
        for (int i = 0; i < n; i++) {
            pending[used++] = bytes[i];
            size_t dn = strlen(end);
            int matched = dn && used >= dn && !memcmp(pending + used - dn, end, dn);
            if (matched || used == sizeof pending || (!dn && i + 1 == n)) {
                terminal_event(j, "rx", pending, used, matched || !dn);
                used = 0;
            }
        }
    }
    if (used)
        terminal_event(j, "rx", pending, used, 0);
    if (failed)
        return NULL;
    json_object *r = bc_comm_information(s);
    json_object_object_add(r, "bytes_read", json_object_new_int64((int64_t)total));
    json_object_object_add(r, "stopped", json_object_new_boolean(atomic_load(&j->stop)));
    return r;
}

static json_object *communication_tool(bc_job *j, bc_error *e) {
    if (!strcmp(j->operation, "communication_firmware")) {
        json_object *r = bc_firmware_run(bc_get(j->values, "operation"), browse_cancelled, j, e);
        if (r && !json_object_get_boolean(bc_get(r, "ok")))
            bc_fail(e, "Ferramenta de firmware retornou falha; consulte código/log.");
        return r;
    }
    if (!strcmp(j->operation, "communication_replay")) {
        json_object *request = bc_get(j->values, "operation");
        return bc_replay_execute(bc_get(request, "capture"), bc_get(request, "operation"),
                                 bc_get(request, "fault"), e);
    }
    if (!strcmp(j->operation, "communication_lab")) {
        char lockfile[4096];
        int guard = open(bc_bench_lock_path(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        (void)lockfile;
        if (guard < 0 || flock(guard, LOCK_EX | LOCK_NB) < 0) {
            if (guard >= 0)
                close(guard);
            bc_fail(e, "Outra ferramenta está usando a bancada.");
            return NULL;
        }
        bc_lab *lab = bc_lab_new(bc_get(j->values, "communication"),
                                 bc_get(j->values, "definition"), browse_cancelled, j, e);
        json_object *r = lab ? bc_lab_execute(lab, bc_get(j->values, "operation"), e) : NULL;
        bc_lab_free(lab);
        close(guard);
        if (r && bc_get(r, "matched") && !json_object_get_boolean(bc_get(r, "matched")))
            bc_fail(e, "Resposta não atendeu ao critério do protocolo.");
        return r;
    }

    if (!strcmp(j->operation, "communication_openocd")) {
        json_object *r = bc_openocd_run(bc_get(j->values, "operation"), browse_cancelled, j, e);
        if (r && !json_object_get_boolean(bc_get(r, "ok"))) {
            if (bc_get(r, "ch347_supported") &&
                !json_object_get_boolean(bc_get(r, "ch347_supported")))
                bc_fail(e, "Esta instalação OpenOCD não contém o driver CH347.");
            else
                bc_fail(
                    e,
                    "OpenOCD retornou código %.0f; consulte o log. Estado físico não confirmado.",
                    bc_num(r, "exit_code"));
        }
        return r;
    }
    json_object *config = bc_comm_config(bc_get(j->values, "communication"), e);
    if (!config)
        return NULL;
    if (!json_object_get_boolean(bc_get(config, "connections_checked"))) {
        json_object_put(config);
        bc_fail(e, "Confira as conexões antes de operar o barramento.");
        return NULL;
    }
    const char *bus = bc_str(config, "bus");
    int terminal = !strcmp(j->operation, "communication_terminal"),
        scan = !strcmp(j->operation, "communication_scan"),
        identify = !strcmp(j->operation, "communication_identify");
    if ((terminal && strcmp(bus, "uart")) || (scan && strcmp(bus, "i2c")) ||
        (identify && strcmp(bus, "jtag"))) {
        json_object_put(config);
        bc_fail(e, "A ferramenta não corresponde ao barramento selecionado.");
        return NULL;
    }
    json_object *op = bc_get(j->values, "operation");
    if (scan &&
        (!json_object_is_type(bc_get(op, "first"), json_type_int) ||
         !json_object_is_type(bc_get(op, "last"), json_type_int) || bc_num(op, "first") < 8 ||
         bc_num(op, "last") > 119 || bc_num(op, "first") > bc_num(op, "last"))) {
        json_object_put(config);
        bc_fail(e, "Varredura: intervalo 0x08..0x77.");
        return NULL;
    }
    if (terminal) {
        const char *d = bc_str(op, "delimiter");
        if (!json_object_is_type(bc_get(op, "seconds"), json_type_int) ||
            bc_num(op, "seconds") < 1 || bc_num(op, "seconds") > 3600 ||
            (strcmp(d, "none") && strcmp(d, "lf") && strcmp(d, "cr") && strcmp(d, "crlf"))) {
            json_object_put(config);
            bc_fail(e, "Terminal: duração ou delimitador inválido; porta não foi aberta.");
            return NULL;
        }
    }
    int lock = open(bc_bench_lock_path(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
        if (lock >= 0)
            close(lock);
        json_object_put(config);
        bc_fail(e, "Outra ferramenta está usando a bancada.");
        return NULL;
    }
    bc_comm *s = NULL;
    json_object *r = NULL;
    if (!bc_comm_open(&s, config, browse_cancelled, j, e)) {
        if (terminal)
            r = terminal_run(j, s, e);
        else if (scan)
            r = bc_comm_scan(&s, (unsigned)bc_num(op, "first"), (unsigned)bc_num(op, "last"),
                             comm_progress, j, e);
        else if (identify) {
            r = bc_comm_jtag_identify(s, 32, e);
            if (r)
                json_object_object_add(r, "transport", bc_comm_information(s));
        }
    }
    bc_comm_close(s);
    json_object_put(config);
    close(lock);
    return r;
}

static void *work(void *arg) {
    bc_job *j = arg;
    bc_error e = {{0}};
    json_object *result = NULL;
    int rc = 0;
    if (!strcmp(j->operation, "save") || !strcmp(j->operation, "install") ||
        !strcmp(j->operation, "calibration_save")) {
        int pending = bc_recovery_pending(j->root, &e);
        if (pending) {
            if (pending > 0)
                bc_fail(&e,
                        "Resolva a recuperação pendente antes de alterar cadastros ou pacotes.");
            rc = -1;
            goto finished;
        }
    }
    if (!strcmp(j->operation, "boot")) {
        rc = bc_boot(j->root, j->path, &e);
        if (!rc)
            result = bc_list(j->root, &e);
    } else if (!strcmp(j->operation, "list"))
        result = bc_list(j->root, &e);
    else if (!strcmp(j->operation, "browse")) {
        result = bc_browse(j->path, browse_cancelled, j, &e);
        if (atomic_load(&j->stop))
            rc = 1;
    } else if (!strcmp(j->operation, "inspect"))
        result = bc_inspect(j->path, &e);
    else if (!strcmp(j->operation, "install"))
        rc = bc_install(j->root, j->path, j->extra, &e);
    else if (!strcmp(j->operation, "save"))
        rc = bc_save(j->root, j->id, j->values, &e);
    else if (!strcmp(j->operation, "calibration_list"))
        result = bc_cal_list(j->root, j->id, &e);
    else if (!strcmp(j->operation, "calibration_save")) {
        char folder[4096];
        json_object *m = bc_profile(j->root, j->id, folder, &e);
        json_object *current = m ? bc_apply(m, j->values, &e) : NULL;
        if (m)
            json_object_put(m);
        result = current ? bc_cal_fit(current, j->values, &e) : NULL;
        if (current)
            json_object_put(current);
        if (result)
            rc = bc_cal_save(j->root, result, &e);
        else
            rc = -1;
    }

    else if (!strcmp(j->operation, "export"))
        rc = bc_export(j->root, j->id, j->path, j->values, &e);
    else if (!strcmp(j->operation, "render_document"))
        result = bc_render_document(j->root, j->id, j->path, j->values, &e);
    else if (!strcmp(j->operation, "document"))
        rc = bc_open_document(j->root, j->id, j->path, &e);
    else if (!strcmp(j->operation, "rigol"))
        result = bc_rigol_read(j->path, j->extra, browse_cancelled, j, &e);
    else if (!strcmp(j->operation, "history_list"))
        result = bc_history_list(j->root, &e);
    else if (!strcmp(j->operation, "history_read"))
        result = bc_history_read(j->root, j->path, &e);
    else if (!strcmp(j->operation, "history_delete"))
        rc = bc_history_delete(j->root, j->path, &e);
    else if (!strcmp(j->operation, "history_export"))
        rc = bc_history_export(j->root, j->id, j->path, &e);
    else if (!strcmp(j->operation, "firmware_bundle_inspect"))
        result = bc_firmware_bundle_inspect(j->path, &e);
    else if (!strcmp(j->operation, "firmware_bundle_import"))
        result = bc_firmware_bundle_import(j->path, bc_str(j->values, "destination"),
                                           bc_str(j->values, "expected_sha256"), &e);
    else if (!strcmp(j->operation, "firmware_bundle_export"))
        rc = bc_firmware_bundle_export(bc_get(j->values, "manifest"), j->path, &e);
    else if (!strcmp(j->operation, "communication_list"))
        result = bc_comm_list(browse_cancelled, j, &e);
    else if (!strcmp(j->operation, "communication_probe"))
        result = bc_comm_probe(bc_get(j->values, "communication"), browse_cancelled, j, &e);
    else if (!strcmp(j->operation, "communication_exchange") ||
             !strcmp(j->operation, "communication_scan") ||
             !strcmp(j->operation, "communication_identify") ||
             !strcmp(j->operation, "communication_terminal") ||
             !strcmp(j->operation, "communication_openocd") ||
             !strcmp(j->operation, "communication_lab") ||
             !strcmp(j->operation, "communication_firmware") ||
             !strcmp(j->operation, "communication_replay")) {
        char lockfile[4096];
        int execution = -1;
        if (bc_recovery_pending(j->root, &e)) {
            if (!e.text[0])
                bc_fail(&e, "Resolva a recuperação pendente antes de transmitir.");
            rc = -1;
        } else if (bc_path(lockfile, sizeof lockfile, j->root, "execution.lock", &e) < 0)
            rc = -1;
        else {
            execution = open(lockfile, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (execution < 0 || flock(execution, LOCK_EX | LOCK_NB) < 0) {
                bc_fail(&e, "Outra operação está usando a bancada.");
                rc = -1;
            } else if (!strcmp(j->operation, "communication_exchange"))
                result = bc_comm_exchange(bc_get(j->values, "communication"),
                                          bc_get(j->values, "operation"), browse_cancelled, j, &e);
            else
                result = communication_tool(j, &e);
        }
        if (execution >= 0)
            close(execution);
    } else if (!strcmp(j->operation, "prepare"))
        result = bc_prepare(j->root, j->id, j->values, browse_cancelled, j, &e);
    else if (!strcmp(j->operation, "backup_create"))
        rc = bc_backup_create(j->root, j->path,
                              json_object_get_boolean(bc_get(j->values, "include_history")), &e);
    else if (!strcmp(j->operation, "backup_inspect"))
        result = bc_backup_inspect(j->root, j->path, &e);
    else if (!strcmp(j->operation, "backup_restore"))
        rc = bc_backup_restore(j->root, j->path, j->extra,
                               json_object_get_boolean(bc_get(j->values, "replace")), &e);
    else if (!strcmp(j->operation, "report_pdf"))
        rc = bc_report_pdf(j->values, j->path, &e);
    else if (!strcmp(j->operation, "plot_csv"))
        rc = bc_plot_csv(j->values, j->path, &e);
    else if (!strcmp(j->operation, "plot_png"))
        rc = bc_plot_png(j->values, j->path, &e);
    else if (!strcmp(j->operation, "diagnostic_preview"))
        result = bc_diagnostic(j->root, j->id, j->values, &e);
    else if (!strcmp(j->operation, "diagnostic_export"))
        rc = bc_export_json(j->values, j->path, &e);
    else if (!strcmp(j->operation, "recovery_list"))
        result = bc_recovery_list(j->root, &e);
    else if (!strcmp(j->operation, "recovery_inspect"))
        result = bc_recovery_inspect(j->root, j->path, &e);
    else if (!strcmp(j->operation, "recovery_apply"))
        rc = bc_recovery_apply(j->root, j->path, j->extra,
                               json_object_get_boolean(bc_get(j->values, "keep_current")), &e);
    else if (!strcmp(j->operation, "history_pdf")) {
        json_object *record = bc_history_read(j->root, j->id, &e);
        if (record) {
            rc = bc_report_pdf(record, j->path, &e);
            json_object_put(record);
        } else
            rc = -1;
    } else if (!strcmp(j->operation, "run")) {
        json_object *setting = bc_get(j->values, "history");
        j->started = now();
        j->save_record = json_object_is_type(bc_get(setting, "enabled"), json_type_boolean) &&
                         json_object_get_boolean(bc_get(setting, "enabled"));
        j->record = j->save_record
                        ? bc_history_begin(j->root, j->id, j->path, setting, &e)
                        : bc_record_new(j->id, j->path, bc_get(j->values, "identity"), &e);
        if (!j->record)
            rc = -1;
        if (!rc)
            rc = native_run(j, &result, &e);
    } else
        rc = bc_fail(&e, "Operacao desconhecida.");
finished:
    if (!strcmp(j->operation, "communication_terminal") && result && atomic_load(&j->stop)) {
        e.text[0] = 0;
        rc = 1;
    }
    if (e.text[0])
        rc = -1;
    json_object *end =
        message("finished", rc < 0    ? e.text
                            : rc == 1 ? "Execução interrompida. Estado físico não confirmado."
                                      : "Operação concluída.");
    json_object_object_add(end, "status",
                           json_object_new_string(rc < 0    ? "error"
                                                  : rc == 1 ? "cancelled"
                                                            : "ok"));
    if (result)
        json_object_object_add(end, "data", result);
    if (j->record) {
        bc_error save_error = {{0}};
        if (j->save_record && bc_history_finish(j->root, j->record, end, &save_error))
            json_object_object_add(end, "history_error", json_object_new_string(save_error.text));
        else if (j->save_record)
            json_object_object_add(end, "history_id",
                                   json_object_new_string(bc_str(j->record, "id")));
        if (!j->save_record)
            bc_record_finish(j->record, end);
        json_object_object_add(end, "record", bc_clone(j->record));
        json_object_put(j->record);
        j->record = NULL;
    }
    emit(j, end);
    atomic_store(&j->finished, 1);
    notify(j);
    return NULL;
}

bc_job *bc_job_start(const char *op, const char *root, const char *id, const char *path,
                     const char *extra, json_object *values) {
    bc_job *j = calloc(1, sizeof *j);
    if (!j)
        return NULL;
#ifdef _WIN32
    j->notify_fd[0] = j->notify_fd[1] = -1;
#else
    if (pipe(j->notify_fd) < 0) {
        free(j);
        return NULL;
    }
    for (int i = 0; i < 2; i++) {
        if (fcntl(j->notify_fd[i], F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(j->notify_fd[i], F_SETFL, O_NONBLOCK) < 0) {
            close(j->notify_fd[0]);
            close(j->notify_fd[1]);
            free(j);
            return NULL;
        }
    }
#endif
    j->operation = strdup(op);
    j->root = strdup(root);
    j->id = strdup(id ? id : "");
    j->path = strdup(path ? path : "");
    j->extra = strdup(extra ? extra : "");
    j->values = values ? bc_clone(values) : json_object_new_object();
    pthread_mutex_init(&j->mutex, NULL);
    if (!j->operation || !j->root || !j->id || !j->path || !j->extra || !j->values ||
        pthread_create(&j->thread, NULL, work, j) != 0) {
        free(j->operation);
        free(j->root);
        free(j->id);
        free(j->path);
        free(j->extra);
        if (j->values)
            json_object_put(j->values);
        close(j->notify_fd[0]);
        close(j->notify_fd[1]);
        pthread_mutex_destroy(&j->mutex);
        free(j);
        return NULL;
    }
    return j;
}

json_object *bc_job_next(bc_job *j) {
    pthread_mutex_lock(&j->mutex);
    event *ev = j->first;
    if (ev) {
        j->first = ev->next;
        if (!j->first)
            j->last = NULL;
        j->queued--;
    }
    pthread_mutex_unlock(&j->mutex);
    if (!ev)
        return NULL;
    json_object *o = ev->o;
    free(ev);
    return o;
}

int bc_job_notification_fd(bc_job *j) {
    return j ? j->notify_fd[0] : -1;
}

void bc_job_acknowledge(bc_job *j) {
#ifndef _WIN32
    unsigned char bytes[256];
    for (;;) {
        ssize_t n = read(j->notify_fd[0], bytes, sizeof bytes);
        if (n > 0)
            continue;
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
#else
    (void)j;
#endif
}

int bc_job_finished(bc_job *j) {
    return atomic_load(&j->finished);
}

void bc_job_cancel(bc_job *j) {
    if (j)
        atomic_store(&j->stop, 1);
}

void bc_job_reply(bc_job *j, const char *id, int accepted) {
    pthread_mutex_lock(&j->mutex);
    snprintf(j->reply_id, sizeof j->reply_id, "%s", id);
    j->reply = accepted;
    j->reply_has_value = 0;
    j->reply_ready = 1;
    pthread_mutex_unlock(&j->mutex);
}

void bc_job_reply_value(bc_job *j, const char *id, int accepted, double value) {
    bc_job_reply_acquired(j, id, accepted, value, NULL);
}

void bc_job_reply_acquired(bc_job *j, const char *id, int accepted, double value,
                           json_object *acquisition) {
    pthread_mutex_lock(&j->mutex);
    snprintf(j->reply_id, sizeof j->reply_id, "%s", id);
    if (j->reply_acquisition)
        json_object_put(j->reply_acquisition);
    j->reply_acquisition = bc_clone(acquisition);
    j->reply = accepted;
    j->reply_has_value = 1;
    j->reply_value = value;
    j->reply_ready = 1;
    pthread_mutex_unlock(&j->mutex);
}

int bc_job_command(bc_job *j, json_object *command) {
    if (!j || !command || atomic_load(&j->stop) || atomic_load(&j->finished) ||
        (strcmp(j->operation, "run") && strcmp(j->operation, "communication_terminal")))
        return -1;
    json_object *copy = bc_clone(command);
    if (!copy)
        return -1;
    pthread_mutex_lock(&j->mutex);
    int ready = j->command == NULL;
    if (ready)
        j->command = copy;
    pthread_mutex_unlock(&j->mutex);
    if (!ready)
        json_object_put(copy);
    return ready ? 0 : -1;
}

void bc_job_free(bc_job *j) {
    if (!j)
        return;
    bc_job_cancel(j);
    pthread_join(j->thread, NULL);
    json_object *ev;
    while ((ev = bc_job_next(j)))
        json_object_put(ev);
    if (j->command)
        json_object_put(j->command);
    if (j->reply_acquisition)
        json_object_put(j->reply_acquisition);
    json_object_put(j->values);
    free(j->operation);
    free(j->root);
    free(j->id);
    free(j->path);
    free(j->extra);
    close(j->notify_fd[0]);
    close(j->notify_fd[1]);
    pthread_mutex_destroy(&j->mutex);
    free(j);
}
