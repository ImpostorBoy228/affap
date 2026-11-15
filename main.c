#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <ncurses.h>

#define STATUS_FG 235
#define STATUS_BG 103
#define TEXT_FG 231
#define TEXT_BG 235

#define PAIR_STATUS 1
#define PAIR_TEXT 2

#define SCROLL_S 1

/// BEGIN EBDS (gap buffer)
typedef struct Buffer {
    char* data;
    size_t total_cap;
    size_t gap_start;
    size_t gap_size;
    size_t size;      // logical length
    int lines;
    // cached contiguous view
    char* flat;
    bool flat_valid;
} Buffer;

static inline size_t buf_gap_end(const Buffer* b) { return b->gap_start + b->gap_size; }

static inline char buf_char_at(const Buffer* b, size_t i) {
    if (i < b->gap_start) return b->data[i];
    return b->data[i + b->gap_size];
}

static char* buf_flatten(Buffer* b) {
    if (b == nullptr) return nullptr;
    if (b->flat_valid && b->flat != nullptr) return b->flat;
    free(b->flat);
    b->flat = (char*)malloc(b->size + 1);
    if (b->flat == nullptr) return nullptr;
    memcpy(b->flat, b->data, b->gap_start);
    memcpy(b->flat + b->gap_start, b->data + buf_gap_end(b), b->size - b->gap_start);
    b->flat[b->size] = '\0';
    b->flat_valid = true;
    return b->flat;
}

static inline char* list_flatten(Buffer* b) { return buf_flatten(b); }

static void buf_grow(Buffer* b, size_t need) {
    if (b->gap_size >= need) return;
    size_t newcap = b->total_cap ? b->total_cap * 2 : 64;
    while (newcap < b->size + need + 1) newcap *= 2;
    if (newcap < b->size + need + 64) newcap = b->size + need + 64;
    char* nd = (char*)malloc(newcap);
    if (nd == nullptr) return;
    // left part
    memcpy(nd, b->data, b->gap_start);
    // right part
    size_t right_len = b->total_cap - buf_gap_end(b);
    size_t new_gap_size = newcap - b->size;
    memcpy(nd + b->gap_start + new_gap_size, b->data + buf_gap_end(b), right_len);
    free(b->data);
    b->data = nd;
    b->total_cap = newcap;
    b->gap_size = new_gap_size;
    b->flat_valid = false;
}

static void buf_move_gap(Buffer* b, size_t pos) {
    if (pos == b->gap_start) return;
    if (pos < b->gap_start) {
        size_t n = b->gap_start - pos;
        memmove(b->data + pos + b->gap_size, b->data + pos, n);
        b->gap_start = pos;
    } else {
        size_t n = pos - b->gap_start;
        memmove(b->data + b->gap_start, b->data + buf_gap_end(b), n);
        b->gap_start = pos;
    }
    b->flat_valid = false;
}

static Buffer* buf_create(const char* s) {
    size_t len = (s != nullptr) ? strlen(s) : 0;
    size_t cap = len + 64;
    if (cap < 64) cap = 64;
    Buffer* b = (Buffer*)calloc(1, sizeof(Buffer));
    if (b == nullptr) return nullptr;
    b->data = (char*)malloc(cap);
    if (b->data == nullptr) { free(b); return nullptr; }
    if (s != nullptr && len > 0) memcpy(b->data, s, len);
    b->total_cap = cap;
    b->gap_start = len;
    b->gap_size = cap - len;
    b->size = len;
    b->flat = nullptr;
    b->flat_valid = false;
    int ln = 1;
    for (size_t i = 0; i < len; ++i) if (s[i] == '\n') ++ln;
    b->lines = ln;
    return b;
}

Buffer* narezka(char* eb) {
    if (eb == nullptr) return nullptr;
    return buf_create(eb);
}

