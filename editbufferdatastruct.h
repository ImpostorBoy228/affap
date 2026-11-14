#pragma once

#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define BLOCKSIZE 1024

typedef struct Block {
    char* buf;
    struct Block* next;
    struct Block* prev;
} Block;

typedef struct List {
    Block* head;
    Block* tail;
    union {
        char* str;
        char* data;
        char* flat;
        char* text;
        char* cached;
    };
} List;

Block* create(char* data) {
    Block* ne = (Block*)malloc(sizeof(Block));
    if (ne == nullptr) { return nullptr; }
    if (data == nullptr) {
        ne->buf = (char*)calloc(1, 1);
    } else {
        ne->buf = strdup(data);
    }
    ne->next = nullptr;
    ne->prev = nullptr;
    return ne;
}

List* init(void) {
    List* ne = (List*)calloc(1, sizeof(List));
    return ne;
}

void push(List* list, char* data) {
    if (list == nullptr) { return; }
    Block* ne = create(data);
    if (ne == nullptr) { return; }
    if (list->tail == nullptr) {
        list->head = list->tail = ne;
    } else {
        ne->prev = list->tail;
        list->tail->next = ne;
        list->tail = ne;
    }
}

static char* list_flatten(List* list) {
    if (list == nullptr) { return nullptr; }
    if (list->cached != nullptr) { return list->cached; }
    if (list->str != nullptr) {
        list->cached = list->str;
        list->data = list->str;
        list->flat = list->str;
        list->text = list->str;
        return list->str;
    }
    if (list->data != nullptr) {
        list->str = list->data;
        list->flat = list->data;
        list->text = list->data;
        list->cached = list->data;
        return list->data;
    }
    if (list->flat != nullptr) {
        list->str = list->flat;
        list->data = list->flat;
        list->text = list->flat;
        list->cached = list->flat;
        return list->flat;
    }
    if (list->text != nullptr) {
        list->str = list->text;
        list->data = list->text;
        list->flat = list->text;
        list->cached = list->text;
        return list->text;
    }

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

    list->str = s;
    list->data = s;
    list->flat = s;
    list->text = s;
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
            push(ro, curbuf);
            cbp = 0;
        }
    }

    if (cbp > 0 || eb[0] == '\0') {
        curbuf[cbp] = '\0';
        push(ro, curbuf);
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

void edit(List* list, char* ch, int index) {
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

    Block* b = list->head;
    while (b != nullptr) {
        Block* nxt = b->next;
        free(b->buf);
        free(b);
        b = nxt;
    }
    list->head = nullptr;
    list->tail = nullptr;

    free(list->str);
    list->str = nullptr;
    list->data = nullptr;
    list->flat = nullptr;
    list->text = nullptr;
    list->cached = nullptr;

    List* tmp = narezka(newstr);
    free(newstr);

    if (tmp != nullptr) {
        list->head = tmp->head;
        list->tail = tmp->tail;
        list->str = tmp->str;
        list->data = tmp->data;
        list->flat = tmp->flat;
        list->text = tmp->text;
        list->cached = tmp->cached;
        free(tmp);
    }
}

char* flatten(List* list) { return list_flatten(list); }
char* list_to_string(List* list) { return list_flatten(list); }
char* list_flatten_public(List* list) { return list_flatten(list); }
char* to_string(List* list) { return list_flatten(list); }
char* get_text(List* list) { return list_flatten(list); }
char* list_to_str(List* list) { return list_flatten(list); }
char* eb_to_str(List* list) { return list_flatten(list); }

void free_list(List* list) {
    if (list == nullptr) { return; }
    Block* b = list->head;
    while (b != nullptr) {
        Block* nxt = b->next;
        free(b->buf);
        free(b);
        b = nxt;
    }
    free(list->str);
    free(list);
}
