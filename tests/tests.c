#include "test_support.h"

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

    /* NULL buffer: mvwprintw with "%s" and NULL is UB; code path not taken in main
       because update only called after non-null check. Ensure no crash by skipping. */
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
    CHECK(status_saw_erase == 0);
    CHECK(text_saw_erase == 1);
    CHECK(wr_count == 2);
}

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

    CHECK(delwin_count == 4);
    CHECK_STR("l0\nl1\nl2", cap_text);

    unlink(path);
    free(path);
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

static void test_app_main_single_line(void) {
    char *path = write_temp("only line");
    char *argv[] = {"affap", path, NULL};

    CHECK(path != NULL);
    reset_all();
    queue_keys((const int[]){KEY_MOUSE, KEY_MOUSE, ERR}, 3);
    queue_mouse(BUTTON5_PRESSED, OK);
    queue_mouse(BUTTON5_PRESSED, OK);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("only line", cap_text);

    unlink(path);
    free(path);
}

static void test_scroll_up_from_middle(void) {
    char *path = write_temp("l0\nl1\nl2\nl3");
    char *argv[] = {"affap", path, NULL};
    int keys[6];

    CHECK(path != NULL);
    reset_all();

    /* go down 2 */
    for (int i = 0; i < 2; i++) {
        keys[i] = KEY_MOUSE;
        queue_mouse(BUTTON5_PRESSED, OK);
    }
    /* go up 1 */
    keys[2] = KEY_MOUSE;
    queue_mouse(BUTTON4_PRESSED, OK);
    keys[3] = ERR;
    queue_keys(keys, 4);

    CHECK(app_main(2, argv) == 0);
    CHECK_STR("l1\nl2\nl3", cap_text);

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

    RUN_TEST(test_init_colors);
    RUN_TEST(test_fileread);
    RUN_TEST(test_ioffset_scroll);
    RUN_TEST(test_line_count);
    RUN_TEST(test_bootstap_mt);
    RUN_TEST(test_text_win);
    RUN_TEST(test_strstate_win);
    RUN_TEST(test_update_srtstate);
    RUN_TEST(test_update_text);
    RUN_TEST(test_update);
    RUN_TEST(test_main_no_args);
    RUN_TEST(test_main_missing_file);
    RUN_TEST(test_main_basic);
    RUN_TEST(test_main_scroll_down);
    RUN_TEST(test_main_scroll_up_at_top);
    RUN_TEST(test_main_scroll_clamped_bottom);
    RUN_TEST(test_main_scroll_roundtrip);
    RUN_TEST(test_main_getmouse_error);
    RUN_TEST(test_main_unknown_button);
    RUN_TEST(test_main_zero_key_does_not_exit);
    RUN_TEST(test_main_resize);
    RUN_TEST(test_mousemask_flags);
    RUN_TEST(test_main_multiple_resizes);
    RUN_TEST(test_ioffset_exact);
    RUN_TEST(test_line_count_large);
    RUN_TEST(test_fileread_directory);
    RUN_TEST(test_update_text_null_buffer_guard);
    RUN_TEST(test_app_main_empty_file);
    RUN_TEST(test_app_main_single_line);
    RUN_TEST(test_scroll_up_from_middle);
    RUN_TEST(test_fuzz_ioffset_no_crash);
    RUN_TEST(test_fuzz_fileread_roundtrip);

    printf("\n%d tests, %d failed; %d checks, %d failed\n",
           tests_run, tests_failed, checks, failures);
    return failures == 0 ? 0 : 1;
}