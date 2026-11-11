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
#define PAIR_TEXT   2


typedef struct {
    int max_x;
    int max_y;

    int cur_x;
    int cur_y;

    char* editbuffer;
} mt;

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

mt bootstap_mt(WINDOW* w, const char* f) {
    int row, col;
    getmaxyx(w, row, col);
    mt res = {.max_x = col, .max_y = row, .cur_y = 0, .cur_x = 0, .editbuffer = fileread(f)};
    return res;
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

void update_srtstate(WINDOW* w, const char* path) {
    wattron(w, COLOR_PAIR(PAIR_STATUS));
    mvwprintw(w, 0, 0, "%-*s", getmaxx(w), path);
    wattroff(w, COLOR_PAIR(PAIR_STATUS));
    wrefresh(w);
}

void update_text(WINDOW* w, const char* eb) {
    wattron(w, COLOR_PAIR(PAIR_TEXT));
    mvwprintw(w, 0, 0, "%s", eb);
    wattroff(w, COLOR_PAIR(PAIR_TEXT));
    wrefresh(w);
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

    init_colors();

    WINDOW* text = text_win();
    WINDOW* srtstate = strstate_win();
    keypad(text, TRUE);
    refresh();

    state = bootstap_mt(text, path);
    if (state.editbuffer==NULL) goto naxyi;

    update_srtstate(srtstate, path);

    update_text(text, state.editbuffer);
    wrefresh(text);
    getch();

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
