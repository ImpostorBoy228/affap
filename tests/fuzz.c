#include "test_support.h"

/* ====================================================================== */
/* deterministic PRNG                                                     */
/* ====================================================================== */

static unsigned int fuzz_state = 0x9e3779b9u;

static unsigned int fuzz_rand(void) {
    fuzz_state ^= fuzz_state << 13;
    fuzz_state ^= fuzz_state >> 17;
    fuzz_state ^= fuzz_state << 5;
    return fuzz_state;
}

static void fuzz_bytes(char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned int r = fuzz_rand();
        /* mix printable, newline, spaces, occasional NUL-ish terminator control */
        char c = (char)(r & 0x7f);
        if (c == 0) c = 'a';
        if ((r & 0x1f) == 0) c = '\n';
        buf[i] = c;
    }
    buf[n] = '\0';
}

/* ====================================================================== */
/* fuzz targets                                                           */
/* ====================================================================== */

static int fuzz_ioffset_linecount(void) {
    int iters = 20000;
    for (int t = 0; t < iters; t++) {
        size_t n = (size_t)(fuzz_rand() % 256);
        char buf[257];
        fuzz_bytes(buf, n);
        mt state;
        memset(&state, 0, sizeof(state));
        state.editbuffer = narezka(strdup(buf));

        int lc = line_count(&state);
        if (lc < 1) return 1;
        size_t len = strlen(buf);
        for (int ln = -10; ln <= lc + 5; ln++) {
            size_t off = ioffset_scroll(&state, ln);
            if (off > len) return 2;
        }
    }
    return 0;
}

static int fuzz_fileread(void) {
    int iters = 2000;
    for (int t = 0; t < iters; t++) {
        size_t n = (size_t)(fuzz_rand() % 4096);
        char *buf = malloc(n + 1);
        if (!buf) return 10;
        fuzz_bytes(buf, n);
        char *path = write_temp_n(buf, n); /* may contain embedded? fuzz_bytes no NUL except terminator not written if n? write n bytes; buf has NUL at n but write n excludes it. file has no NUL */
        /* Actually write_temp_n writes n bytes from buf, which has no embedded NUL (fuzz_bytes puts no 0). Good. */
        if (!path) { free(buf); return 11; }
        char *out = fileread(path);
        if (!out) { unlink(path); free(path); free(buf); return 12; }
        /* fileread adds NUL; content should match written bytes */
        if (memcmp(out, buf, n) != 0) { 
            unlink(path); free(path); free(buf); free(out); return 13;
        }
        if (out[n] != '\0') {
            unlink(path); free(path); free(buf); free(out); return 14;
        }
        free(out);
        unlink(path);
        free(path);
        free(buf);
    }
    return 0;
}

static int fuzz_bootstap(void) {
    int iters = 500;
    for (int t = 0; t < iters; t++) {
        /* mix existing and missing paths */
        char *path = nullptr;
        if (fuzz_rand() & 1) {
            size_t n = fuzz_rand() % 64;
            char buf[65];
            fuzz_bytes(buf, n);
            path = write_temp(buf);
        } else {
            path = strdup("/definitely/does/not/exist/fuzz");
        }
        if (!path) return 20;
        reset_all();
        mock_rows = (int)(fuzz_rand() % 100) + 1;
        mock_cols = (int)(fuzz_rand() % 200) + 1;
        mt state = {0};
        bootstap_mt(&state, WIN_TEXT, path);
        if (state.max_x != mock_cols) { free_list(state.editbuffer); free(path); return 21; }
        if (state.max_y != mock_rows) { free_list(state.editbuffer); free(path); return 22; }
        if (state.editbuffer != nullptr) {
            /* must be NUL terminated */
            /* strlen should be safe */
            (void)strlen(state.editbuffer ? state.editbuffer->str : "");
        }
        free_list(state.editbuffer);
        free(path);
    }
    return 0;
}

static int fuzz_app_main(void) {
    int iters = 300;
    for (int t = 0; t < iters; t++) {
        /* random file content */
        size_t n = fuzz_rand() % 512;
        char buf[513];
        fuzz_bytes(buf, n);
        char *path = write_temp(buf);
        if (!path) return 30;

        reset_all();
        mock_rows = (fuzz_rand() % 40) + 5;
        mock_cols = (fuzz_rand() % 80) + 20;

        /* random key sequence */
        int nkeys = (fuzz_rand() % 40) + 1;
        int *keys = calloc((size_t)nkeys + 1, sizeof(int));
        if (!keys) { free(path); return 31; }
        for (int i = 0; i < nkeys; i++) {
            int r = fuzz_rand() % 5;
            if (r == 0) {
                keys[i] = KEY_MOUSE;
                queue_mouse((fuzz_rand() & 1) ? BUTTON5_PRESSED : BUTTON4_PRESSED, OK);
            } else if (r == 1) {
                keys[i] = KEY_MOUSE;
                queue_mouse(BUTTON5_PRESSED, ERR); /* getmouse error */
            } else if (r == 2) {
                keys[i] = KEY_RESIZE;
                resize_pending = 1;
                resize_to_rows = (fuzz_rand() % 50) + 5;
                resize_to_cols = (fuzz_rand() % 100) + 20;
            } else if (r == 3) {
                keys[i] = 0; /* unknown key, ignored */
            } else {
                keys[i] = 'x';
            }
        }
        keys[nkeys] = ERR;
        queue_keys(keys, nkeys + 1);

        char *argv[] = {"affap", path, nullptr};
        int rc = app_main(2, argv);
        if (rc != 0) {
            /* app_main returns 0 on normal exit; nonzero only on missing file (not here) */
            free(keys); unlink(path); free(path);
            return 32;
        }

        free(keys);
        unlink(path);
        free(path);
    }
    return 0;
}

static int fuzz_update_render(void) {
    int iters = 5000;
    for (int t = 0; t < iters; t++) {
        size_t n = fuzz_rand() % 1024;
        char buf[1025];
        fuzz_bytes(buf, n);
        mt state;
        memset(&state, 0, sizeof(state));
        state.editbuffer = narezka(strdup(buf));
        state.path = buf; /* alias okay for read-only usage? update_srtstate uses path for printing; fine */
        state.scroll = (int)(fuzz_rand() % 10) - 3;

        reset_all();
        mock_rows = (fuzz_rand() % 50) + 1;
        mock_cols = (fuzz_rand() % 100) + 1;
        update(WIN_STATUS, WIN_TEXT, &state);
        /* no crash = pass */
    }
    return 0;
}

int main(void) {
    printf("affap fuzz suite\n");
    printf("================\n");

    int rc;
    rc = fuzz_ioffset_linecount();
    printf("  fuzz_ioffset_linecount: %s\n", rc == 0 ? "OK" : "FAIL");
    if (rc) return 1;

    rc = fuzz_fileread();
    printf("  fuzz_fileread:          %s\n", rc == 0 ? "OK" : "FAIL");
    if (rc) return 1;

    rc = fuzz_bootstap();
    printf("  fuzz_bootstap:          %s\n", rc == 0 ? "OK" : "FAIL");
    if (rc) return 1;

    rc = fuzz_app_main();
    printf("  fuzz_app_main:          %s\n", rc == 0 ? "OK" : "FAIL");
    if (rc) return 1;

    rc = fuzz_update_render();
    printf("  fuzz_update_render:     %s\n", rc == 0 ? "OK" : "FAIL");
    if (rc) return 1;

    printf("\nall fuzz targets passed.\n");
    return 0;
}