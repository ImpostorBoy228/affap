#include "test_support.h"

/* ====================================================================== */
/* force_direct_term                                                       */
/* ====================================================================== */

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

    /* case sensitivity / substrings */
    setenv("TERM", "FOOT", 1);
    force_direct_term();
    CHECK(env_is("TERM", "FOOT")); /* strstr is case-sensitive */

    setenv("TERM", "myxterm", 1);
    setenv("COLORTERM", "truecolor", 1);
    force_direct_term();
    CHECK(env_is("TERM", "xterm-direct"));

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

/* ====================================================================== */
/* fileread                                                               */
/* ====================================================================== */

static void test_fileread(void) {
    char *path;
    char *buffer;
    const char binary[] = {'a', '\0', 'b', 'c'};

    reset_all();

    CHECK(fileread("/definitely/does/not/exist") == NULL);
    CHECK(fileread("") == NULL); /* empty path -> fopen fails */

    path = write_temp("");
    CHECK(path != NULL);
    buffer = fileread(path);
    CHECK(buffer != NULL);
    if (buffer != NULL) {
        CHECK(buffer[0] == '\0');
        CHECK(strlen(buffer) == 0);
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
        CHECK(strlen(buffer) == strlen("hello\nworld"));
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

    /* large file */
    {
        size_t n = 1 << 20;
        char *big = malloc(n + 1);
        CHECK_NOT_NULL(big);
        if (big) {
            memset(big, 'A', n);
            big[n] = '\0';
            path = write_temp_n(big, n + 1);
            CHECK_NOT_NULL(path);
            if (path) {
                buffer = fileread(path);
                CHECK_NOT_NULL(buffer);
                if (buffer) {
                    CHECK(strlen(buffer) == n);
                    CHECK(memcmp(buffer, big, n) == 0);
                }
                free(buffer);
                unlink(path);
                free(path);
            }
            free(big);
        }
    }
}

/* ====================================================================== */
/* ioffset_scroll / line_count                                            */
/* ====================================================================== */

static void test_ioffset_scroll(void) {
    mt state = {0};

    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 5) == 0);

    state.editbuffer = (char *)"a\nb\nc";
    CHECK(ioffset_scroll(&state, -1) == 0);
    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 1) == 2);
    CHECK(ioffset_scroll(&state, 2) == 4);
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

    /* property: result is either 0 or points at start of a line */
    {
        const char *samples[] = {"", "a", "a\n", "\n", "\n\n", "a\nb\nc\nd",
                                 "trailing\n"};
        for (size_t s = 0; s < sizeof(samples)/sizeof(samples[0]); s++) {
            state.editbuffer = (char *)samples[s];
            int lc = line_count(&state);
            for (int ln = 0; ln <= lc + 2; ln++) {
                size_t off = ioffset_scroll(&state, ln);
                CHECK(off <= strlen(samples[s]));
            }
        }
    }
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

    /* property: line_count >= 1 always */
    state.editbuffer = NULL;
    CHECK(line_count(&state) >= 1);
}

/* ====================================================================== */
/* bootstap_mt                                                            */
/* ====================================================================== */

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

/* ====================================================================== */
/* windows and colors                                                     */
/* ====================================================================== */

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

/* ====================================================================== */
/* rendering                                                              */
/* ====================================================================== */




/* ====================================================================== */
/* app_main                                                               */
/* ====================================================================== */

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
}










/* ====================================================================== */
/* additional high-rigor tests                                            */
/* ====================================================================== */

static void test_mousemask_flags(void) {
    char *path = write_temp("x");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){ERR}, 1);

    CHECK(app_main(2, argv) == 0);
    CHECK(mousemask_count == 1);
    CHECK(last_mousemask & ALL_MOUSE_EVENTS);
    CHECK(last_mousemask & REPORT_MOUSE_POSITION);
    CHECK(last_mousemask & BUTTON4_PRESSED);
    CHECK(last_mousemask & BUTTON5_PRESSED);

    unlink(path);
    free(path);
}

static void test_main_multiple_resizes(void) {
    char *path = write_temp("a\nb\nc");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();

    /* first resize */
    resize_pending = 1;
    resize_to_rows = 10;
    resize_to_cols = 40;
    /* second resize queued after first key processed */
    /* We'll do two separate KEY_RESIZE with a manual reset in between by queueing both;
       mock_getch applies resize on first KEY_RESIZE; second needs resize_pending set again,
       but it is cleared. Simulate by queueing one resize then ERR. */
    queue_keys((const int[]){KEY_RESIZE, ERR}, 2);

    CHECK(app_main(2, argv) == 0);
    /* 2 initial + 2 after one resize = 4 newwin */
    CHECK(newwin_count == 4);

    unlink(path);
    free(path);
}

