#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#ifdef _WIN32
#undef isfinite
#define isfinite __builtin_isfinite
#endif
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <poll.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <glib.h>

#ifdef _WIN32
typedef SOCKET instrument_fd;
#else
typedef int instrument_fd;
#endif
typedef struct {
    instrument_fd fd;
    int socket;
    double deadline;
    int (*cancel)(void *);
    void *context;
    bc_error *error;
} instrument_link;

static double instant(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int ready(instrument_link *c, short events) {
    while (instant() < c->deadline) {
        if (c->cancel && c->cancel(c->context))
            return bc_fail(c->error, "Leitura do Rigol cancelada.");
#ifdef _WIN32
        WSAPOLLFD p = {c->fd, events, 0};
        int n = WSAPoll(&p, 1, 50);
#else
        struct pollfd p = {c->fd, events, 0};
        int n = poll(&p, 1, 50);
#endif
        if (n > 0) {
            if (p.revents & events)
                return 0;
            return bc_fail(c->error, "Conexão com o Rigol encerrada.");
        }
        if (n < 0 && errno != EINTR)
            return bc_fail(c->error, "Comunicação com o Rigol: %s", strerror(errno));
    }
    return bc_fail(c->error, "Tempo limite de comunicação com o Rigol.");
}

static int connect_instrument(instrument_link *c, const char *endpoint) {
    if (!strncmp(endpoint, "tcp://", 6)) {
        char host[256], port[8];
        const char *start = endpoint + 6, *colon = strrchr(start, ':');
        if (!colon || colon == start || (size_t)(colon - start) >= sizeof host ||
            strlen(colon + 1) >= sizeof port)
            return bc_fail(c->error, "Use tcp://IP:porta, com o IP numérico do Rigol.");
        memcpy(host, start, (size_t)(colon - start));
        host[colon - start] = 0;
        strcpy(port, colon + 1);
        if (host[0] == '[' && host[strlen(host) - 1] == ']') {
            memmove(host, host + 1, strlen(host));
            host[strlen(host) - 1] = 0;
        }
        char *end = NULL;
        long p = strtol(port, &end, 10);
        if (!*port || *end || p < 1 || p > 65535)
            return bc_fail(c->error, "Porta TCP inválida.");
        struct addrinfo hints = {0}, *address = NULL;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
        if (getaddrinfo(host, port, &hints, &address))
            return bc_fail(c->error, "Endereço IP inválido.");
        c->fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        c->socket = 1;
        if (c->fd == (instrument_fd)-1) {
            freeaddrinfo(address);
            return bc_fail(c->error, "Abrir socket do Rigol.");
        }
#ifdef _WIN32
        u_long nonblocking = 1;
        ioctlsocket(c->fd, (long)FIONBIO, &nonblocking);
#else
        fcntl(c->fd, F_SETFL, O_NONBLOCK);
        fcntl(c->fd, F_SETFD, FD_CLOEXEC);
#endif
        int rc = connect(c->fd, address->ai_addr, (socklen_t)address->ai_addrlen);
        freeaddrinfo(address);
        if (rc < 0 &&
#ifdef _WIN32
            WSAGetLastError() != WSAEWOULDBLOCK
#else
            errno != EINPROGRESS
#endif
        )
            return bc_fail(c->error, "Conectar ao Rigol: %s", strerror(errno));
        if (rc < 0) {
            if (ready(c, POLLOUT))
                return -1;
            int error = 0;
            socklen_t len = sizeof error;
            if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (void *)&error, &len) || error)
                return bc_fail(c->error, "Rigol indisponível na porta informada.");
        }
        return 0;
    }
