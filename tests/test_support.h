/*
 * Shared test support for affap.
 *
 * This header provides:
 *   - ncurses mocks (so the code under test runs without a terminal),
 *   - the application code (main.c) included with `main` renamed to `app_main`,
 *   - reset/queue/temp-file helpers,
 *   - a small assertion + test-runner framework,
 *   - timing helpers used by the benchmark suite.
 *
 * Include this header exactly once per translation unit.
 */

#ifndef AFFAP_TEST_SUPPORT_H
#define AFFAP_TEST_SUPPORT_H

#include <locale.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

/* ---------------------------------------------------------------------- */
/* mock state                                                             */
/* ---------------------------------------------------------------------- */

static int mock_rows = 24;
static int mock_cols = 80;

static char win_std_token;
static char win_text_token;
static char win_status_token;

#define WIN_STD ((WINDOW *)&win_std_token)
#define WIN_TEXT ((WINDOW *)&win_text_token)
#define WIN_STATUS ((WINDOW *)&win_status_token)

static char cap_text[1 << 20];
static char cap_status[1 << 20];

static int text_erased;
static int status_erased;
static int text_saw_erase;
static int status_saw_erase;
static int text_attr;
static int status_attr;

static int wr_count;
static int refresh_count;
static int keypad_count;
static int mousemask_count;
static mmask_t last_mousemask;

static int wbkgd_text_ch;
static int wbkgd_status_ch;

static int newwin_count;
static int delwin_count;
static int initscr_count;
static int endwin_count;
static int start_color_count;
static int default_colors_count;

static int pair_count;
static int pair_ids[64];
static int pair_fgs[64];
static int pair_bgs[64];

static int newwin_nlines[64];
static int newwin_ncols[64];
static int newwin_y[64];
static int newwin_x[64];

static int key_queue[4096];
static int key_n;
static int key_i;

static mmask_t mouse_queue[4096];
static int mouse_ret_queue[4096];
static int mouse_n;
static int mouse_i;

static int resize_pending;
static int resize_to_rows;
static int resize_to_cols;

/* ---------------------------------------------------------------------- */
/* mocks                                                                  */
/* ---------------------------------------------------------------------- */

static int mock_getmaxyx(WINDOW *w, int *y, int *x) {
    (void)w;
    *y = mock_rows;
    *x = mock_cols;
    return OK;
}

static int mock_getmaxx(WINDOW *w) {
    (void)w;
    return mock_cols;
}

static int mock_werase(WINDOW *w) {
    if (w == WIN_TEXT) {
        text_erased = 1;
    }
    if (w == WIN_STATUS) {
        status_erased = 1;
    }
    return OK;
}

static int mock_wattron(WINDOW *w, int attrs) {
    if (w == WIN_TEXT) {
        text_attr = attrs;
    }
    if (w == WIN_STATUS) {
        status_attr = attrs;
    }
    return OK;
}

static int mock_wattroff(WINDOW *w, int attrs) {
    (void)w;
    (void)attrs;
    return OK;
}

static int mock_wrefresh(WINDOW *w) {
    (void)w;
    wr_count++;
    return OK;
}

static int mock_mvwprintw(WINDOW *w, int y, int x, const char *fmt, ...) {
    char buf[1 << 20];
    va_list ap;

    (void)y;
    (void)x;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (w == WIN_TEXT) {
        snprintf(cap_text, sizeof(cap_text), "%s", buf);
        text_saw_erase = text_erased;
    } else if (w == WIN_STATUS) {
        snprintf(cap_status, sizeof(cap_status), "%s", buf);
        status_saw_erase = status_erased;
    }
    return OK;
}

static int mock_wbkgd(WINDOW *w, chtype ch) {
    if (w == WIN_TEXT) {
        wbkgd_text_ch = (int)ch;
    } else if (w == WIN_STATUS) {
        wbkgd_status_ch = (int)ch;
    }
    return OK;
}

static WINDOW *mock_newwin(int nlines, int ncols, int begin_y, int begin_x) {
    if (newwin_count < 64) {
        newwin_nlines[newwin_count] = nlines;
        newwin_ncols[newwin_count] = ncols;
        newwin_y[newwin_count] = begin_y;
        newwin_x[newwin_count] = begin_x;
    }
    newwin_count++;
    return (nlines == 1) ? WIN_STATUS : WIN_TEXT;
}

static int mock_delwin(WINDOW *w) {
    (void)w;
    delwin_count++;
    return OK;
}

static int mock_getch(void) {
    int key;

    if (key_i >= key_n) {
        return ERR;
    }

    key = key_queue[key_i++];
    if (key == KEY_RESIZE && resize_pending) {
        mock_rows = resize_to_rows;
        mock_cols = resize_to_cols;
        resize_pending = 0;
    }
    return key;
}

static int mock_getmouse(MEVENT *event) {
    int i;

    if (mouse_i >= mouse_n) {
        return ERR;
    }

    i = mouse_i++;
    if (event != nullptr) {
        memset(event, 0, sizeof(*event));
        event->bstate = mouse_queue[i];
    }
    return mouse_ret_queue[i];
}