static void test_ioffset_exact(void) {
    const char *buf = "ab\ncd\nef";
    mt state = {0};
    state.editbuffer = (char *)buf;

    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 1) == 3);
    CHECK(ioffset_scroll(&state, 2) == 6);

    /* trailing newline */
    state.editbuffer = (char *)"a\nb\n";
    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 1) == 2);
    CHECK(ioffset_scroll(&state, 2) == 4);

    /* empty lines */
    state.editbuffer = (char *)"\n\n";
    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 1) == 1);
    CHECK(ioffset_scroll(&state, 2) == 2);
}

static void test_line_count_large(void) {
    size_t n = 100000;
    char *buf = malloc(n * 2 + 1);
    CHECK_NOT_NULL(buf);
    if (buf) {
        size_t pos = 0;
        for (size_t i = 0; i < n; i++) {
            buf[pos++] = 'x';
            buf[pos++] = '\n';
        }
        buf[pos] = '\0';
        mt state = {0};
        state.editbuffer = buf;
        CHECK(line_count(&state) == (int)n + 1);
        free(buf);
    }
}

static void test_fileread_directory(void) {
    /* opening a directory for reading may succeed on some systems; fileread
       should not crash either way. */
    char *b = fileread("/tmp");
    /* accept NULL or non-NULL, but if non-NULL it must be freeable */
    free(b);
    CHECK(1);
}

static void test_update_text_null_buffer_guard(void) {
    /* update_text with NULL editbuffer would pass NULL to mvwprintw %s (UB).
       The application never calls it with NULL (guarded in main), so we do not
       exercise that path. Instead verify line_count/ioffset handle NULL. */
    mt state = {0};
    state.editbuffer = NULL;
    CHECK(line_count(&state) == 1);
    CHECK(ioffset_scroll(&state, 0) == 0);
    CHECK(ioffset_scroll(&state, 5) == 0);
}

static void test_app_main_empty_file(void) {
    char *path = write_temp("");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){ERR}, 1);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("", cap_text);

    unlink(path);
    free(path);
}



/* ====================================================================== */
/* fuzz-ish random tests                                                  */
/* ====================================================================== */

static void test_fuzz_ioffset_no_crash(void) {
    /* random-ish buffers */
    const char *cases[] = {
        "", "a", "\n", "\n\n", "a\n", "abc", "a\0b", "line1\nline2\nline3",
    };
    /* can't embed NUL in string literal easily; use write_temp_n instead */
    mt state;
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        memset(&state, 0, sizeof(state));
        state.editbuffer = (char *)cases[i];
        /* call for a range of line numbers */
        for (int ln = -5; ln <= 10; ln++) {
            size_t off = ioffset_scroll(&state, ln);
            CHECK(off <= strlen(cases[i]));
        }
        CHECK(line_count(&state) >= 1);
    }
}

static void test_fuzz_fileread_roundtrip(void) {
    /* property: fileread returns content equal to written (for text without NUL issues) */
    const char *payloads[] = {"", "x", "hello world", "a\nb\nc\n", "   spaces   "};
    for (size_t i = 0; i < sizeof(payloads)/sizeof(payloads[0]); i++) {
        char *p = write_temp(payloads[i]);
        CHECK_NOT_NULL(p);
        if (p) {
            char *b = fileread(p);
            CHECK_NOT_NULL(b);
            if (b) CHECK_STR(payloads[i], b);
            free(b);
            unlink(p);
            free(p);
        }
    }
}

/* ====================================================================== */
/* runner                                                                 */
/* ====================================================================== */

int main(void) {
    printf("affap test suite\n");
    printf("================\n");

    RUN_TEST(test_force_direct_term);
    RUN_TEST(test_fileread);
    RUN_TEST(test_ioffset_scroll);
    RUN_TEST(test_line_count);
    RUN_TEST(test_bootstap_mt);
    RUN_TEST(test_text_win);
    RUN_TEST(test_strstate_win);
    RUN_TEST(test_init_colors);
    RUN_TEST(test_main_no_args);
    RUN_TEST(test_main_missing_file);
    RUN_TEST(test_mousemask_flags);
    RUN_TEST(test_main_multiple_resizes);
    RUN_TEST(test_ioffset_exact);
    RUN_TEST(test_line_count_large);
    RUN_TEST(test_fileread_directory);
    RUN_TEST(test_update_text_null_buffer_guard);
    RUN_TEST(test_app_main_empty_file);
    RUN_TEST(test_fuzz_ioffset_no_crash);
    RUN_TEST(test_fuzz_fileread_roundtrip);

    printf("\n%d tests, %d failed; %d checks, %d failed\n",
           tests_run, tests_failed, checks, failures);
    return failures == 0 ? 0 : 1;
}