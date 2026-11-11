/*
 * Tests for affap.
 *
 * main.c is included directly (with main renamed) so the static helpers can be
 * tested without changing the application. All ncurses calls are replaced by
 * mocks below, so the tests do not need a real terminal.
 */

#include <locale.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks;
static int failures;

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
        if (e_ == NULL || a_ == NULL || strcmp(e_, a_) != 0) {                 \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d\n  expected: [%s]\n  actual:   [%s]\n",\
                    __FILE__, __LINE__, e_ ? e_ : "(null)",                    \
                    a_ ? a_ : "(null)");                                       \
        }                                                                      \
    } while (0)

/* ---------------------------------------------------------------------- */
/* mock state                                                             */
/* ---------------------------------------------------------------------- */

static int mock_rows = 24;
static int mock_cols = 80;

/* WINDOW is opaque in ncurses; the mocks only compare pointers and never
 * dereference them, so plain tokens are enough. */
static char win_std_token;
static char win_text_token;
static char win_status_token;

#define WIN_STD ((WINDOW *)&win_std_token)
#define WIN_TEXT ((WINDOW *)&win_text_token)
#define WIN_STATUS ((WINDOW *)&win_status_token)

static char cap_text[16384];
static char cap_status[16384];

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
static int pair_ids[8];
static int pair_fgs[8];
static int pair_bgs[8];

static int newwin_nlines[8];
static int newwin_ncols[8];
static int newwin_y[8];
static int newwin_x[8];

static int key_queue[64];
static int key_n;
static int key_i;

static mmask_t mouse_queue[64];
static int mouse_ret_queue[64];
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
    char buf[16384];
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
    if (newwin_count < 8) {
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
    if (event != NULL) {
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
    if (pair_count < 8) {
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
    if (oldmask != NULL) {
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
    for (int i = 0; i < count && i < 64; i++) {
        key_queue[i] = keys[i];
    }
    key_n = count > 64 ? 64 : count;
}

static void queue_mouse(mmask_t bstate, int ret) {
    if (mouse_n < 64) {
        mouse_queue[mouse_n] = bstate;
        mouse_ret_queue[mouse_n] = ret;
        mouse_n++;
    }
}

static char *write_temp_n(const void *data, size_t size) {
    char tmpl[] = "/tmp/affap_tests_XXXXXX";
    int fd = mkstemp(tmpl);

    if (fd < 0) {
        return NULL;
    }

    if (size > 0 && write(fd, data, size) < 0) {
        close(fd);
        unlink(tmpl);
        return NULL;
    }

    close(fd);
    return strdup(tmpl);
}

static char *write_temp(const char *text) {
    return write_temp_n(text, strlen(text));
}

static int env_is(const char *name, const char *value) {
    const char *current = getenv(name);

    if (value == NULL) {
        return current == NULL;
    }
    return current != NULL && strcmp(current, value) == 0;
}

/* ---------------------------------------------------------------------- */
/* tests: force_direct_term                                               */
/* ---------------------------------------------------------------------- */

static void test_force_direct_term(void) {
    char *old_term = getenv("TERM") ? strdup(getenv("TERM")) : NULL;
    char *old_colorterm =
        getenv("COLORTERM") ? strdup(getenv("COLORTERM")) : NULL;

    reset_all();

    unsetenv("TERM");
    unsetenv("COLORTERM");
    force_direct_term();
    CHECK(env_is("TERM", NULL));

    setenv("TERM", "foot-extra", 1);
    force_direct_term();
    CHECK(env_is("TERM", "foot-direct"));

    setenv("TERM", "tmux-256color", 1);
    force_direct_term();
    CHECK(env_is("TERM", "tmux-direct"));

    setenv("TERM", "xterm-256color", 1);
    force_direct_term();
    CHECK(env_is("TERM", "xterm-direct"));

    setenv("TERM", "foot", 1);
    setenv("COLORTERM", "truecolor", 1);
    force_direct_term();
    CHECK(env_is("TERM", "foot-direct"));

    setenv("TERM", "alacritty", 1);
    setenv("COLORTERM", "truecolor", 1);
    force_direct_term();
    CHECK(env_is("TERM", "xterm-direct"));

    setenv("TERM", "alacritty", 1);
    setenv("COLORTERM", "24bit", 1);
    force_direct_term();
    CHECK(env_is("TERM", "xterm-direct"));

    setenv("TERM", "alacritty", 1);
    setenv("COLORTERM", "no", 1);
    force_direct_term();
    CHECK(env_is("TERM", "alacritty"));

    setenv("TERM", "alacritty", 1);
    unsetenv("COLORTERM");
    force_direct_term();
    CHECK(env_is("TERM", "alacritty"));

    if (old_term != NULL) {
        setenv("TERM", old_term, 1);
        free(old_term);
    } else {
        unsetenv("TERM");
    }

    if (old_colorterm != NULL) {
        setenv("COLORTERM", old_colorterm, 1);
        free(old_colorterm);
    } else {
        unsetenv("COLORTERM");
    }
}

/* ---------------------------------------------------------------------- */
/* tests: fileread                                                        */
/* ---------------------------------------------------------------------- */

static void test_fileread(void) {
    char *path;
    char *buffer;
    const char binary[] = {'a', '\0', 'b', 'c'};

    reset_all();

    CHECK(fileread("/definitely/does/not/exist") == NULL);

    path = write_temp("");
    CHECK(path != NULL);
    buffer = fileread(path);
    CHECK(buffer != NULL);
    if (buffer != NULL) {
        CHECK(buffer[0] == '\0');
    }
    free(buffer);
    unlink(path);
    free(path);

    path = write_temp("hello\nworld");
    CHECK(path != NULL);
    buffer = fileread(path);
    CHECK(buffer != NULL);
    if (buffer != NULL) {
        CHECK_STR("hello\nworld", buffer);
    }
    free(buffer);
    unlink(path);
    free(path);

    path = write_temp_n(binary, sizeof(binary));
    CHECK(path != NULL);
    buffer = fileread(path);
    CHECK(buffer != NULL);
    if (buffer != NULL) {
        CHECK(buffer[0] == 'a');
        CHECK(buffer[1] == '\0');
        CHECK(buffer[2] == 'b');
        CHECK(buffer[3] == 'c');
        CHECK(buffer[4] == '\0');
    }
    free(buffer);
    unlink(path);
    free(path);
}

/* ---------------------------------------------------------------------- */
/* tests: ioffset_scroll / line_count                                     */
/* ---------------------------------------------------------------------- */

static void test_ioffset_scroll(void) {
    mt state = {0};

    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 5) == 0);

    state.editbuffer = (char *)"a\nb\nc";
    CHECK(ioffset_scroll(&state, -1) == 0);
    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 1) == 2);
    CHECK(ioffset_scroll(&state, 2) == 4);
    /* out-of-range line currently falls back to the start of the buffer */
    CHECK(ioffset_scroll(&state, 3) == 0);
    CHECK(ioffset_scroll(&state, 42) == 0);

    state.editbuffer = (char *)"";
    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 1) == 0);

    state.editbuffer = (char *)"no newlines";
    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 1) == 0);

    state.editbuffer = NULL;
    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 3) == 0);
}

