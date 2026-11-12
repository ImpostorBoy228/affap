#include "test_support.h"
#include <math.h>

/* ====================================================================== */
/* benchmark harness                                                      */
/* ====================================================================== */

typedef struct {
    const char *name;
    size_t size_bytes;
    double *samples;
    size_t n;
    size_t cap;
} bench_result_t;

static void bench_result_init(bench_result_t *r, const char *name, size_t size_bytes) {
    r->name = name;
    r->size_bytes = size_bytes;
    r->cap = 8192;
    r->samples = calloc(r->cap, sizeof(double));
    r->n = 0;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static double bench_percentile(bench_result_t *r, double p) {
    if (r->n == 0) return 0;
    double *tmp = malloc(r->n * sizeof(double));
    if (!tmp) return 0;
    memcpy(tmp, r->samples, r->n * sizeof(double));
    qsort(tmp, r->n, sizeof(double), cmp_double);
    size_t idx = (size_t)((p / 100.0) * (double)(r->n - 1) + 0.5);
    if (idx >= r->n) idx = r->n - 1;
    double v = tmp[idx];
    free(tmp);
    return v;
}

static void bench_report(bench_result_t *r) {
    if (r->n == 0) return;
    double sum = 0, mn = r->samples[0], mx = r->samples[0];
    for (size_t i = 0; i < r->n; i++) {
        sum += r->samples[i];
        if (r->samples[i] < mn) mn = r->samples[i];
        if (r->samples[i] > mx) mx = r->samples[i];
    }
    double avg = sum / (double)r->n;
    double var = 0;
    for (size_t i = 0; i < r->n; i++) {
        double d = r->samples[i] - avg;
        var += d * d;
    }
    double sd = sqrt(var / (double)r->n);
    double p50 = bench_percentile(r, 50);
    double p95 = bench_percentile(r, 95);
    double p99 = bench_percentile(r, 99);
    double ops = 1e9 / avg;
    printf("  %-40s n=%-6zu  min=%8.0f  p50=%8.0f  p95=%8.0f  p99=%8.0f  max=%8.0f  avg=%8.0f ns  sd=%6.0f  ops/s=%10.0f",
           r->name, r->n, mn, p50, p95, p99, mx, avg, sd, ops);
    if (r->size_bytes > 0) {
        double mb = (double)r->size_bytes / (avg / 1e9) / (1024.0 * 1024.0);
        printf("  MB/s=%7.1f", mb);
    }
    printf("\n");
    free(r->samples);
    r->samples = NULL;
    r->n = r->cap = 0;
}

static double ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* ====================================================================== */
/* benchmarks                                                             */
/* ====================================================================== */

static void bench_fileread(void) {
    const size_t sizes[] = {0, 64, 1024, 64 * 1024, 1024 * 1024};
    const char *labels[] = {"0B", "64B", "1KiB", "64KiB", "1MiB"};

    for (size_t si = 0; si < sizeof(sizes)/sizeof(sizes[0]); si++) {
        size_t n = sizes[si];
        char *buf = NULL;
        char *path = NULL;
        bench_result_t r;

        if (n == 0) {
            buf = strdup("");
        } else {
            buf = malloc(n + 1);
            if (buf) {
                memset(buf, 'A' + (int)(si % 26), n);
                buf[n] = '\0';
            }
        }
        path = write_temp_n(buf ? buf : "", n == 0 ? 0 : n + 1);
        /* write_temp_n writes size bytes including NUL for n+1; adjust: rewrite as text */
        /* simpler: create file with exact n bytes of 'A' */
        if (path) { unlink(path); free(path); path = NULL; }
        {
            /* build path manually */
            char tmpl[] = "/tmp/affap_bench_XXXXXX";
            int fd = mkstemp(tmpl);
            if (fd >= 0) {
                size_t written = 0;
                while (written < n) {
                    ssize_t w = write(fd, buf ? buf + written : "", n - written);
                    if (w <= 0) break;
                    written += (size_t)w;
                }
                close(fd);
                path = strdup(tmpl);
            }
        }

        bench_result_init(&r, NULL, 0);
        /* dynamic name */
        char name[128];
        snprintf(name, sizeof(name), "fileread %-6s", labels[si]);
        /* warmup */
        for (int i = 0; i < 5; i++) {
            char *b = fileread(path);
            free(b);
        }
        for (long i = 0; i < 200; i++) {
            double t0 = ns_now();
            char *b = fileread(path);
            double t1 = ns_now();
            free(b);
            r.samples[r.n++] = (t1 - t0);
        }
        /* patch name */
        r.name = name;
        r.size_bytes = n;
        bench_report(&r);

        if (path) { unlink(path); free(path); }
        free(buf);
    }
}

static void bench_line_count(void) {
    /* build a big buffer with many newlines */
    size_t n = 1 << 20;
    char *buf = malloc(n + 1);
    CHECK_NOT_NULL(buf);
    if (!buf) return;
    for (size_t i = 0; i < n; i++) {
        buf[i] = (i % 80 == 79) ? '\n' : 'x';
    }
    buf[n] = '\0';

    mt state = {0};
    state.editbuffer = buf;

    bench_result_t r;
    bench_result_init(&r, "line_count (1MiB, ~13k lines)", 0);
    for (int i = 0; i < 5; i++) line_count(&state);
    for (long i = 0; i < 500; i++) {
        double t0 = ns_now();
        volatile int lc = line_count(&state);
        (void)lc;
        double t1 = ns_now();
        r.samples[r.n++] = (t1 - t0);
    }
    bench_report(&r);
    free(buf);
}

static void bench_ioffset_scroll(void) {
    size_t n = 1 << 20;
    char *buf = malloc(n + 1);
    CHECK_NOT_NULL(buf);
    if (!buf) return;
    for (size_t i = 0; i < n; i++) {
        buf[i] = (i % 80 == 79) ? '\n' : 'x';
    }
    buf[n] = '\0';

    mt state = {0};
    state.editbuffer = buf;
    int total_lines = line_count(&state);

    bench_result_t r;
    bench_result_init(&r, "ioffset_scroll (last line)", 0);
    for (int i = 0; i < 5; i++) ioffset_scroll(&state, total_lines - 1);
    for (long i = 0; i < 500; i++) {
        double t0 = ns_now();
        volatile size_t off = ioffset_scroll(&state, total_lines - 1);
        (void)off;
        double t1 = ns_now();
        r.samples[r.n++] = (t1 - t0);
    }
    bench_report(&r);

    /* worst-case scan middle */
    bench_result_init(&r, "ioffset_scroll (middle line)", 0);
    int mid = total_lines / 2;
    for (int i = 0; i < 5; i++) ioffset_scroll(&state, mid);
    for (long i = 0; i < 500; i++) {
        double t0 = ns_now();
        volatile size_t off = ioffset_scroll(&state, mid);
        (void)off;
        double t1 = ns_now();
        r.samples[r.n++] = (t1 - t0);
    }
    bench_report(&r);

    free(buf);
}

static void bench_update_text(void) {
    size_t n = 64 * 1024;
    char *buf = malloc(n + 1);
    CHECK_NOT_NULL(buf);
    if (!buf) return;
    for (size_t i = 0; i < n; i++) {
        buf[i] = (i % 90 == 89) ? '\n' : 'x';
    }
    buf[n] = '\0';

    mt state = {0};
    state.editbuffer = buf;
    state.scroll = 0;

    reset_all();
    mock_rows = 24;
    mock_cols = 80;

    bench_result_t r;
    bench_result_init(&r, "update_text (64KiB buffer)", 0);
    for (int i = 0; i < 5; i++) update_text(WIN_TEXT, &state);
    for (long i = 0; i < 2000; i++) {
        double t0 = ns_now();
        update_text(WIN_TEXT, &state);
        double t1 = ns_now();
        r.samples[r.n++] = (t1 - t0);
    }
    bench_report(&r);
    free(buf);
}

static void bench_app_main_startup(void) {
    char *path = write_temp("l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9");
    CHECK_NOT_NULL(path);
    if (!path) return;

    bench_result_t r;
    bench_result_init(&r, "app_main startup+shutdown", 0);
    for (int i = 0; i < 3; i++) {
        reset_all();
        char *argv[] = {"affap", path, NULL};
        queue_keys((const int[]){ERR}, 1);
        app_main(2, argv);
    }
    for (long i = 0; i < 100; i++) {
        reset_all();
        char *argv[] = {"affap", path, NULL};
        queue_keys((const int[]){ERR}, 1);
        double t0 = ns_now();
        app_main(2, argv);
        double t1 = ns_now();
        r.samples[r.n++] = (t1 - t0);
    }
    bench_report(&r);

    unlink(path);
    free(path);
}

static void bench_scroll_loop(void) {
    /* many lines, scroll down all the way via mouse events */
    size_t lines = 100000;
    /* build buffer */
    size_t cap = lines * 12;
    char *buf = malloc(cap + 1);
    CHECK_NOT_NULL(buf);
    if (!buf) return;
    size_t pos = 0;
    for (size_t i = 0; i < lines; i++) {
        int w = snprintf(buf + pos, cap - pos, "line %zu\n", i);
        pos += (size_t)w;
    }
    buf[pos] = '\0';
    char *path = write_temp(buf);
    CHECK_NOT_NULL(path);
    if (!path) { free(buf); return; }

    bench_result_t r;
    bench_result_init(&r, "app_main scroll 100k lines (all down)", 0);
    /* single run measured */
    reset_all();
    /* queue 100001 mouse downs + ERR */
    /* can't queue that many (4096 limit); do in chunks? app_main processes until ERR.
       We'll queue 4095 downs then ERR; enough to stress. */
    int nkeys = 4096;
    int *keys = calloc((size_t)nkeys, sizeof(int));
    for (int i = 0; i < nkeys - 1; i++) keys[i] = KEY_MOUSE;
    keys[nkeys - 1] = ERR;
    for (int i = 0; i < nkeys - 1; i++) queue_mouse(BUTTON5_PRESSED, OK);
    /* queue_keys resets; do after queue_mouse? queue_keys doesn't touch mouse. order fine */
    queue_keys(keys, nkeys);

    double t0 = ns_now();
    char *argv[] = {"affap", path, NULL};
    app_main(2, argv);
    double t1 = ns_now();
    printf("  %-44s  total=%.3f ms\n", "app_main scroll 4095 down (100k lines)", (t1 - t0) / 1e6);

    /* suppress unused */
    (void)r;
    free(keys);
    unlink(path);
    free(path);
    free(buf);
}

int main(void) {
    printf("affap benchmarks\n");
    printf("================\n");
    printf("  (times are per-call; lower is better)\n\n");

    bench_fileread();
    printf("\n");
    bench_line_count();
    bench_ioffset_scroll();
    printf("\n");
    bench_update_text();
    printf("\n");
    bench_app_main_startup();
    bench_scroll_loop();

    printf("\ndone.\n");
    return 0;
}