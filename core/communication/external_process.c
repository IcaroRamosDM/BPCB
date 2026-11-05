#define _POSIX_C_SOURCE 200809L
#include "../lab.h"
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#else
#include <io.h>
#endif
#define LOG_LIMIT 65536
#ifndef _WIN32
static void child_setup(void *unused) {
    (void)unused;
    if (setsid() < 0)
        _exit(127);
}

static void *reap_later(void *context) {
    pid_t pid = *(pid_t *)context;
    free(context);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
    return NULL;
}

static void reap(pid_t pid) {
    double until = bc_comm_now() + 2;
    for (;;) {
        pid_t r = waitpid(pid, NULL, WNOHANG);
        if (r == pid || (r < 0 && errno != EINTR))
            return;
        if (bc_comm_now() > until)
            break;
        struct timespec delay = {0, 10000000};
        nanosleep(&delay, NULL);
    }
    pid_t *copy = malloc(sizeof *copy);
    if (copy) {
        *copy = pid;
        pthread_t thread;
        if (!pthread_create(&thread, NULL, reap_later, copy)) {
            pthread_detach(thread);
            return;
        }
        free(copy);
    }
}
#else
/* Microsoft CRT argv quoting. No command interpreter is involved. */
static wchar_t *command_line(char **argv, size_t count) {
    GString *s = g_string_new(NULL);
    for (size_t i = 0; i < count; i++) {
        if (i)
            g_string_append_c(s, ' ');
        g_string_append_c(s, '"');
        unsigned slashes = 0;
        for (const char *p = argv[i]; *p; p++) {
            if (*p == '\\') {
                slashes++;
                continue;
            }
            unsigned n = *p == '"' ? slashes * 2 + 1 : slashes;
            for (unsigned j = 0; j < n; j++)
                g_string_append_c(s, '\\');
            g_string_append_c(s, *p);
            slashes = 0;
        }
        for (unsigned j = 0; j < slashes * 2; j++)
            g_string_append_c(s, '\\');
        g_string_append_c(s, '"');
    }
    wchar_t *result = (wchar_t *)g_utf8_to_utf16(s->str, -1, NULL, NULL, NULL);
    g_string_free(s, TRUE);
    return result;
}

