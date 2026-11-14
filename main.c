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

/// BEGIN EBDS
#define BLOCKSIZE 1024

typedef struct Block {
    char* buf;
    struct Block* next;
    struct Block* prev;
} Block;

typedef struct List {
    Block* head;
    Block* tail;
    char* cached;
} List;

void free_list(List* list);

Block* create(char* data) {
    Block* ne = (Block*)malloc(sizeof(Block));
    if (ne == nullptr) { return nullptr; }
    if (data == nullptr) {
        ne->buf = (char*)calloc(1, 1);
    } else {
        ne->buf = strdup(data);
    }
    if (ne->buf == nullptr) {
        free(ne);
        return nullptr;
    }
    ne->next = nullptr;
    ne->prev = nullptr;
    return ne;
}

List* init(void) {
    List* ne = (List*)calloc(1, sizeof(List));
    return ne;
}

int push(List* list, char* data) {
    if (list == nullptr) { return 0; }
    Block* ne = create(data);
    if (ne == nullptr) { return 0; }
    if (list->tail == nullptr) {
        list->head = list->tail = ne;
    } else {
        ne->prev = list->tail;
        list->tail->next = ne;
        list->tail = ne;
    }
    return 1;
}

static char* list_flatten(List* list) {
    if (list == nullptr) { return nullptr; }
    if (list->cached != nullptr) { return list->cached; }

    size_t total = 0;
    for (Block* b = list->head; b != nullptr; b = b->next) {
        if (b->buf != nullptr) {
            total += strlen(b->buf);
        }
    }

    char* s = (char*)malloc(total + 1);
    if (s == nullptr) { return nullptr; }

    size_t off = 0;
    for (Block* b = list->head; b != nullptr; b = b->next) {
        if (b->buf != nullptr) {
            size_t l = strlen(b->buf);
            memcpy(s + off, b->buf, l);
            off += l;
        }
    }
    s[total] = '\0';

    list->cached = s;
    return s;
}

List* narezka(char* eb) {
    if (eb == nullptr) { return nullptr; }

    List* ro = init();
    if (ro == nullptr) { return nullptr; }

    char curbuf[BLOCKSIZE + 1];
    int cbp = 0;

    for (int i = 0; eb[i] != '\0'; ++i) {
        curbuf[cbp++] = eb[i];
        if (cbp == BLOCKSIZE) {
            curbuf[cbp] = '\0';
            if (push(ro, curbuf) == 0) { free_list(ro); return nullptr; }
            cbp = 0;
        }
    }

    if (cbp > 0 || eb[0] == '\0') {
        curbuf[cbp] = '\0';
        if (push(ro, curbuf) == 0) { free_list(ro); return nullptr; }
    }

    list_flatten(ro);
    return ro;
}

Block* find_from_pos(List* list, int pos) {
    if (list == nullptr || list->head == nullptr || pos < 0) {
        return nullptr;
    }

    size_t cum = 0;
    Block* curr = list->head;

    while (curr != nullptr) {
        size_t len = (curr->buf != nullptr) ? strlen(curr->buf) : 0;

        if (pos < (int)(cum + len)) {
            return curr;
        }

        if (pos == (int)(cum + len)) {
            if (curr->next != nullptr) {
                cum += len;
                curr = curr->next;
                continue;
            }
            return curr;
        }

        cum += len;
        curr = curr->next;
    }

    return list->tail;
}

void chadd(List* list, char* ch, int index) {
    if (list == nullptr || ch == nullptr) { return; }

    char* base = list_flatten(list);
    size_t len = (base != nullptr) ? strlen(base) : 0;

    if (index < 0) { index = 0; }
    if ((size_t)index > len) { index = (int)len; }

    size_t chlen = strlen(ch);

    char* newstr = (char*)malloc(len + chlen + 1);
    if (newstr == nullptr) { return; }

    if (base != nullptr) {
        memcpy(newstr, base, (size_t)index);
    }
    memcpy(newstr + index, ch, chlen);
    if (base != nullptr) {
        memcpy(newstr + index + chlen, base + index, len - (size_t)index + 1);
    } else {
        newstr[index + chlen] = '\0';
    }

    List* tmp = narezka(newstr);
    free(newstr);

    if (tmp == nullptr || tmp->head == nullptr ||
        tmp->cached == nullptr || strlen(tmp->cached) != len + chlen) {
        // rebuild failed: keep the old list intact
        free_list(tmp);
        return;
    }

    Block* b = list->head;
    while (b != nullptr) {
        Block* nxt = b->next;
        free(b->buf);
        free(b);
        b = nxt;
    }

    free(list->cached);

    list->head = tmp->head;
    list->tail = tmp->tail;
    list->cached = tmp->cached;
    free(tmp);
}

