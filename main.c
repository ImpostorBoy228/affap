#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ncurses.h>


#define STATUS_FG 0x271F30
#define STATUS_BG 0x7D82B8
#define TEXT_FG 0xFCF7FF
#define TEXT_BG 0x271F30

#define PAIR_STATUS 1
#define PAIR_TEXT 2

#define SCROLL_S 1

typedef struct {
    int max_x;
    int max_y;

    int cur_x;
    int cur_y;
    int scroll;

    char* editbuffer;
    const char* path;
} mt;

// utility
char* fileread(const char* f) {
    FILE* file = fopen(f, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (length < 0) {
        fclose(file);
        return NULL;
    }

    char* buffer = (char*)malloc(length + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, length, file);
    buffer[read_bytes] = '\0';

    fclose(file);
    return buffer;
}

void bootstap_mt(mt* state, WINDOW* w, const char* f) {
    int row, col;
    getmaxyx(w, row, col);
    *state = (mt){
        .max_x = col,
        .max_y = row,
        .cur_y = 0,
        .cur_x = 0,
        .path = f,
        .editbuffer = fileread(f),
    };
}

static void init_custom_pair(short pair, int fg, int bg) {
    init_extended_pair(pair, fg, bg);
}

static void init_colors(void) {
    start_color();
    use_default_colors();

    init_custom_pair(PAIR_STATUS, STATUS_FG, STATUS_BG);
    init_custom_pair(PAIR_TEXT, TEXT_FG, TEXT_BG);
}

static void force_direct_term(void) {
    const char *term = getenv("TERM");
    const char *colorterm = getenv("COLORTERM");

    if (term == NULL) {
        return;
    }

    if (strstr(term, "foot") != NULL) {
        setenv("TERM", "foot-direct", 1);
    } else if (strstr(term, "tmux") != NULL) {
        setenv("TERM", "tmux-direct", 1);
    } else if (strstr(term, "xterm") != NULL) {
        setenv("TERM", "xterm-direct", 1);
    } else if (colorterm != NULL &&
               (strcmp(colorterm, "truecolor") == 0 ||
                strcmp(colorterm, "24bit") == 0)) {
        setenv("TERM", "xterm-direct", 1);
    }
}

size_t ioffset_scroll(const mt* state, int stn) {
    // returns offset of the first char of line stn (0-based)
    const char* eb = state->editbuffer;
    if (eb == NULL || stn <= 0) {
        return 0;
    }

    int curstn = 0;
    for (int i = 0; eb[i] != '\0'; ++i) {
        if (eb[i] == '\n' && ++curstn == stn) {
            return i + 1; // next char after \n
        }
    }
    return 0;
}

int line_count(const mt* state) {
    const char* eb = state->editbuffer;
    if (eb == NULL) {
        return 1;
    }

    int lines = 1;
    for (const char* p = eb; *p != '\0'; ++p) {
        if (*p == '\n') {
            lines++;
        }
    }
    return lines;
}

// ncurses helpers
WINDOW* text_win(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    WINDOW *text_win = newwin(max_y - 1, max_x, 0, 0);

    wbkgd(text_win, ' ' | COLOR_PAIR(PAIR_TEXT));

    return text_win;
}

WINDOW* strstate_win(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    WINDOW *status_win = newwin(1, max_x, max_y - 1, 0);

    wbkgd(status_win, ' ' | COLOR_PAIR(PAIR_STATUS));

    return status_win;
}

#define ANSI_STATUS "\033[38;2;39;31;48;48;2;125;130;184m"
#define ANSI_TEXT   "\033[38;2;252;247;255;48;2;39;31;48m"
#define ANSI_RESET  "\033[0m"

/* Emit ANSI directly so the terminal interprets colors instead of printing escapes. */
void update_srtstate(WINDOW* w, const mt* state) {
    (void)w;
    printf("\033[%d;1H" ANSI_STATUS "%-*s" ANSI_RESET "\033[K",
           state->max_y + 1, state->max_x, state->path);
    fflush(stdout);
}

void update_text(WINDOW* w, const mt* state) {
    (void)w;
    for (int r = 1; r <= state->max_y; ++r) {
        printf("\033[%d;1H\033[2K", r);
    }
    printf("\033[1;1H" ANSI_TEXT "%s" ANSI_RESET, state->editbuffer + ioffset_scroll(state, state->scroll));
    fflush(stdout);
}

void update(WINDOW* strstate, WINDOW* text, mt* state) {
    update_srtstate(strstate, state);
    update_text(text, state);
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    mt state = {0};

    char* path;
    if (argc>1) {
        path = argv[1];
    } else { return 666; }


    force_direct_term();

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    MEVENT event;

    init_colors();

    WINDOW* text = text_win();
    WINDOW* srtstate = strstate_win();
    keypad(text, TRUE);
    refresh();

    bootstap_mt(&state, text, path);
    if (state.editbuffer==NULL) goto naxyi;

    update(srtstate, text, &state);

    // TODO implement vim
    int ch;
    while ((ch = getch()) != ERR) {
        switch (ch) {
            case KEY_RESIZE: {
                delwin(text);
                delwin(srtstate);

                text = text_win();
                srtstate = strstate_win();
                keypad(text, TRUE);

                int max_y, max_x;
                getmaxyx(stdscr, max_y, max_x);
                state.max_y = max_y;
                state.max_x = max_x;
                break;
            }
            case KEY_MOUSE:
                if (getmouse(&event)==OK) {
                    if (event.bstate & BUTTON4_PRESSED) {
                        state.scroll -= SCROLL_S;
                        if (state.scroll < 0) {
                            state.scroll = 0;
                        }
                    } else if (event.bstate & BUTTON5_PRESSED) {
                        int last = line_count(&state) - 1;
                        state.scroll += SCROLL_S;
                        if (state.scroll > last) {
                            state.scroll = last;
                        }
                    }
                }
        }
        update(srtstate, text, &state);
    }

    // getch();

    // cleanup
    delwin(text);
    delwin(srtstate);
    endwin();
    if (state.editbuffer != NULL) free(state.editbuffer);
    return 0;

naxyi:
    delwin(text);
    delwin(srtstate);
    endwin();
    if (state.editbuffer != NULL) free(state.editbuffer);
    return 666;
}
