#ifndef CDOKU_LIBRARY_H
#define CDOKU_LIBRARY_H

#include<stdio.h>

#define PUZZLE_FILE_HEADER_SIZE 16
#define PUZZLE_FILE_ROW_SIZE 164

void hello(void);

long get_num_puzzles(FILE *f);

#endif // CDOKU_LIBRARY_H