void chadd(Buffer* b, char* ch, int index) {
    if (b == nullptr || ch == nullptr) return;
    size_t chlen = strlen(ch);
    if (chlen == 0) return;

    size_t cur = b->size;
    if (index < 0) index = 0;
    if ((size_t)index > cur) index = (int)cur;

    if (b->gap_size < chlen) buf_grow(b, chlen);
    if (b->gap_size < chlen) return; // grow failed

    buf_move_gap(b, (size_t)index);
    memcpy(b->data + b->gap_start, ch, chlen);
    b->gap_start += chlen;
    b->gap_size -= chlen;
    b->size += chlen;
    b->flat_valid = false;

    int addlines = 0;
    for (size_t i = 0; i < chlen; ++i) if (ch[i] == '\n') ++addlines;
    b->lines += addlines;
}

void free_list(Buffer* b) {
    if (b == nullptr) return;
    free(b->data);
    free(b->flat);
    free(b);
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

    int cached_scroll;
    size_t cached_scroll_off;
    bool scroll_off_valid;

    bool hide_mode;

    Buffer* editbuffer;
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
        .cached_scroll = 0,
        .cached_scroll_off = 0,
        .scroll_off_valid = false,
        .hide_mode = false,
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

size_t ioffset_scroll(mt* state, int stn) {
    // returns offset of the first char of line stn (0-based)
    if (state == nullptr || state->editbuffer == nullptr || stn <= 0) {
        return 0;
    }

    if (state->scroll_off_valid && state->cached_scroll == stn) {
        return state->cached_scroll_off;
    }

    char* eb = list_flatten(state->editbuffer);
    if (eb == nullptr) {
        return 0;
    }

    size_t __len = strlen(eb);
    int curstn = 0;
    size_t i = 0;
    while (i < __len && curstn < stn) {
        char* nl = memchr(eb + i, '\n', __len - i);
        if (nl == nullptr) break;
        i = (size_t)(nl - eb) + 1;
        ++curstn;
        if (curstn == stn) {
            state->cached_scroll = stn;
            state->cached_scroll_off = i;
            state->scroll_off_valid = true;
            return i;
        }
    }

    state->cached_scroll = stn;
    state->cached_scroll_off = 0;
    state->scroll_off_valid = true;
    return 0;
}

int line_count(const mt* state) {
    if (state == nullptr || state->editbuffer == nullptr) {
        return 1;
    }
    if (state->editbuffer->lines < 1) {
        return 1;
    }
    return state->editbuffer->lines;
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

    char* eb = list_flatten(state->editbuffer);
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
    char* flat = list_flatten(state->editbuffer);
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

void update_text(WINDOW* w, mt* state) {
    werase(w);
    wattron(w, COLOR_PAIR(PAIR_TEXT));
    char* flat = (state != nullptr && state->editbuffer != nullptr)
                     ? list_flatten(state->editbuffer)
                     : nullptr;
    size_t flatlen = (flat != nullptr) ? strlen(flat) : 0;
    size_t off = ioffset_scroll(state, state->scroll);
    if (flat != nullptr && off <= flatlen) {
        char* src = flat + off;
        size_t srclen = strlen(src);
        char* buf = (char*)malloc(srclen * 4 + 1);
        if (buf != nullptr) {
            size_t j = 0;
            size_t i = 0;
            while (src[i] != '\0') {
                int sl = utf8_seq_len((unsigned char)src[i]);
                if (i + (size_t)sl > srclen) sl = 1;
                unsigned int cp = utf8_decode(src + i, sl);

                if (cp == '\n' || cp == '\t' || cp == ' ') {
                    memcpy(buf + j, src + i, (size_t)sl);
                    j += (size_t)sl;
                } else if (state != nullptr && state->hide_mode) {
                    unsigned int r = 0x2800 + (unsigned int)(rand() % 256);
                    buf[j++] = (char)(0xE0 | ((r >> 12) & 0x0F));
                    buf[j++] = (char)(0x80 | ((r >> 6) & 0x3F));
                    buf[j++] = (char)(0x80 | (r & 0x3F));
                } else {
                    if (cp < 0x20 || cp == 0x7F) {
                        /* skip control chars */
                    } else {
                        memcpy(buf + j, src + i, (size_t)sl);
                        j += (size_t)sl;
                    }
                }
                i += (size_t)sl;
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
    srand((unsigned)time(NULL));

    mt state = {0};

    char* path;
    if (argc>1) {
        path = argv[1];
    } else { return 666; }


    initscr();
    raw();
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
            goto quit_ok;
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
        } else if (ch == 8) { // C-h: toggle hide mode
            state.hide_mode = !state.hide_mode;
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
                char* eb = list_flatten(state.editbuffer);
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
            char* eb = list_flatten(state.editbuffer);
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
            char* eb = list_flatten(state.editbuffer);
            size_t len = (eb != nullptr) ? strlen(eb) : 0;
            if ((size_t)state.curpos < len) {
                int sl = utf8_seq_len((unsigned char)eb[state.curpos]);
                if ((size_t)(state.curpos + sl) > len) { sl = 1; }
                state.curpos += sl;
            }
            sync_cursor(&state);
        } else if (ch == KEY_UP) {
            char* eb = list_flatten(state.editbuffer);
            int line = 0;
            int col = 0;
            pos_to_lc(eb, state.curpos, &line, &col);
            if (line > 0) {
                int target_line = line - 1;
                int start = 0;
                if (eb != nullptr) {
                    size_t len = strlen(eb);
                    size_t i = 0;
                    int l = 0;
                    while (l < target_line && i < len) {
                        char* nl = memchr(eb + i, '\n', len - i);
                        if (nl == nullptr) break;
                        i = (size_t)(nl - eb) + 1;
                        ++l;
                    }
                    start = (l == target_line) ? (int)i : (int)len;
                } else {
                    start = 0;
                }
                int new_off = visual_col_to_offset(eb + start, col);
                state.curpos = start + new_off;
            }
            sync_cursor(&state);
        } else if (ch == KEY_DOWN) {
            char* eb = list_flatten(state.editbuffer);
            int total = line_count(&state);
            int line = 0;
            int col = 0;
            pos_to_lc(eb, state.curpos, &line, &col);
            if (line + 1 < total) {
                int target_line = line + 1;
                int start = 0;
                if (eb != nullptr) {
                    size_t len = strlen(eb);
                    size_t i = 0;
                    int l = 0;
                    while (l < target_line && i < len) {
                        char* nl = memchr(eb + i, '\n', len - i);
                        if (nl == nullptr) break;
                        i = (size_t)(nl - eb) + 1;
                        ++l;
                    }
                    start = (l == target_line) ? (int)i : (int)len;
                } else {
                    start = 0;
                }
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
            chadd(state.editbuffer, "\n", state.curpos);
            state.curpos++;
            state.scroll_off_valid = false;
            sync_cursor(&state);
        } else if (state.append_mode && ch == 9) { // TAB
            chadd(state.editbuffer, "\t", state.curpos);
            state.curpos++;
            state.scroll_off_valid = false;
            sync_cursor(&state);
        } else if (state.append_mode && ch >= 32 && ch != 127) {
            char buf[2];
            buf[0] = (char)ch;
            buf[1] = '\0';
            chadd(state.editbuffer, buf, state.curpos);
            state.curpos++;
            state.scroll_off_valid = false;
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
    if (state.editbuffer != nullptr) free_list(state.editbuffer);
    return 0;

quit_ok:
    delwin(text);
    delwin(srtstate);
    endwin();
    if (state.editbuffer != nullptr) free_list(state.editbuffer);
    return 0;

naxyi:
    delwin(text);
    delwin(srtstate);
    endwin();
    if (state.editbuffer != nullptr) free_list(state.editbuffer);
    return 666;
}


