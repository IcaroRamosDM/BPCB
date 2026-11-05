#define _POSIX_C_SOURCE 200809L
#include "bancada.h"
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int bc_export_json(json_object *value, const char *destination, bc_error *e) {
    if (!json_object_is_type(value, json_type_object))
        return bc_fail(e, "Não há prévia válida para exportar.");
    const char *data = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PRETTY);
    size_t size = strlen(data), written = 0;
    if (size > 8u * 1024u * 1024u)
        return bc_fail(e, "Diagnóstico excede 8 MiB.");
    int fd = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return bc_fail(e, "Escolha um arquivo novo: %s.", strerror(errno));
    int ok = 1;
    while (written < size) {
        ssize_t n = write(fd, data + written, size - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = 0;
            break;
        }
        written += (size_t)n;
    }
    if (fsync(fd))
        ok = 0;
    if (close(fd))
        ok = 0;
    if (!ok) {
        unlink(destination);
        return bc_fail(e, "Falha ao exportar o diagnóstico.");
    }
    return 0;
}

json_object *bc_diagnostic(const char *root, const char *board, json_object *context, bc_error *e) {
    json_object *out = json_object_new_object();
    json_object_object_add(out, "format", json_object_new_string("bpcb.diagnostic"));
    json_object_object_add(out, "schema", json_object_new_int(1));
    json_object_object_add(out, "bpcb_version", json_object_new_string(BC_VERSION));
    GDateTime *date = g_date_time_new_now_utc();
    char *stamp = g_date_time_format_iso8601(date);
    json_object_object_add(out, "created_at", json_object_new_string(stamp));
    g_free(stamp);
    g_date_time_unref(date);
    char *os = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME);
    json_object_object_add(out, "system",
                           json_object_new_string(os ? os :
#ifdef _WIN32
                                                     "Windows"
#else
                                                     "Linux"
#endif
                                                  ));
    g_free(os);
    json_object_object_add(out, "pointer_bits", json_object_new_int((int)(sizeof(void *) * 8)));
    if (*board) {
        char folder[4096];
        json_object *profile = bc_profile(root, board, folder, e);
        if (!profile) {
            json_object_put(out);
            return NULL;
        }
        json_object *info = json_object_new_object();
        const char *keys[] = {"id",      "pcb",       "revision", "version",
                              "min_app", "platforms", "min_glibc"};
        for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
            if (bc_get(profile, keys[i]))
                json_object_object_add(info, keys[i], bc_clone(bc_get(profile, keys[i])));
        json_object_object_add(out, "package", info);
        json_object_put(profile);
    }
    json_object_object_add(
        out, "history_enabled",
        json_object_new_boolean(json_object_get_boolean(bc_get(context, "history_enabled"))));
    json_object *messages = bc_get(context, "messages");
    if (messages && (!json_object_is_type(messages, json_type_array) ||
                     json_object_array_length(messages) > 100)) {
        json_object_put(out);
        bc_fail(e, "Muitas mensagens no diagnóstico.");
        return NULL;
    }
    json_object_object_add(out, "messages",
                           messages ? bc_clone(messages) : json_object_new_array());
    if (json_object_get_boolean(bc_get(context, "include_instrument")) &&
        bc_get(context, "instrument"))
        json_object_object_add(out, "instrument", bc_clone(bc_get(context, "instrument")));
    json_object_object_add(
        out, "scope",
        json_object_new_string(
            "Prévia solicitada pelo operador. Mensagens podem conter caminhos locais. Dados do "
            "instrumento são incluídos somente se selecionados. Sem coleta de documentos, "
            "credenciais ou histórico de ensaios; sem envio pela rede."));
    if (strlen(json_object_to_json_string_ext(out, JSON_C_TO_STRING_PLAIN)) > 1024u * 1024u) {
        json_object_put(out);
        bc_fail(e, "Prévia de diagnóstico excede 1 MiB.");
        return NULL;
    }
    return out;
}