static void test_line_count(void) {
    mt state = {0};

    CHECK(line_count(&state) == 1);

    state.editbuffer = (char *)"";
    CHECK(line_count(&state) == 1);

    state.editbuffer = (char *)"a";
    CHECK(line_count(&state) == 1);

    state.editbuffer = (char *)"a\n";
    CHECK(line_count(&state) == 2);

    state.editbuffer = (char *)"a\nb\n";
    CHECK(line_count(&state) == 3);

    state.editbuffer = (char *)"\n\n\n";
    CHECK(line_count(&state) == 4);
}

/* ---------------------------------------------------------------------- */
/* tests: bootstap_mt                                                     */
/* ---------------------------------------------------------------------- */

static void test_bootstap_mt(void) {
    mt state = {0};
    char *path;

    reset_all();
    mock_rows = 30;
    mock_cols = 100;

    path = write_temp("first\nsecond");
    CHECK(path != NULL);

    bootstap_mt(&state, WIN_TEXT, path);
    CHECK(state.max_x == 100);
    CHECK(state.max_y == 30);
    CHECK(state.cur_x == 0);
    CHECK(state.cur_y == 0);
    CHECK(state.scroll == 0);
    CHECK(state.path == path);
    CHECK(state.editbuffer != NULL);
    if (state.editbuffer != NULL) {
        CHECK_STR("first\nsecond", state.editbuffer);
    }

    free(state.editbuffer);
    unlink(path);
    free(path);

    memset(&state, 0, sizeof(state));
    bootstap_mt(&state, WIN_TEXT, "/definitely/does/not/exist");
    CHECK(state.max_x == 100);
    CHECK(state.max_y == 30);
    CHECK(state.editbuffer == NULL);
    CHECK_STR("/definitely/does/not/exist", state.path);
    free(state.editbuffer);
}

/* ---------------------------------------------------------------------- */
/* tests: windows and colors                                              */
/* ---------------------------------------------------------------------- */

static void test_text_win(void) {
    WINDOW *window;

    reset_all();
    mock_rows = 24;
    mock_cols = 80;

    window = text_win();
    CHECK(window == WIN_TEXT);
    CHECK(newwin_count == 1);
    CHECK(newwin_nlines[0] == 23);
    CHECK(newwin_ncols[0] == 80);
    CHECK(newwin_y[0] == 0);
    CHECK(newwin_x[0] == 0);
    CHECK(wbkgd_text_ch == (int)(' ' | COLOR_PAIR(PAIR_TEXT)));
}

