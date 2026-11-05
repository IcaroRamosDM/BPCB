#define _POSIX_C_SOURCE 200809L
#include "communication.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "OPENOCD FAIL %d: %s (%s)\n", __LINE__, #x, e.text);                   \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static int devices = 1;

json_object *__wrap_bc_comm_list(int (*cancel)(void *), void *ctx, bc_error *e) {
    (void)cancel;
    (void)ctx;
    (void)e;
    json_object *a = json_object_new_array();
    for (int i = 0; i < devices; i++)
        json_object_array_add(
            a, json_tokener_parse(
                   "{\"adapter\":\"ch347\",\"mode\":3,\"bus\":\"jtag\",\"serial\":\"TEST\"}"));
    return a;
}

static void str(json_object *o, const char *k, const char *v) {
    json_object_object_add(o, k, json_object_new_string(v));
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int cancelled(void *ctx) {
    return now() - *(double *)ctx > 0.15;
}

int main(void) {
    bc_error e = {{0}};
    char root[] = "/tmp/bpcb-openocd-test-XXXXXX";
    CHECK(mkdtemp(root));
    char exe[4096], file[4096], dir[4096], cfg[4096];
    snprintf(exe, sizeof exe, "%s/openocd fake", root);
    snprintf(file, sizeof file, "%s/firmware ; [exec nope] $.bin", root);
    snprintf(dir, sizeof dir, "%s/target", root);
    CHECK(mkdir(dir, 0700) == 0);
    CHECK(bc_path(cfg, sizeof cfg, dir, "stm32f1x.cfg", &e) == 0);
    FILE *f = fopen(cfg, "w");
    CHECK(f);
    fputs("# fixture, never evaluated\n", f);
    fclose(f);
    f = fopen(exe, "w");
    CHECK(f);
    fputs(
        "#!/usr/bin/python3\nimport sys,os,time,json,re,pathlib\ns=' "
        "'.join(sys.argv[1:])\nmode=os.getenv('BPCB_OPENOCD_TEST','ok')\nif 'adapter list' in s:\n "
        "print('ch347 ftdi')\n if mode=='mutate': "
        "pathlib.Path(os.environ['BPCB_OPENOCD_IMAGE']).write_bytes(b'changed')\nelse:\n if "
        "mode=='slow': time.sleep(30)\n if mode=='overflow': print('x'*70000)\n if mode=='fail': "
        "sys.exit(9)\n paths=[bytes.fromhex(h).decode() for h in re.findall(r'binary format H\\* "
        "([0-9a-f]+)',s)]\n assert len(paths)==1\n for p in paths:\n  assert 'bpcb-firmware-' in p "
        "and pathlib.Path(p).read_bytes()==bytes(range(64))\n print(json.dumps(sys.argv[1:]))\n",
        f);
    fclose(f);
    CHECK(chmod(exe, 0700) == 0);
    f = fopen(file, "wb");
    CHECK(f);
    for (unsigned i = 0; i < 64; i++)
        fputc((int)i, f);
    fclose(f);
    json_object *q = json_object_new_object();
    str(q, "executable", exe);
    str(q, "scripts", root);
    str(q, "action", "program");
    str(q, "target", "stm32f1x");
    str(q, "firmware", file);
    str(q, "serial", "TEST");
    json_object_object_add(q, "address", json_object_new_int(0x08000000));
    json_object_object_add(q, "timeout_seconds", json_object_new_int(2));
    json_object *plan = bc_openocd_plan(q, &e);
    CHECK(plan);
    char sha[65];
    snprintf(sha, sizeof sha, "%s", bc_str(plan, "sha256"));
    CHECK(strlen(sha) == 64);
    const char *args = json_object_to_json_string_ext(bc_get(plan, "argv"), JSON_C_TO_STRING_PLAIN);
    CHECK(!strstr(args, "[exec nope]") && !strstr(args, "firmware ;"));
    json_object_put(plan);
    CHECK(!bc_openocd_run(q, NULL, NULL, &e));
    json_object_object_add(q, "trusted_tools", json_object_new_boolean(1));
    CHECK(!bc_openocd_run(q, NULL, NULL, &e));
    str(q, "expected_sha256", sha);
    json_object_object_add(q, "program_confirmed", json_object_new_boolean(1));
    json_object *r = bc_openocd_run(q, NULL, NULL, &e);
    if (!r)
        fprintf(stderr, "%s\n", e.text);
    CHECK(r && json_object_get_boolean(bc_get(r, "ok")));
    CHECK(strstr(bc_str(r, "log"), "bpcb-firmware-") == NULL);
    json_object_put(r);
    devices = 0;
    CHECK(!bc_openocd_run(q, NULL, NULL, &e));
    devices = 2;
    CHECK(!bc_openocd_run(q, NULL, NULL, &e));
    devices = 1;
    setenv("BPCB_OPENOCD_TEST", "fail", 1);
    r = bc_openocd_run(q, NULL, NULL, &e);
    CHECK(r && !json_object_get_boolean(bc_get(r, "ok")) && bc_num(r, "exit_code") == 9);
    json_object_put(r);
    json_object *values = json_object_new_object();
    json_object_object_add(values, "operation", bc_clone(q));
    bc_job *job = bc_job_start("communication_openocd", root, "", "", "", values);
    json_object_put(values);
    CHECK(job);
    int finished = 0;
    double until = now() + 4;
    while (!finished && now() < until) {
        json_object *ev;
        while ((ev = bc_job_next(job))) {
            if (!strcmp(bc_str(ev, "type"), "finished")) {
                CHECK(!strcmp(bc_str(ev, "status"), "error") &&
                      bc_num(bc_get(ev, "data"), "exit_code") == 9);
                finished = 1;
            }
            json_object_put(ev);
        }
        struct timespec wait = {0, 1000000};
        nanosleep(&wait, NULL);
    }
    CHECK(finished);
    bc_job_free(job);
    setenv("BPCB_OPENOCD_TEST", "overflow", 1);
    CHECK(!bc_openocd_run(q, NULL, NULL, &e) && strstr(e.text, "64 KiB"));
    setenv("BPCB_OPENOCD_TEST", "slow", 1);
    double start = now();
    CHECK(!bc_openocd_run(q, cancelled, &start, &e) && now() - start < 2);
    json_object_object_add(q, "timeout_seconds", json_object_new_int(1));
    start = now();
    CHECK(!bc_openocd_run(q, NULL, NULL, &e) && now() - start < 3);
    setenv("BPCB_OPENOCD_TEST", "mutate", 1);
    setenv("BPCB_OPENOCD_IMAGE", file, 1);
    CHECK(!bc_openocd_run(q, NULL, NULL, &e) && strstr(e.text, "mudou"));
    unsetenv("BPCB_OPENOCD_TEST");
    str(q, "action", "identify");
    str(q, "expected_sha256", "");
    json_object_object_add(
        q, "target",
        json_tokener_parse(
            "{\"id\":\"bad\",\"family\":\"stm32\",\"script\":\"target/../../etc/passwd.cfg\"}"));
    CHECK(!bc_openocd_plan(q, &e));
    str(q, "target", "stm32f1x");
    str(q, "serial", "x; shutdown");
    CHECK(!bc_openocd_plan(q, &e));
    str(q, "serial", "TEST");
    str(q, "action", "check");
    r = bc_openocd_run(q, NULL, NULL, &e);
    CHECK(r && json_object_get_boolean(bc_get(r, "ok")));
    json_object_put(r);
    str(q, "executable", "relative");
    CHECK(!bc_openocd_plan(q, &e));
    json_object_put(q);
    unlink(file);
    unlink(exe);
    unlink(cfg);
    rmdir(dir);
    rmdir(root);
    printf("%u verificações OpenOCD: processo simulado, sem USB/JTAG\n", checks);
    return 0;
}