static WINDOW *mock_initscr(void) {
    initscr_count++;
    return WIN_STD;
}

static int mock_endwin(void) {
    endwin_count++;
    return OK;
}

static int mock_start_color(void) {
    start_color_count++;
    return OK;
}

static int mock_use_default_colors(void) {
    default_colors_count++;
    return OK;
}

static int mock_init_extended_pair(int pair, int fg, int bg) {
    if (pair_count < 64) {
        pair_ids[pair_count] = pair;
        pair_fgs[pair_count] = fg;
        pair_bgs[pair_count] = bg;
    }
    pair_count++;
    return OK;
}

static int mock_cbreak(void) { return OK; }
static int mock_noecho(void) { return OK; }

static int mock_curs_set(int visibility) {
    (void)visibility;
    return 1;
}

static int mock_keypad(WINDOW *w, bool enable) {
    (void)w;
    (void)enable;
    keypad_count++;
    return OK;
}

static int mock_refresh(void) {
    refresh_count++;
    return OK;
}

static mmask_t mock_mousemask(mmask_t mask, mmask_t *oldmask) {
    mousemask_count++;
    last_mousemask = mask;
    if (oldmask != nullptr) {
        *oldmask = 0;
    }
    return mask;
}

/* ---------------------------------------------------------------------- */
/* ncurses -> mocks                                                       */
/* ---------------------------------------------------------------------- */

#undef getmaxyx
#undef getmaxx
#undef werase
#undef wattron
#undef wattroff
#undef wrefresh
#undef mvwprintw
#undef wbkgd
#undef newwin
#undef delwin
#undef getch
#undef getmouse
#undef initscr
#undef endwin
#undef start_color
#undef use_default_colors
#undef init_extended_pair
#undef cbreak
#undef noecho
#undef curs_set
#undef keypad
#undef refresh
#undef mousemask

#define getmaxyx(w, y, x) mock_getmaxyx((w), &(y), &(x))
#define getmaxx(w) mock_getmaxx((w))
#define werase(w) mock_werase((w))
#define wattron(w, a) mock_wattron((w), (a))
#define wattroff(w, a) mock_wattroff((w), (a))
#define wrefresh(w) mock_wrefresh((w))
#define mvwprintw(w, y, x, ...) mock_mvwprintw((w), (y), (x), __VA_ARGS__)
#define wbkgd(w, ch) mock_wbkgd((w), (ch))
#define newwin(nl, nc, y, x) mock_newwin((nl), (nc), (y), (x))
#define delwin(w) mock_delwin((w))
#define getch() mock_getch()
#define getmouse(e) mock_getmouse((e))
#define initscr() mock_initscr()
#define endwin() mock_endwin()
#define start_color() mock_start_color()
#define use_default_colors() mock_use_default_colors()
#define init_extended_pair(p, fg, bg) mock_init_extended_pair((p), (fg), (bg))
#define cbreak() mock_cbreak()
#define noecho() mock_noecho()
#define curs_set(v) mock_curs_set((v))
#define keypad(w, e) mock_keypad((w), (e))
#define refresh() mock_refresh()
#define mousemask(m, o) mock_mousemask((m), (o))

#define main app_main
#include "../main.c"
#undef main

/* ---------------------------------------------------------------------- */
/* helpers                                                                */
/* ---------------------------------------------------------------------- */

static void reset_all(void) {
    mock_rows = 24;
    mock_cols = 80;

    memset(cap_text, 0, sizeof(cap_text));
    memset(cap_status, 0, sizeof(cap_status));

    text_erased = 0;
    status_erased = 0;
    text_saw_erase = 0;
    status_saw_erase = 0;
    text_attr = 0;
    status_attr = 0;

    wr_count = 0;
    refresh_count = 0;
    keypad_count = 0;
    mousemask_count = 0;
    last_mousemask = 0;

    wbkgd_text_ch = 0;
    wbkgd_status_ch = 0;

    newwin_count = 0;
    delwin_count = 0;
    initscr_count = 0;
    endwin_count = 0;
    start_color_count = 0;
    default_colors_count = 0;

    pair_count = 0;
    memset(pair_ids, 0, sizeof(pair_ids));
    memset(pair_fgs, 0, sizeof(pair_fgs));
    memset(pair_bgs, 0, sizeof(pair_bgs));

    memset(newwin_nlines, 0, sizeof(newwin_nlines));
    memset(newwin_ncols, 0, sizeof(newwin_ncols));
    memset(newwin_y, 0, sizeof(newwin_y));
    memset(newwin_x, 0, sizeof(newwin_x));

    memset(key_queue, 0, sizeof(key_queue));
    key_n = 0;
    key_i = 0;

    memset(mouse_queue, 0, sizeof(mouse_queue));
    memset(mouse_ret_queue, 0, sizeof(mouse_ret_queue));
    mouse_n = 0;
    mouse_i = 0;

    resize_pending = 0;
    resize_to_rows = 0;
    resize_to_cols = 0;
}

