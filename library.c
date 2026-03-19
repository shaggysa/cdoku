#include "library.h"
#include <stdio.h>
#include <sys/stat.h>

void hello(void) {
    printf("Hello, World!\n");
}

long get_num_puzzles(FILE *f) {

    return (ftell(f) - PUZZLE_FILE_HEADER_SIZE) / PUZZLE_FILE_ROW_SIZE;
}