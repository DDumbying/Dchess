/* genfens against engines that fail: counted, and the run fails.
 *
 * Build & run:  make test   (needs build/genfens and build/fake_uci)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* Runs genfens with the fake engine in `mode`; its exit code, and its summary line in `log`. */
static int run(const char *mode, const char *extra, char *log, size_t n, long *ms)
{
    char cmd[512], out[] = "/tmp/dchess-gf-XXXXXX", err[] = "/tmp/dchess-gf-err-XXXXXX";
    close(mkstemp(out));
    close(mkstemp(err));
    snprintf(cmd, sizeof(cmd), "FAKE_UCI_MODE=%s ./build/genfens --engine build/fake_uci --games 2 --ms 5 %s "
             "--out %s 2>%s", mode, extra, out, err);
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    int st = system(cmd);
    clock_gettime(CLOCK_MONOTONIC, &b);
    *ms = (b.tv_sec - a.tv_sec) * 1000L + (b.tv_nsec - a.tv_nsec) / 1000000L;
    log[0] = '\0';
    FILE *f = fopen(err, "r");
    if (f) { if (!fgets(log, (int)n, f)) log[0] = '\0'; fclose(f); }
    unlink(out);
    unlink(err);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int main(void)
{
    char log[512];
    long ms;
    printf("== genfens ==\n");
    int code = run("crash", "", log, sizeof(log), &ms);
    check("an engine that crashes fails the run", code == 1);
    check("and its games are counted as failed", strstr(log, "0 positions") && strstr(log, "2 failed"));
    code = run("deaf", "--hang-ms 300", log, sizeof(log), &ms);
    check("an engine that never answers is given up on", code == 1 && ms < 5000);
    code = run("normal", "", log, sizeof(log), &ms);
    check("a working engine gives positions", code == 0 && !strstr(log, " 0 positions"));
    if (failures) {
        printf("\n%d genfens test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll genfens tests passed.\n");
    return 0;
}