static void test_strstate_win(void) {
    WINDOW *window;

    reset_all();
    mock_rows = 24;
    mock_cols = 80;

    window = strstate_win();
    CHECK(window == WIN_STATUS);
    CHECK(newwin_count == 1);
    CHECK(newwin_nlines[0] == 1);
    CHECK(newwin_ncols[0] == 80);
    CHECK(newwin_y[0] == 23);
    CHECK(newwin_x[0] == 0);
    CHECK(wbkgd_status_ch == (int)(' ' | COLOR_PAIR(PAIR_STATUS)));
}

static void test_init_colors(void) {
    reset_all();

    init_colors();

    CHECK(start_color_count == 1);
    CHECK(default_colors_count == 1);
    CHECK(pair_count == 2);

    CHECK(pair_ids[0] == PAIR_STATUS);
    CHECK(pair_fgs[0] == STATUS_FG);
    CHECK(pair_bgs[0] == STATUS_BG);

    CHECK(pair_ids[1] == PAIR_TEXT);
    CHECK(pair_fgs[1] == TEXT_FG);
    CHECK(pair_bgs[1] == TEXT_BG);
}

/* ---------------------------------------------------------------------- */
/* tests: rendering                                                       */
/* ---------------------------------------------------------------------- */

static void test_update_srtstate(void) {
    mt state = {0};

    reset_all();
    mock_cols = 10;

    state.path = "/tmp/x";
    update_srtstate(WIN_STATUS, &state);

    CHECK_STR("/tmp/x    ", cap_status);
    CHECK(status_attr == COLOR_PAIR(PAIR_STATUS));
    CHECK(wr_count == 1);

    reset_all();
    mock_cols = 4;
    state.path = "longer-than-window";
    update_srtstate(WIN_STATUS, &state);
    CHECK_STR("longer-than-window", cap_status);

    reset_all();
    mock_cols = 0;
    state.path = "";
    update_srtstate(WIN_STATUS, &state);
    CHECK_STR("", cap_status);
}

static void test_update_text(void) {
    mt state = {0};

    reset_all();
    state.editbuffer = (char *)"line0\nline1\nline2";

    update_text(WIN_TEXT, &state);
    CHECK(text_erased == 1);
    CHECK(text_saw_erase == 1);
    CHECK(text_attr == COLOR_PAIR(PAIR_TEXT));
    CHECK_STR("line0\nline1\nline2", cap_text);
    CHECK(wr_count == 1);

    state.scroll = 1;
    text_erased = 0;
    text_saw_erase = 0;
    update_text(WIN_TEXT, &state);
    CHECK(text_erased == 1);
    CHECK(text_saw_erase == 1);
    CHECK_STR("line1\nline2", cap_text);

    state.scroll = 2;
    update_text(WIN_TEXT, &state);
    CHECK_STR("line2", cap_text);

    /* out-of-range scroll currently renders from the start of the buffer */
    state.scroll = 3;
    update_text(WIN_TEXT, &state);
    CHECK_STR("line0\nline1\nline2", cap_text);

    state.scroll = 42;
    update_text(WIN_TEXT, &state);
    CHECK_STR("line0\nline1\nline2", cap_text);

    state.scroll = -1;
    update_text(WIN_TEXT, &state);
    CHECK_STR("line0\nline1\nline2", cap_text);

    reset_all();
    state.editbuffer = (char *)"";
    state.scroll = 0;
    update_text(WIN_TEXT, &state);
    CHECK_STR("", cap_text);
    CHECK(text_erased == 1);
}

static void test_update(void) {
    mt state = {0};

    reset_all();
    mock_cols = 16;
    state.path = "file.txt";
    state.editbuffer = (char *)"alpha\nbeta";
    state.scroll = 1;

    update(WIN_STATUS, WIN_TEXT, &state);

    CHECK_STR("file.txt        ", cap_status);
    CHECK_STR("beta", cap_text);
    CHECK(status_saw_erase == 0); /* status window is never erased */
    CHECK(text_saw_erase == 1);
    CHECK(wr_count == 2);
}

/* ---------------------------------------------------------------------- */
/* tests: app_main                                                        */
/* ---------------------------------------------------------------------- */

static void test_main_no_args(void) {
    char *argv[] = {"affap", NULL};

    reset_all();
    CHECK(app_main(1, argv) == 666);
    CHECK(initscr_count == 0);
}

static void test_main_missing_file(void) {
    char *argv[] = {"affap", "/definitely/does/not/exist", NULL};

    reset_all();
    CHECK(app_main(2, argv) == 666);
    CHECK(initscr_count == 1);
    CHECK(endwin_count == 1);
    CHECK(delwin_count == 2);
    CHECK(mousemask_count == 1);
    CHECK(mousemask_count == 1);
}