#ifndef _WIN32
    const char *prefix = "usbtmc:/dev/usbtmc";
    if (!strncmp(endpoint, prefix, strlen(prefix))) {
        const char *n = endpoint + strlen(prefix);
        if (!*n)
            return bc_fail(c->error, "Informe o dispositivo USBTMC.");
        for (const char *p = n; *p; p++)
            if (!isdigit((unsigned char)*p))
                return bc_fail(c->error, "Dispositivo USBTMC inválido.");
        c->fd = open(endpoint + 7, O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        struct stat st;
        if (c->fd < 0 || fstat(c->fd, &st) || !S_ISCHR(st.st_mode))
            return bc_fail(c->error, "Abrir USBTMC: confira conexão e permissão do dispositivo.");
        return 0;
    }
#endif
    return bc_fail(c->error, "Use tcp://IP:porta. USBTMC direto está disponível somente no Linux.");
}

/* Fixed, read-only SCPI queries. No arbitrary commands or acquisition settings. */
static int query(instrument_link *c, const char *command, char *out, size_t cap) {
    size_t used = 0, n = strlen(command);
    while (used < n) {
        if (ready(c, POLLOUT))
            return -1;
#ifdef _WIN32
        int k = send(c->fd, command + used, (int)(n - used), 0);
        if (k < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
#else
        ssize_t k = c->socket ? send(c->fd, command + used, n - used, MSG_NOSIGNAL)
                              : write(c->fd, command + used, n - used);
        if (k < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
#endif
        if (k > 0)
            used += (size_t)k;
        else
            return bc_fail(c->error, "Falha ao consultar o Rigol.");
    }
    used = 0;
    while (used + 1 < cap) {
        if (ready(c, POLLIN))
            return -1;
#ifdef _WIN32
        int k = recv(c->fd, out + used, (int)(cap - 1 - used), 0);
        if (k < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
#else
        ssize_t k = read(c->fd, out + used, cap - 1 - used);
        if (k < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
#endif
        if (k <= 0)
            return bc_fail(c->error, "Resposta incompleta do Rigol.");
        used += (size_t)k;
        out[used] = 0;
        char *line = memchr(out, '\n', used);
        if (line) {
            *line = 0;
            size_t end = (size_t)(line - out);
            while (end && isspace((unsigned char)out[end - 1]))
                out[--end] = 0;
            return 0;
        }
    }
    return bc_fail(c->error, "Resposta do Rigol maior que o permitido.");
}

static int require_value(instrument_link *c, const char *command, const char *expected,
                         const char *message) {
    char s[256];
    if (query(c, command, s, sizeof s))
        return -1;
    return strcmp(s, expected) ? bc_fail(c->error, "%s", message) : 0;
}

static int number(instrument_link *c, const char *command, double *value) {
    char s[256], *end;
    if (query(c, command, s, sizeof s))
        return -1;
    errno = 0;
    *value = strtod(s, &end);
    if (!*s || end == s || *end || errno || !isfinite(*value) || fabs(*value) > 1e30)
        return bc_fail(c->error, "O Rigol não forneceu uma medição válida.");
    return 0;
}

static json_object *channel_details(instrument_link *c, unsigned channel) {
    json_object *o = json_object_new_object();
    char command[100], answer[256];
    json_object_object_add(o, "channel", json_object_new_int((int)channel));
    const char *queries[] = {"DISPlay", "UNITs", "COUPling", "PROBe", "SCALe", "OFFSet"};
    const char *keys[] = {"display", "unit", "coupling", "probe_ratio", "scale", "offset"};
    for (unsigned i = 0; i < 6; i++) {
        snprintf(command, sizeof command, ":CHANnel%u:%s?\n", channel, queries[i]);
        if (i < 3) {
            if (query(c, command, answer, sizeof answer))
                goto fail;
            json_object_object_add(o, keys[i], json_object_new_string(answer));
        } else {
            double value;
            if (number(c, command, &value))
                goto fail;
            json_object_object_add(o, keys[i], json_object_new_double(value));
        }
    }
    const char *metrics[] = {"VAVG", "VPP", "VMIN", "VMAX"};
    for (unsigned i = 0; i < 4; i++) {
        snprintf(command, sizeof command, ":MEASure:ITEM? %s,CHANnel%u\n", metrics[i], channel);
        if (query(c, command, answer, sizeof answer))
            goto fail;
        errno = 0;
        char *end;
        double value = strtod(answer, &end);
        int valid =
            *answer && end != answer && !*end && !errno && isfinite(value) && fabs(value) < 1e30;
        json_object_object_add(o, metrics[i], valid ? json_object_new_double(value) : NULL);
    }
    return o;
fail:
    json_object_put(o);
    return NULL;
}

json_object *bc_rigol_read(const char *endpoint, const char *field, int (*cancel)(void *),
                           void *context, bc_error *e) {
    instrument_link c = {.fd = (instrument_fd)-1,
                         .deadline = instant() + 5,
                         .cancel = cancel,
                         .context = context,
                         .error = e};
    json_object *result = NULL;
    char idn[512];
    double value = 0;
    int diagnostic = !strcmp(field, "diagnostic"), channel = 1, used = 0, generic = 0;
    char metric[16] = "", command[100], expected[16];
    if (sscanf(field, "read:%d:%15[A-Z]%n", &channel, metric, &used) == 2 && field[used] == 0 &&
        (channel == 1 || channel == 2)) {
        const char *allowed[] = {"VAVG", "VPP", "VMIN", "VMAX", "VTOP", "TIME"};
        for (unsigned i = 0; i < 6; i++)
            if (!strcmp(metric, allowed[i]))
                generic = 1;
    }
    if (!generic)
        channel = 1;
    if (diagnostic)
        c.deadline = instant() + 10;
    if (strcmp(field, "identify") && strcmp(field, "cap_inicial") && strcmp(field, "cap_final") &&
        strcmp(field, "cap_tempo") && !diagnostic && !generic) {
        bc_fail(e, "Leitura Rigol desconhecida.");
        return NULL;
    }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
        bc_fail(e, "Não foi possível iniciar a rede Windows.");
        return NULL;
    }
#endif
    if (connect_instrument(&c, endpoint) || query(&c, "*IDN?\n", idn, sizeof idn))
        goto done;
    if (!strstr(idn, "RIGOL") || (!strstr(idn, ",DHO8") && !strstr(idn, ",DHO9"))) {
        bc_fail(e, "O instrumento não foi identificado como Rigol DHO800/900.");
        goto done;
    }
    if (diagnostic) {
        char state[128];
        if (query(&c, ":TRIGger:STATus?\n", state, sizeof state))
            goto done;
        result = json_object_new_object();
        json_object_object_add(result, "capture_state", json_object_new_string(state));
        json_object *channels = json_object_new_array();
        json_object_object_add(result, "channels", channels);
        for (unsigned i = 1; i <= 2; i++) {
            json_object *ch = channel_details(&c, i);
            if (!ch) {
                json_object_put(result);
                result = NULL;
                goto done;
            }
            json_object_array_add(channels, ch);
        }
        goto describe;
    }
    if (generic) {
        snprintf(command, sizeof command, ":CHANnel%d:UNITs?\n", channel);
        if (require_value(&c, command, "VOLT",
                          "O canal escolhido precisa usar a unidade de tensão (V)."))
            goto done;
        if (strcmp(metric, "VAVG") && require_value(&c, ":TRIGger:STATus?\n", "STOP",
                                                    "Congele a captura antes de ler esta medida."))
            goto done;
        if (!strcmp(metric, "TIME")) {
            double a, b;
            snprintf(expected, sizeof expected, "CHAN%d", channel);
            if (require_value(&c, ":CURSor:MODE?\n", "MAN", "Selecione cursores manuais.") ||
                require_value(&c, ":CURSor:MANual:TYPE?\n", "TIME",
                              "Selecione cursores de tempo.") ||
                require_value(&c, ":CURSor:MANual:SOURce?\n", expected,
                              "A fonte dos cursores deve ser o canal escolhido na etapa.") ||
                number(&c, ":CURSor:MANual:CAX?\n", &a) || number(&c, ":CURSor:MANual:CBX?\n", &b))
                goto done;
            value = (b - a) * 1000;
        } else {
            snprintf(command, sizeof command, ":MEASure:ITEM? %s,CHANnel%d\n", metric, channel);
            if (number(&c, command, &value))
                goto done;
        }
    } else if (strcmp(field, "identify")) {
        if (require_value(&c, ":CHANnel1:UNITs?\n", "VOLT",
                          "CH1 precisa usar a unidade de tensão (V)."))
            goto done;
        if (strcmp(field, "cap_inicial") &&
            require_value(&c, ":TRIGger:STATus?\n", "STOP",
                          "Congele a captura do ensaio no Rigol antes de ler."))
            goto done;
        if (!strcmp(field, "cap_tempo")) {
            double a, b;
            if (require_value(&c, ":CURSor:MODE?\n", "MAN",
                              "Selecione os cursores manuais no Rigol.") ||
                require_value(&c, ":CURSor:MANual:TYPE?\n", "TIME",
                              "Selecione cursores de tempo no Rigol.") ||
                require_value(&c, ":CURSor:MANual:SOURce?\n", "CHAN1",
                              "Selecione CH1 como fonte dos cursores.") ||
                number(&c, ":CURSor:MANual:CAX?\n", &a) || number(&c, ":CURSor:MANual:CBX?\n", &b))
                goto done;
            value = (b - a) * 1000;
            if (value <= 0 || value > 2000) {
                bc_fail(
                    e,
                    "Posicione A no início da carga e B na queda de CAP; B deve vir depois de A.");
                goto done;
            }
        } else if (number(&c,
                          !strcmp(field, "cap_final") ? ":MEASure:ITEM? VTOP,CHANnel1\n"
                                                      : ":MEASure:ITEM? VAVG,CHANnel1\n",
                          &value))
            goto done;
        if (!strcmp(field, "cap_inicial") && (value < 0 || value > 0.1)) {
            bc_fail(e,
                    "Média inicial: %.6g mV. Faixa: 0 a 100 mV (0,1 V). Confira CH1 antes da "
                    "carga; Vpp/pico não é a tensão inicial média.",
                    value * 1000);
            goto done;
        }
        if (!strcmp(field, "cap_final") && (value <= 0 || value > 5.5)) {
            bc_fail(e, "Patamar do Rigol fora da faixa de 0 a 5,5 V.");
            goto done;
        }
    }
    result = json_object_new_object();
describe:
    json_object_object_add(result, "instrument", json_object_new_string(idn));
    json_object_object_add(result, "endpoint", json_object_new_string(endpoint));
    json_object_object_add(result, "field", json_object_new_string(field));
    GDateTime *date = g_date_time_new_now_utc();
    char *stamp = g_date_time_format_iso8601(date);
    json_object_object_add(result, "queried_at", json_object_new_string(stamp));
    g_free(stamp);
    g_date_time_unref(date);
    if (strcmp(field, "identify") && !diagnostic) {
        json_object_object_add(result, "value", json_object_new_double(value));
        json_object_object_add(result, "unit",
                               json_object_new_string((!strcmp(field, "cap_tempo") ||
                                                       (generic && !strcmp(metric, "TIME")))
                                                          ? "ms"
                                                          : "V"));
        json_object_object_add(
            result, "measurement",
            json_object_new_string(generic
                                       ? (!strcmp(metric, "TIME") ? "CURSOR_DELTA_TIME" : metric)
                                   : !strcmp(field, "cap_inicial") ? "VAVG"
                                   : !strcmp(field, "cap_final")   ? "VTOP"
                                                                   : "CURSOR_DELTA_TIME"));
        json_object_object_add(result, "channel", json_object_new_int(channel));
    }
done:
#ifdef _WIN32
    if (c.fd != (instrument_fd)-1)
        closesocket(c.fd);
    WSACleanup();
#else
    if (c.fd >= 0)
        close(c.fd);
#endif
    return result;
}
