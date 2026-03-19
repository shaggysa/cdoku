#include <assert.h>

#include "library.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#define PUZZLE_SIZE 81
#define BILLION  1000000000L
#define PUZZLES_PER_CHUNK 50000
#define CHUNK_SIZE (PUZZLES_PER_CHUNK * PUZZLE_FILE_ROW_SIZE)

typedef struct puzzle_reader {
    long num_puzzles;
    uint8_t (*unsolved)[PUZZLE_SIZE];
    uint8_t (*solved)[PUZZLE_SIZE];
} puzzle_reader;

puzzle_reader read_puzzles(const char *filename) {

    FILE *file = fopen(filename, "rb");

    if (file == NULL) {
        perror("Unable to open file");
    }

    fseek(file, 0, SEEK_END);

    const long file_size = ftell(file);

    const long num_puzzles = (file_size - PUZZLE_FILE_HEADER_SIZE) / PUZZLE_FILE_ROW_SIZE;

    fseek(file, PUZZLE_FILE_HEADER_SIZE, SEEK_SET);

    char *buff = malloc(CHUNK_SIZE);

    uint8_t (*unsolved)[PUZZLE_SIZE] = malloc(num_puzzles * sizeof(uint8_t[PUZZLE_SIZE]));
    uint8_t (*solved)[PUZZLE_SIZE] = malloc(num_puzzles * sizeof(uint8_t[PUZZLE_SIZE]));

    for (int chunk = 0; chunk < (file_size - PUZZLE_FILE_HEADER_SIZE) / CHUNK_SIZE; chunk++) {
        fread(buff, 1, CHUNK_SIZE, file);

        for (int p = 0; p < PUZZLES_PER_CHUNK; p++) {
            const int puzzle_idx = p * PUZZLE_FILE_ROW_SIZE;

            if (buff[81 + puzzle_idx] != ',') {
                printf("Malformed puzzle row %d, exiting. Found %c instead of ',", p, buff[81 + puzzle_idx]);
                exit(1);
            }

            for (int j = 0; j < 81; j++) {
                unsolved[p + PUZZLES_PER_CHUNK * chunk][j] = buff[j + puzzle_idx] - '0';
                solved[p + PUZZLES_PER_CHUNK * chunk][j] = buff[j + puzzle_idx + 82] - '0';
            }
        }
    }

    free(buff);

    const puzzle_reader puzzles = {
        num_puzzles,
        unsolved,
        solved
    };

    return puzzles;
}

void destroy_puzzles(const puzzle_reader reader) {
    free(reader.solved);
    free(reader.unsolved);
}

int main(void) {

    struct timespec start, stop;

    clock_gettime( CLOCK_REALTIME, &start);

    const puzzle_reader reader = read_puzzles("sudoku.csv");

    clock_gettime( CLOCK_REALTIME, &stop);

    const double delta = stop.tv_sec - start.tv_sec // NOLINT(*-narrowing-conversions)
            + (double)( stop.tv_nsec - start.tv_nsec )
              / (double)BILLION;

    printf("Parsed %ld puzzles in %f milliseconds\n", reader.num_puzzles, delta * 1000);

    destroy_puzzles(reader);

    return 0;
}