static void test_main_basic(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){ERR}, 1);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l0\nl1\nl2", cap_text);
    CHECK(initscr_count == 1);
    CHECK(endwin_count == 1);
    CHECK(delwin_count == 2);
    CHECK(keypad_count >= 2);
    CHECK(refresh_count == 1);
    CHECK(mousemask_count == 1);
    CHECK(last_mousemask & BUTTON4_PRESSED);
    CHECK(last_mousemask & BUTTON5_PRESSED);

    unlink(path);
    free(path);
}

static void test_main_scroll_down(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){KEY_MOUSE, ERR}, 2);
    queue_mouse(BUTTON5_PRESSED, OK);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l1\nl2", cap_text);

    unlink(path);
    free(path);
}

static void test_main_scroll_up_at_top(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){KEY_MOUSE, ERR}, 2);
    queue_mouse(BUTTON4_PRESSED, OK);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l0\nl1\nl2", cap_text);

    unlink(path);
    free(path);
}

static void test_main_scroll_clamped_bottom(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};
    int keys[12];

    CHECK(path != NULL);
    reset_all();

    for (int i = 0; i < 11; i++) {
        keys[i] = KEY_MOUSE;
        queue_mouse(BUTTON5_PRESSED, OK);
    }
    keys[11] = ERR;
    queue_keys(keys, 12);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l2", cap_text);

    unlink(path);
    free(path);
}

static void test_main_scroll_roundtrip(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};
    int keys[12];

    CHECK(path != NULL);
    reset_all();

    for (int i = 0; i < 3; i++) {
        keys[i] = KEY_MOUSE;
        queue_mouse(BUTTON5_PRESSED, OK);
    }
    for (int i = 3; i < 11; i++) {
        keys[i] = KEY_MOUSE;
        queue_mouse(BUTTON4_PRESSED, OK);
    }
    keys[11] = ERR;
    queue_keys(keys, 12);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l0\nl1\nl2", cap_text);

    unlink(path);
    free(path);
}

static void test_main_getmouse_error(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){KEY_MOUSE, ERR}, 2);
    queue_mouse(BUTTON5_PRESSED, ERR);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l0\nl1\nl2", cap_text);

    unlink(path);
    free(path);
}

static void test_main_unknown_button(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){KEY_MOUSE, ERR}, 2);
    queue_mouse(BUTTON1_PRESSED, OK);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l0\nl1\nl2", cap_text);

    unlink(path);
    free(path);
}

static void test_main_zero_key_does_not_exit(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){0, KEY_MOUSE, ERR}, 3);
    queue_mouse(BUTTON5_PRESSED, OK);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l1\nl2", cap_text);

    unlink(path);
    free(path);
}

static void test_main_resize(void) {
    char *path = write_temp("l0\nl1\nl2");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();

    resize_pending = 1;
    resize_to_rows = 40;
    resize_to_cols = 120;
    queue_keys((const int[]){KEY_RESIZE, ERR}, 2);

    CHECK(app_main(2, argv) == 0);

    CHECK(newwin_count == 4);
    CHECK(newwin_nlines[0] == 23);
    CHECK(newwin_ncols[0] == 80);
    CHECK(newwin_nlines[1] == 1);
    CHECK(newwin_ncols[1] == 80);

    CHECK(newwin_nlines[2] == 39);
    CHECK(newwin_ncols[2] == 120);
    CHECK(newwin_y[2] == 0);
    CHECK(newwin_x[2] == 0);

    CHECK(newwin_nlines[3] == 1);
    CHECK(newwin_ncols[3] == 120);
    CHECK(newwin_y[3] == 39);
    CHECK(newwin_x[3] == 0);

    /* two initial windows deleted on resize + two resized deleted at cleanup */
    CHECK(delwin_count == 4);
    CHECK_STR("l0\nl1\nl2", cap_text);

    unlink(path);
    free(path);
}

/* ---------------------------------------------------------------------- */
/* runner                                                                 */
/* ---------------------------------------------------------------------- */

int main(void) {
    test_force_direct_term();
    test_fileread();
    test_ioffset_scroll();
    test_line_count();
    test_bootstap_mt();
    test_text_win();
    test_strstate_win();
    test_init_colors();
    test_update_srtstate();
    test_update_text();
    test_update();
    test_main_no_args();
    test_main_missing_file();
    test_main_basic();
    test_main_scroll_down();
    test_main_scroll_up_at_top();
    test_main_scroll_clamped_bottom();
    test_main_scroll_roundtrip();
    test_main_getmouse_error();
    test_main_unknown_button();
    test_main_zero_key_does_not_exit();
    test_main_resize();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
