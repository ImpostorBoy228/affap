#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <ncurses.h>


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
    keypad(stdscr, TRUE);
    refresh();
    
    state = bootstap_mt(stdscr, path);
    if (state.editbuffer==NULL) goto naxyi;

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);  
        init_pair(2, COLOR_YELLOW, COLOR_BLACK); 
    }
    
    printw("%s", state.editbuffer);
    refresh();
    getch();

    endwin();
    if (state.editbuffer != NULL) free(state.editbuffer);
    return 0;

naxyi:
    endwin();
    if (state.editbuffer != NULL) free(state.editbuffer);
    return 666;
}
