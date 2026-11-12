#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ncurses.h>


#define STATUS_FG 235
#define STATUS_BG 103
#define TEXT_FG 231
#define TEXT_BG 235

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

void update_srtstate(WINDOW* w, const mt* state) {
    wattron(w, COLOR_PAIR(PAIR_STATUS));
    mvwprintw(w, 0, 0, "%-*s", getmaxx(w), state->path); // пля что за фокусы
    wattroff(w, COLOR_PAIR(PAIR_STATUS));
    wrefresh(w);
}

void update_text(WINDOW* w, const mt* state) {
    werase(w);
    wattron(w, COLOR_PAIR(PAIR_TEXT));
    mvwprintw(w, 0, 0, "%s",
    state->editbuffer + ioffset_scroll(state, state->scroll));
    wattroff(w, COLOR_PAIR(PAIR_TEXT));
    wrefresh(w);
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
