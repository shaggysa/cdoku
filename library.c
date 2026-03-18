#include "library.h"
#include <stdio.h>

void hello(void) {
    printf("Hello, World!\n");
}

long get_num_puzzles(FILE *f) {
    fseek(f,0L,SEEK_END);

    return (ftell(f) - PUZZLE_FILE_HEADER_SIZE) / PUZZLE_FILE_ROW_SIZE;
}