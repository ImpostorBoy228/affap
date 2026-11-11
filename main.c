#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <ncurses.h>


#define STATUS_FG 0x271F30
#define STATUS_BG 0x7D82B8
#define TEXT_FG 0xFCF7FF
#define TEXT_BG 0x271F30

#define PAIR_STATUS 1
#define PAIR_TEXT   2

static int colors_on = 0;


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

static short rgb_component(int rgb, int shift) {
    return (short)(((rgb >> shift) & 0xff) * 1000 / 255);
}

static int nearest_256(int rgb) {
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    int r = (rgb >> 16) & 0xff;
    int g = (rgb >> 8) & 0xff;
    int b = rgb & 0xff;
    int best = 16, best_d = -1;

    for (int i = 16; i < 256; i++) {
        int cr, cg, cb;
        if (i < 232) {
            int n = i - 16;
            cr = lv[n / 36];
            cg = lv[(n / 6) % 6];
            cb = lv[n % 6];
        } else {
            cr = cg = cb = 8 + (i - 232) * 10;
        }
        int d = (r - cr) * (r - cr) + (g - cg) * (g - cg) + (b - cb) * (b - cb);
        if (best_d < 0 || d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static int init_custom_pair(short pair, int fg, int bg) {
#if defined(NCURSES_EXT_COLORS)
    if (init_extended_pair(pair, fg, bg) == OK) {
        return OK;
    }
#endif
    if (COLORS >= 256) {
        if (can_change_color()) {
            short fg_slot = 16 + pair * 2;
            short bg_slot = fg_slot + 1;
            if (init_color(fg_slot, rgb_component(fg, 16), rgb_component(fg, 8), rgb_component(fg, 0)) == OK &&
                init_color(bg_slot, rgb_component(bg, 16), rgb_component(bg, 8), rgb_component(bg, 0)) == OK &&
                init_pair(pair, fg_slot, bg_slot) == OK) {
                return OK;
            }
        }
        return init_pair(pair, nearest_256(fg), nearest_256(bg));
    }
    if (COLORS >= 8) {
        short bg = (pair == PAIR_STATUS) ? COLOR_BLUE : COLOR_BLACK;
        return init_pair(pair, COLOR_WHITE, bg);
    }
    if (COLORS >= 2) {
        return init_pair(pair, 1, 0);
    }
    return ERR;
}

static void init_colors(void) {
    if (!has_colors()) {
        return;
    }

    if (start_color() != OK) {
        return;
    }

    use_default_colors();

    if (init_custom_pair(PAIR_STATUS, STATUS_FG, STATUS_BG) != OK ||
        init_custom_pair(PAIR_TEXT, TEXT_FG, TEXT_BG) != OK) {
        return;
    }

    colors_on = 1;
}

WINDOW* text_win(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    WINDOW *text_win = newwin(max_y - 1, max_x, 0, 0);

    if (colors_on) {
        wbkgd(text_win, ' ' | COLOR_PAIR(PAIR_TEXT));
    }

    return text_win;
}

WINDOW* strstate_win(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    WINDOW *status_win = newwin(1, max_x, max_y - 1, 0);

    if (colors_on) {
        wbkgd(status_win, ' ' | COLOR_PAIR(PAIR_STATUS));
    } else {
        wbkgd(status_win, A_REVERSE);
    }

    return status_win;
}

void update_srtstate(WINDOW* w, const char* path) {
    if (colors_on) wattron(w, COLOR_PAIR(PAIR_STATUS));
    mvwprintw(w, 0, 0, "%-*s", getmaxx(w), path);
    if (colors_on) wattroff(w, COLOR_PAIR(PAIR_STATUS));
    wrefresh(w);
}

void update_text(WINDOW* w, const char* eb) {
    if (colors_on) wattron(w, COLOR_PAIR(PAIR_TEXT));
    mvwprintw(w, 0, 0, "%s", eb);
    if (colors_on) wattroff(w, COLOR_PAIR(PAIR_TEXT));
    wrefresh(w);
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