void free_list(List* list) {
    if (list == nullptr) { return; }
    Block* b = list->head;
    while (b != nullptr) {
        Block* nxt = b->next;
        free(b->buf);
        free(b);
        b = nxt;
    }
    free(list->cached);
    free(list);
}

/// BEGIN MAIN

typedef struct {
    int max_x;
    int max_y;

    int cur_x;
    int cur_y;
    int scroll;

    List* editbuffer;
    const char* path;
} mt;

// utility
char* fileread(const char* f) {
    FILE* file = fopen(f, "rb");
    if (file == nullptr) {
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (length < 0) {
        fclose(file);
        return nullptr;
    }

    char* buffer = (char*)malloc(length + 1);
    if (buffer == nullptr) {
        fclose(file);
        return nullptr;
    }

    size_t read_bytes = fread(buffer, 1, length, file);
    buffer[read_bytes] = '\0';

    fclose(file);
    return buffer;
}

void filewrite(const char* f, char* eb) {
    FILE* file = fopen(f, "w");
    if (file == nullptr) {
        return;
    }
    if (eb != nullptr) {
        fputs(eb, file);
    }
    fclose(file);
}

void bootstap_mt(mt* state, WINDOW* w, const char* f) {
    int row, col;
    getmaxyx(w, row, col);
    char* raw = fileread(f);
    *state = (mt){
        .max_x = col,
        .max_y = row,
        .cur_y = 0,
        .cur_x = 0,
        .path = f,
        .editbuffer = narezka(raw),
    };
    free(raw);
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
    if (state == nullptr || state->editbuffer == nullptr || stn <= 0) {
        return 0;
    }

    char* eb = list_flatten((List*)state->editbuffer);
    if (eb == nullptr) {
        return 0;
    }

    int curstn = 0;
    for (int i = 0; eb[i] != '\0'; ++i) {
        if (eb[i] == '\n' && ++curstn == stn) {
            return (size_t)(i + 1); // next char after \n
        }
    }
    return 0;
}

int line_count(const mt* state) {
    if (state == nullptr || state->editbuffer == nullptr) {
        return 1;
    }

    char* eb = list_flatten((List*)state->editbuffer);
    if (eb == nullptr) {
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
    char* flat = (state != nullptr && state->editbuffer != nullptr)
                     ? list_flatten((List*)state->editbuffer)
                     : nullptr;
    size_t off = ioffset_scroll(state, state->scroll);
    if (flat != nullptr && off <= strlen(flat)) {
        mvwprintw(w, 0, 0, "%s", flat + off);
    } else {
        mvwprintw(w, 0, 0, "%s", "");
    }
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
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr);
    MEVENT event;

    init_colors();

    WINDOW* text = text_win();
    WINDOW* srtstate = strstate_win();
    keypad(text, TRUE);
    refresh();

    bootstap_mt(&state, text, path);
    if (state.editbuffer == nullptr) goto naxyi;

    update(srtstate, text, &state);

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
                getmaxyx(text, max_y, max_x);
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
        mvwchgat(text, state.cur_y, state.cur_x, 1, A_REVERSE, 0, NULL);
        wrefresh(text);
    }

    // getch();

    // cleanup
    delwin(text);
    delwin(srtstate);
    endwin();
    if (state.editbuffer != nullptr) free_list((List*)state.editbuffer);
    return 0;

naxyi:
    delwin(text);
    delwin(srtstate);
    endwin();
    if (state.editbuffer != nullptr) free_list((List*)state.editbuffer);
    return 666;
}
