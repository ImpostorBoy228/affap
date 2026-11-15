#include <stdbool.h>
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
    bool append_mode;

    int max_x;
    int max_y;

    int cur_x;
    int cur_y;
    int curpos;
    int scroll;

    List* editbuffer;
    const char* path;
} mt;

static void sync_cursor(mt* state);

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
        .curpos = 0,
        .scroll = 0,
        .append_mode = false,
        .path = f,
        .editbuffer = narezka(raw),
    };
    free(raw);
    sync_cursor(state);
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


static int utf8_seq_len(unsigned char c) {
    if (c < 0x80) { return 1; }
    if ((c & 0xE0) == 0xC0) { return 2; }
    if ((c & 0xF0) == 0xE0) { return 3; }
    if ((c & 0xF8) == 0xF0) { return 4; }
    return 1;
}

static unsigned int utf8_decode(const char* s, int len) {
    if (len <= 0) { return 0; }
    unsigned char b0 = (unsigned char)s[0];
    if (len == 1) { return b0; }
    if (len == 2) {
        return ((b0 & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
    }
    if (len == 3) {
        return ((b0 & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) |
               ((unsigned char)s[2] & 0x3F);
    }
    return ((b0 & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) |
           (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
}

static int codepoint_width(unsigned int cp) {
    if (cp == 0) { return 0; }
    if (cp < 0x20 || cp == 0x7F) { return 0; }
    if (cp >= 0x1100 && (cp <= 0x115F || cp == 0x2329 || cp == 0x232A ||
        (cp >= 0x2E80 && cp <= 0x321E && cp != 0x303F) ||
        (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0xA4C6) ||
        (cp >= 0xA960 && cp <= 0xA97C) ||
        (cp >= 0xAC00 && cp <= 0xD7FB) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF01 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1B000 && cp <= 0x1B001) ||
        (cp >= 0x1F200 && cp <= 0x1F251) ||
        (cp >= 0x20000 && cp <= 0x3FFFD))) {
        return 2;
    }
    return 1;
}

static void pos_to_lc(const char* s, int pos, int* line, int* col) {
    if (line != nullptr) { *line = 0; }
    if (col != nullptr) { *col = 0; }
    if (s == nullptr || pos < 0) {
        return;
    }

    size_t len = strlen(s);
    if ((size_t)pos > len) {
        pos = (int)len;
    }

    int l = 0;
    int c = 0;
    int i = 0;
    while (i < pos && s[i] != '\0') {
        if (s[i] == '\n') {
            ++l;
            c = 0;
            ++i;
        } else if (s[i] == '\t') {
            c = (c / 8 + 1) * 8;
            ++i;
        } else {
            int sl = utf8_seq_len((unsigned char)s[i]);
            if (i + sl > pos) {
                break;
            }
            unsigned int cp = utf8_decode(s + i, sl);
            c += codepoint_width(cp);
            i += sl;
        }
    }
    if (line != nullptr) { *line = l; }
    if (col != nullptr) { *col = c; }
}

static int line_visual_width(const char* p) {
    int c = 0;
    while (p != nullptr && *p != '\0' && *p != '\n') {
        if (*p == '\t') {
            c = (c / 8 + 1) * 8;
            ++p;
        } else {
            int sl = utf8_seq_len((unsigned char)*p);
            unsigned int cp = utf8_decode(p, sl);
            c += codepoint_width(cp);
            p += sl;
        }
    }
    return c;
}

static int visual_rows_upto(const char* eb, int target_line, int max_x) {
    if (eb == nullptr || target_line <= 0 || max_x <= 0) {
        return 0;
    }
    int total = 0;
    int line = 0;
    const char* p = eb;
    while (line < target_line && *p != '\0') {
        int w = line_visual_width(p);
        int r = (w == 0) ? 1 : ((w + max_x - 1) / max_x);
        total += r;
        while (*p != '\0' && *p != '\n') { ++p; }
        if (*p == '\n') { ++p; }
        ++line;
    }
    return total;
}

static int visual_col_to_offset(const char* line, int target_col) {
    int c = 0;
    int i = 0;
    if (line == nullptr || target_col <= 0) { return 0; }
    while (line[i] != '\0' && line[i] != '\n') {
        int w = 0;
        int sl = 1;
        if (line[i] == '\t') {
            w = (c / 8 + 1) * 8 - c;
            sl = 1;
        } else {
            sl = utf8_seq_len((unsigned char)line[i]);
            unsigned int cp = utf8_decode(line + i, sl);
            w = codepoint_width(cp);
        }
        if (c + w > target_col) { break; }
        c += w;
        i += sl;
    }
    return i;
}

static void sync_cursor(mt* state) {
    if (state == nullptr) { return; }

    char* eb = list_flatten((List*)state->editbuffer);
    size_t len = (eb != nullptr) ? strlen(eb) : 0;

    if (state->curpos < 0) { state->curpos = 0; }
    if ((size_t)state->curpos > len) { state->curpos = (int)len; }

    int line = 0;
    int col = 0;
    pos_to_lc(eb, state->curpos, &line, &col);

    if (state->max_y <= 0) { state->max_y = 1; }
    if (state->max_x <= 0) { state->max_x = 1; }

    int Vcursor = visual_rows_upto(eb, line, state->max_x) + (col / state->max_x);

    if (line < state->scroll) {
        state->scroll = line;
    }
    while (state->scroll < line &&
           visual_rows_upto(eb, state->scroll, state->max_x) + state->max_y <= Vcursor) {
        ++state->scroll;
    }
    if (visual_rows_upto(eb, state->scroll, state->max_x) + state->max_y <= Vcursor) {
        state->scroll = line;
    }
    if (state->scroll < 0) { state->scroll = 0; }

    int total = line_count(state);
    if (total < 1) { total = 1; }
    if (state->scroll > total - 1) {
        state->scroll = total - 1;
    }
    if (state->scroll < 0) { state->scroll = 0; }

    int Vscroll = visual_rows_upto(eb, state->scroll, state->max_x);
    state->cur_y = Vcursor - Vscroll;
    state->cur_x = col % state->max_x;

    if (state->cur_y < 0) { state->cur_y = 0; }
    if (state->cur_y >= state->max_y) { state->cur_y = state->max_y - 1; }
    if (state->cur_x < 0) { state->cur_x = 0; }
    if (state->cur_x >= state->max_x) { state->cur_x = state->max_x - 1; }
}

void save(mt* state) {
    if (state == nullptr || state->path == nullptr || state->editbuffer == nullptr) {
        return;
    }
    char* flat = list_flatten((List*)state->editbuffer);
    if (flat == nullptr) {
        return;
    }
    filewrite(state->path, flat);
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
        char* src = flat + off;
        size_t srclen = strlen(src);
        char* buf = (char*)malloc(srclen + 1);
        if (buf != nullptr) {
            size_t j = 0;
            for (size_t i = 0; src[i] != '\0'; ++i) {
                unsigned char ch = (unsigned char)src[i];
                if (ch == '\t' || ch == '\n') {
                    buf[j++] = (char)ch;
                } else if (ch < 32 || ch == 127) {
                    continue;
                } else {
                    buf[j++] = (char)ch;
                }
            }
            buf[j] = '\0';
            mvwprintw(w, 0, 0, "%s", buf);
            free(buf);
        } else {
            mvwprintw(w, 0, 0, "%s", src);
        }
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
    mvwchgat(text, state.cur_y, state.cur_x, 1, A_REVERSE, 0, nullptr);
    wrefresh(text);

    int ch;
    while ((ch = getch()) != ERR) {
        if (ch == 3) { // C-c
            goto naxyi;
        } else if (ch == KEY_RESIZE) {
            delwin(text);
            delwin(srtstate);

            text = text_win();
            srtstate = strstate_win();
            keypad(text, TRUE);

            int max_y, max_x;
            getmaxyx(text, max_y, max_x);
            state.max_y = max_y;
            state.max_x = max_x;
            sync_cursor(&state);
        } else if (ch == KEY_MOUSE) {
            if (getmouse(&event) == OK) {
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
                char* eb = list_flatten((List*)state.editbuffer);
                int line = 0;
                int col = 0;
                pos_to_lc(eb, state.curpos, &line, &col);
                if (state.max_x <= 0) { state.max_x = 1; }
                if (state.max_y <= 0) { state.max_y = 1; }
                int Vcursor = visual_rows_upto(eb, line, state.max_x) + (col / state.max_x);
                int Vscroll = visual_rows_upto(eb, state.scroll, state.max_x);
                state.cur_y = Vcursor - Vscroll;
                state.cur_x = col % state.max_x;
                if (state.cur_y < 0) { state.cur_y = 0; }
                if (state.cur_y >= state.max_y) { state.cur_y = state.max_y - 1; }
                if (state.cur_x < 0) { state.cur_x = 0; }
                if (state.cur_x >= state.max_x) { state.cur_x = state.max_x - 1; }
            }
        } else if (ch == KEY_LEFT) {
            char* eb = list_flatten((List*)state.editbuffer);
            if (state.curpos > 0) {
                --state.curpos;
                while (state.curpos > 0 && eb != nullptr &&
                       (unsigned char)eb[state.curpos] >= 0x80 &&
                       (unsigned char)eb[state.curpos] <= 0xBF) {
                    --state.curpos;
                }
            }
            sync_cursor(&state);
        } else if (ch == KEY_RIGHT) {
            char* eb = list_flatten((List*)state.editbuffer);
            size_t len = (eb != nullptr) ? strlen(eb) : 0;
            if ((size_t)state.curpos < len) {
                int sl = utf8_seq_len((unsigned char)eb[state.curpos]);
                if ((size_t)(state.curpos + sl) > len) { sl = 1; }
                state.curpos += sl;
            }
            sync_cursor(&state);
        } else if (ch == KEY_UP) {
            char* eb = list_flatten((List*)state.editbuffer);
            int line = 0;
            int col = 0;
            pos_to_lc(eb, state.curpos, &line, &col);
            if (line > 0) {
                int target_line = line - 1;
                int start = -1;
                int l = 0;
                if (eb != nullptr) {
                    for (int i = 0; eb[i] != '\0'; ++i) {
                        if (l == target_line) { start = i; break; }
                        if (eb[i] == '\n') { ++l; }
                    }
                }
                if (start == -1) { start = (eb != nullptr) ? (int)strlen(eb) : 0; }
                int new_off = visual_col_to_offset(eb + start, col);
                state.curpos = start + new_off;
            }
            sync_cursor(&state);
        } else if (ch == KEY_DOWN) {
            char* eb = list_flatten((List*)state.editbuffer);
            int total = line_count(&state);
            int line = 0;
            int col = 0;
            pos_to_lc(eb, state.curpos, &line, &col);
            if (line + 1 < total) {
                int target_line = line + 1;
                int start = -1;
                int l = 0;
                if (eb != nullptr) {
                    for (int i = 0; eb[i] != '\0'; ++i) {
                        if (l == target_line) { start = i; break; }
                        if (eb[i] == '\n') { ++l; }
                    }
                }
                if (start == -1) { start = (eb != nullptr) ? (int)strlen(eb) : 0; }
                int new_off = visual_col_to_offset(eb + start, col);
                state.curpos = start + new_off;
            }
            size_t len = (eb != nullptr) ? strlen(eb) : 0;
            if ((size_t)state.curpos > len) { state.curpos = (int)len; }
            sync_cursor(&state);
        } else if (ch == 23) { // C-w
            save(&state);
        } else if (ch == 1) { // C-a
            state.append_mode = !state.append_mode;
            sync_cursor(&state);
        } else if (ch == 27) { // ESC
            state.append_mode = false;
            sync_cursor(&state);
        } else if (state.append_mode && (ch == KEY_ENTER || ch == 10 || ch == 13 || ch == '\n')) {
            chadd((List*)state.editbuffer, "\n", state.curpos);
            state.curpos++;
            sync_cursor(&state);
        } else if (state.append_mode && ch == 9) { // TAB
            chadd((List*)state.editbuffer, "\t", state.curpos);
            state.curpos++;
            sync_cursor(&state);
        } else if (state.append_mode && ch >= 32 && ch != 127) {
            char buf[2];
            buf[0] = (char)ch;
            buf[1] = '\0';
            chadd((List*)state.editbuffer, buf, state.curpos);
            state.curpos++;
            sync_cursor(&state);
        }

        update(srtstate, text, &state);
        mvwchgat(text, state.cur_y, state.cur_x, 1, A_REVERSE, 0, nullptr);
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