static void queue_keys(const int *keys, int count) {
    key_n = 0;
    key_i = 0;
    for (int i = 0; i < count && i < 4096; i++) {
        key_queue[i] = keys[i];
    }
    key_n = count > 4096 ? 4096 : count;
}

static void queue_mouse(mmask_t bstate, int ret) {
    if (mouse_n < 4096) {
        mouse_queue[mouse_n] = bstate;
        mouse_ret_queue[mouse_n] = ret;
        mouse_n++;
    }
}

static char *write_temp_n(const void *data, size_t size) {
    char tmpl[] = "/tmp/affap_XXXXXX";
    int fd = mkstemp(tmpl);

    if (fd < 0) {
        return nullptr;
    }

    if (size > 0 && write(fd, data, size) < 0) {
        close(fd);
        unlink(tmpl);
        return nullptr;
    }

    close(fd);
    return strdup(tmpl);
}

static char *write_temp(const char *text) {
    return write_temp_n(text, strlen(text));
}

static __attribute__((unused)) int env_is(const char *name, const char *value) {
    const char *current = getenv(name);

    if (value == nullptr) {
        return current == nullptr;
    }
    return current != nullptr && strcmp(current, value) == 0;
}

/* ---------------------------------------------------------------------- */
/* framework                                                              */
/* ---------------------------------------------------------------------- */

static int checks __attribute__((unused)) = 0;
static int failures __attribute__((unused)) = 0;
static int tests_run __attribute__((unused)) = 0;
static int tests_failed __attribute__((unused)) = 0;

static __attribute__((unused)) double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

#define CHECK_STR(expected, actual)                                            \
    do {                                                                       \
        checks++;                                                              \
        const char *e_ = (expected);                                           \
        const char *a_ = (actual);                                             \
        if (e_ == nullptr || a_ == nullptr || strcmp(e_, a_) != 0) {                 \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d\n  expected: [%s]\n  actual:   [%s]\n",\
                    __FILE__, __LINE__, e_ ? e_ : "(null)",                    \
                    a_ ? a_ : "(null)");                                       \
        }                                                                      \
    } while (0)

#define CHECK_INT(expected, actual)                                            \
    do {                                                                       \
        checks++;                                                              \
        long long e_ = (long long)(expected);                                  \
        long long a_ = (long long)(actual);                                    \
        if (e_ != a_) {                                                        \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n",            \
                    __FILE__, __LINE__, e_, a_);                               \
        }                                                                      \
    } while (0)

#define CHECK_SIZE(expected, actual) CHECK_INT((size_t)(expected), (size_t)(actual))

#define CHECK_PTR(expected, actual)                                            \
    do {                                                                       \
        checks++;                                                              \
        if ((void *)(expected) != (void *)(actual)) {                          \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: ptr expected %p, got %p\n",           \
                    __FILE__, __LINE__, (void *)(expected), (void *)(actual));  \
        }                                                                      \
    } while (0)

#define CHECK_NOT_NULL(p)                                                      \
    do {                                                                       \
        checks++;                                                              \
        if ((p) == nullptr) {                                                     \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: unexpected nullptr: %s\n",               \
                    __FILE__, __LINE__, #p);                                   \
        }                                                                      \
    } while (0)

#define CHECK_NULL(p)                                                          \
    do {                                                                       \
        checks++;                                                              \
        if ((p) != nullptr) {                                                     \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: expected nullptr: %s\n",                 \
                    __FILE__, __LINE__, #p);                                   \
        }                                                                      \
    } while (0)

#define CHECK_MEM(expected, actual, n)                                         \
    do {                                                                       \
        checks++;                                                              \
        if (memcmp((expected), (actual), (n)) != 0) {                          \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: memory mismatch (%zu bytes)\n",       \
                    __FILE__, __LINE__, (size_t)(n));                          \
        }                                                                      \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                       \
    do {                                                                       \
        checks++;                                                              \
        const char *h_ = (haystack);                                           \
        const char *n_ = (needle);                                             \
        if (h_ == nullptr || n_ == nullptr || strstr(h_, n_) == nullptr) {              \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: [%s] not found in [%s]\n",            \
                    __FILE__, __LINE__, n_ ? n_ : "(null)",                    \
                    h_ ? h_ : "(null)");                                       \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                       \
        int _bc = checks;                                                      \
        int _bf = failures;                                                    \
        tests_run++;                                                           \
        double _t0 = now_sec();                                                \
        fn();                                                                  \
        double _t1 = now_sec();                                                \
        int _nf = failures - _bf;                                              \
        if (_nf > 0) {                                                         \
            tests_failed++;                                                    \
        }                                                                      \
        printf("  %-52s %s  %6.3f ms  (%d checks)\n", #fn,                    \
               _nf > 0 ? "FAIL" : " ok ", (_t1 - _t0) * 1000.0,              \
               checks - _bc);                                                   \
        fflush(stdout);                                                        \
    } while (0)

#endif /* AFFAP_TEST_SUPPORT_H */