static int spawn_windows(char **argv, size_t count, const char *cwd, GPid *pid, HANDLE *job,
                         int *out, int *err, bc_error *e) {
    SECURITY_ATTRIBUTES security = {sizeof security, NULL, TRUE};
    HANDLE stdout_read = NULL, stdout_write = NULL, stderr_read = NULL, stderr_write = NULL,
           input = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process = {0};
    STARTUPINFOEXW startup = {0};
    SIZE_T attribute_bytes = 0;
    int ok = 0, attributes_ready = 0;
    *job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!*job ||
        !SetInformationJobObject(*job, JobObjectExtendedLimitInformation, &limits, sizeof limits) ||
        !CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &security, 0) ||
        !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0))
        goto done;
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                        OPEN_EXISTING, 0, NULL);
    if (input == INVALID_HANDLE_VALUE)
        goto done;
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = stdout_write;
    startup.StartupInfo.hStdError = stderr_write;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    startup.lpAttributeList = g_malloc0(attribute_bytes);
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes))
        goto done;
    attributes_ready = 1;
    HANDLE inherited[] = {input, stdout_write, stderr_write};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited, sizeof inherited, NULL, NULL))
        goto done;
    wchar_t *line = command_line(argv, count),
            *directory = (wchar_t *)g_utf8_to_utf16(cwd, -1, NULL, NULL, NULL),
            *exe = (wchar_t *)g_utf8_to_utf16(argv[0], -1, NULL, NULL, NULL);
    DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                  EXTENDED_STARTUPINFO_PRESENT;
    int created = line && directory && exe &&
                  CreateProcessW(exe, line, NULL, NULL, TRUE, flags, NULL, directory,
                                 &startup.StartupInfo, &process);
    g_free(exe);
    g_free(directory);
    g_free(line);
    if (!created)
        goto done;
    /* The child is suspended until containment is established, including descendants. */
    if (!AssignProcessToJobObject(*job, process.hProcess) ||
        ResumeThread(process.hThread) == (DWORD)-1) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
        goto done;
    }
    CloseHandle(process.hThread);
    process.hThread = NULL;
    *out = _open_osfhandle((intptr_t)stdout_read, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (*out < 0)
        goto done;
    stdout_read = NULL;
    *err = _open_osfhandle((intptr_t)stderr_read, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (*err < 0)
        goto done;
    stderr_read = NULL;
    *pid = process.hProcess;
    process.hProcess = NULL;
    ok = 1;
done:
    if (startup.lpAttributeList) {
        if (attributes_ready)
            DeleteProcThreadAttributeList(startup.lpAttributeList);
        g_free(startup.lpAttributeList);
    }
    if (input != INVALID_HANDLE_VALUE)
        CloseHandle(input);
    if (stdout_write)
        CloseHandle(stdout_write);
    if (stderr_write)
        CloseHandle(stderr_write);
    if (stdout_read)
        CloseHandle(stdout_read);
    if (stderr_read)
        CloseHandle(stderr_read);
    if (process.hThread)
        CloseHandle(process.hThread);
    if (process.hProcess) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hProcess);
    }
    if (!ok) {
        if (*job) {
            TerminateJobObject(*job, 1);
            CloseHandle(*job);
            *job = NULL;
        }
        if (*out >= 0) {
            close(*out);
            *out = -1;
        }
        if (*err >= 0) {
            close(*err);
            *err = -1;
        }
        bc_fail(e, "Não foi possível iniciar/isolar a ferramenta Windows (erro %lu).",
                (unsigned long)GetLastError());
    }
    return ok;
}
#endif
json_object *bc_external_process(json_object *args, int seconds, int (*cancel)(void *), void *ctx,
                                 bc_error *e) {
    if (!json_object_is_type(args, json_type_array) || !json_object_array_length(args) ||
        json_object_array_length(args) > 128 || seconds < 1 || seconds > 600)
        return bc_fail(e, "Argumentos/prazo de processo externo inválidos."), NULL;
    size_t count = json_object_array_length(args);
    char **argv = calloc(count + 1, sizeof *argv);
    if (!argv)
        return bc_fail(e, "Memória insuficiente."), NULL;
    for (size_t i = 0; i < count; i++) {
        json_object *value = json_object_array_get_idx(args, i);
        if (!json_object_is_type(value, json_type_string) ||
            json_object_get_string_len(value) > 32768 ||
            (size_t)json_object_get_string_len(value) != strlen(json_object_get_string(value))) {
            free(argv);
            bc_fail(e, "Argumento de processo inválido.");
            return NULL;
        }
        argv[i] = (char *)json_object_get_string(value);
    }
    if (!g_path_is_absolute(argv[0])) {
        free(argv);
        bc_fail(e, "Executável absoluto obrigatório.");
        return NULL;
    }
    GError *error = NULL;
    char *cwd = g_dir_make_tmp("bpcb-process-XXXXXX", &error);
    GPid pid = 0;
    int out = -1, err = -1, ok = 0;
#ifdef _WIN32
    HANDLE group = NULL;
    if (cwd)
        ok = spawn_windows(argv, count, cwd, &pid, &group, &out, &err, e);
#else
    if (cwd)
        ok = g_spawn_async_with_pipes(cwd, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, child_setup, NULL,
                                      &pid, NULL, &out, &err, &error);
#endif
    free(argv);
    if (!ok) {
        if (!e->text[0])
            bc_fail(e, "Iniciar ferramenta externa: %s", error ? error->message : "falha");
        g_clear_error(&error);
        if (cwd) {
            rmdir(cwd);
            g_free(cwd);
        }
        return NULL;
    }
#ifndef _WIN32
    fcntl(out, F_SETFL, O_NONBLOCK);
    fcntl(err, F_SETFL, O_NONBLOCK);
#endif
    char *log = calloc(LOG_LIMIT + 1, 1);
    size_t used = 0;
    int done = 0, failed = 0, eof[2] = {0, 0};
    int64_t status = 1;
    double deadline = bc_comm_now() + seconds, exited_at = 0;
    if (!log) {
        bc_fail(e, "Memória insuficiente para log.");
        failed = 1;
    }
    while (!failed && (!done || !eof[0] || !eof[1])) {
        if ((cancel && cancel(ctx)) || bc_comm_now() > deadline) {
            bc_fail(
                e,
                cancel && cancel(ctx)
                    ? "Ferramenta externa cancelada; estado físico não confirmado."
                    : "Ferramenta externa excedeu o tempo limite; estado físico não confirmado.");
            failed = 1;
            break;
        }
        int fds[] = {out, err};
        for (unsigned i = 0; i < 2; i++)
            if (!eof[i]) {
                char buffer[2048];
                int n;
#ifdef _WIN32
                DWORD available = 0;
                HANDLE handle = (HANDLE)_get_osfhandle(fds[i]);
                if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) {
                    eof[i] = 1;
                    continue;
                }
                if (!available)
                    continue;
#endif
                n = (int)read(fds[i], buffer, sizeof buffer);
                if (n == 0) {
                    eof[i] = 1;
                    continue;
                }
                if (n < 0) {
                    if (errno == EAGAIN || errno == EINTR)
                        continue;
                    bc_fail(e, "Falha lendo ferramenta externa.");
                    failed = 1;
                    break;
                }
                if (used + (size_t)n > LOG_LIMIT) {
                    bc_fail(e, "Ferramenta externa excedeu o limite de 64 KiB de log.");
                    failed = 1;
                    break;
                }
                memcpy(log + used, buffer, (size_t)n);
                used += (size_t)n;
            }
        if (failed)
            break;
        if (!done) {
#ifdef _WIN32
            if (WaitForSingleObject(pid, 0) == WAIT_OBJECT_0) {
                DWORD code = 1;
                GetExitCodeProcess(pid, &code);
                status = (int64_t)code;
                done = 1;
                exited_at = bc_comm_now();
            }
#else
            siginfo_t info = {0};
            int waited = waitid(P_PID, (id_t)pid, &info, WEXITED | WNOHANG | WNOWAIT);
            if (!waited && info.si_pid == pid) {
                status = info.si_code == CLD_EXITED ? info.si_status : 1;
                done = 1;
                exited_at = bc_comm_now();
            } else if (waited < 0 && errno != EINTR) {
                failed = 1;
                bc_fail(e, "Falha aguardando ferramenta externa.");
                break;
            }
#endif
        }
        if (done && bc_comm_now() - exited_at > 0.5)
            break;
        struct timespec delay = {0, 10000000};
        nanosleep(&delay, NULL);
    }
    /* Kill the entire group even after a successful parent exit. On POSIX the direct
       child remains unreaped until this point, so its PID cannot have been recycled. */
#ifdef _WIN32
    TerminateJobObject(group, 1);
    WaitForSingleObject(pid, 2000);
    CloseHandle(group);
    CloseHandle(pid);
#else
    kill(-pid, SIGKILL);
    if (!done)
        kill(pid, SIGKILL);
    reap(pid);
    g_spawn_close_pid(pid);
#endif
    close(out);
    close(err);
    rmdir(cwd);
    g_free(cwd);
    if (failed) {
        free(log);
        return NULL;
    }
    json_object *result = json_object_new_object();
    char *utf = g_utf8_make_valid(log, (gssize)used);
    json_object_object_add(result, "log", json_object_new_string(utf));
    g_free(utf);
    free(log);
    json_object_object_add(result, "exit_code", json_object_new_int64(status));
    json_object_object_add(result, "ok", json_object_new_boolean(!status));
    return result;
}
