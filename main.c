#include <assert.h>

#include "library.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

#define PUZZLE_SIZE 81

typedef struct puzzle_reader {
    long num_puzzles;
    uint8_t (*unsolved)[PUZZLE_SIZE];
    uint8_t (*solved)[PUZZLE_SIZE];
} puzzle_reader;

puzzle_reader read_puzzles(FILE *file) {
    const long num_puzzles = get_num_puzzles(file);

    uint8_t (*unsolved)[PUZZLE_SIZE] = malloc(num_puzzles * sizeof(uint8_t[PUZZLE_SIZE]));
    uint8_t (*solved)[PUZZLE_SIZE] = malloc(num_puzzles * sizeof(uint8_t[PUZZLE_SIZE]));

    fseek(file, 0L, SEEK_SET);

    // add extra character for null terminator
    char buf[PUZZLE_FILE_ROW_SIZE + 1];

    // read the first line to skip the header
    fgets(buf, sizeof(buf), file);

    for (long i = 0; i < num_puzzles; i++) {
        fgets(buf, sizeof(buf), file);

        if (buf[81] != ',') {
            printf("Malformed puzzle row %s, (line %ld) exiting.", buf, i);
            exit(1);
        }

        for (int j = 0; j < 81; j++) {
            unsolved[i][j] = buf[j] - '0';
            solved[i][j] = buf[j + 82] - '0';
        }
    }

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

    FILE *file = fopen("puzzles.csv", "r");

    if (!file) {
        printf("failed to open the file!");
        exit(1);
    }

    struct timespec start, end;

    clock_gettime(CLOCK_REALTIME, &start);

    const puzzle_reader reader = read_puzzles(file);

    clock_gettime(CLOCK_REALTIME, &end);

    const double elapsed =  end.tv_sec - start.tv_sec
             + (double)( end.tv_nsec - start.tv_nsec )
               / 1000000000.0;

    printf("Read %ld puzzles in %f milliseconds!", reader.num_puzzles, elapsed * 1000);


    destroy_puzzles(reader);

}
