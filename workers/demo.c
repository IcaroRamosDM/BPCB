#define _POSIX_C_SOURCE 200809L
#include "worker_api.h"
#include <math.h>
#ifdef _WIN32
#undef isfinite
#define isfinite __builtin_isfinite
#endif
#include <time.h>

int main(void) {
    json_object *q = worker_begin();
    if (!q)
        return 1;
    json_object *p = bc_get(q, "parameters");
    double r = 0, v = bc_num(p, "voltage"), duration = bc_num(p, "delay");
    if (bc_component(bc_str(bc_get(q, "components"), "R1"), &r) < 0 || r <= 0 || !isfinite(v) ||
        duration < 0 || duration > 600) {
        worker_event("error", "Parâmetro de simulação inválido.");
        json_object_put(q);
        return 1;
    }
    worker_event("status", "Calculando no núcleo C. Nenhum acesso à PCB.");
    for (double elapsed = 0; elapsed < duration && !worker_stopped; elapsed += 0.01) {
        struct timespec ts = {0, 10000000};
        nanosleep(&ts, NULL);
    }
    if (worker_stopped) {
        json_object_put(q);
        return 130;
    }
    worker_measure("Corrente calculada", v / r * 1000, "mA", "Simulação");
    worker_event("done", "Simulação concluída. Nenhuma medição física foi realizada.");
    json_object_put(q);
    return 0;
}
