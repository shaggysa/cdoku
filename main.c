#include <assert.h>

#include "library.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <pthread.h>

#define PUZZLE_SIZE 81
#define BILLION  1000000000L
#define PUZZLES_PER_CHUNK 50000
#define CHUNK_SIZE (PUZZLES_PER_CHUNK * PUZZLE_FILE_ROW_SIZE)

#ifdef MMMAP
typedef struct puzzle_instructions {

};
#else
typedef struct puzzle_instructions {
    int num_chunks;

    // the file must have been seeked to first puzzle the thread should solve
    FILE* file;

    // these pointers must match the first position the puzzle expects.
    uint8_t (*unsolved)[81];
    uint8_t (*solved)[81];

} puzzle_instructions;

void *read_puzzles(void *args) {
    const puzzle_instructions instructions = *(puzzle_instructions *)args;

    for (int chunk = 0; chunk < instructions.num_chunks; chunk++) {
        char *buff = malloc(CHUNK_SIZE);
            fread(buff, 1, CHUNK_SIZE, instructions.file);

            for (int p = 0; p < PUZZLES_PER_CHUNK; p++) {
                const int puzzle_idx = p * PUZZLE_FILE_ROW_SIZE;

                if (buff[81 + puzzle_idx] != ',') {
                    printf("Malformed puzzle row %d, exiting. Found %c instead of ',", p, buff[81 + puzzle_idx]);
                    exit(1);
                }

                for (int j = 0; j < 81; j++) {
                    instructions.unsolved[p + PUZZLES_PER_CHUNK * chunk][j] = buff[j + puzzle_idx] - '0';
                    instructions.solved[p + PUZZLES_PER_CHUNK * chunk][j] = buff[j + puzzle_idx + 82] - '0';
                }
            }
        free(buff);
    }

    return NULL;
}
#endif

typedef struct puzzle_reader {
    long num_puzzles;
    uint8_t (*unsolved)[PUZZLE_SIZE];
    uint8_t (*solved)[PUZZLE_SIZE];
} puzzle_reader;



puzzle_reader read_puzzles_threaded(const char *filename, int _max_threads) {

    FILE *file = fopen(filename, "rb");

    if (file == NULL) {
        perror("Unable to open file");
    }

    fseek(file, 0, SEEK_END);

    const long file_size = ftell(file);

    const long num_puzzles = (file_size - PUZZLE_FILE_HEADER_SIZE) / PUZZLE_FILE_ROW_SIZE;

    fseek(file, PUZZLE_FILE_HEADER_SIZE, SEEK_SET);

    uint8_t (*unsolved)[PUZZLE_SIZE] = malloc(num_puzzles * sizeof(uint8_t[PUZZLE_SIZE]));
    uint8_t (*solved)[PUZZLE_SIZE] = malloc(num_puzzles * sizeof(uint8_t[PUZZLE_SIZE]));


#ifdef MMAP
    fclose(file);
    const int fd = open(filename, O_RDONLY);
    const char* puzzle_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (puzzle_data == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    for (int i = 0; i < num_puzzles; i++) {
        const int puzzle_idx = i * PUZZLE_FILE_ROW_SIZE + PUZZLE_FILE_HEADER_SIZE;
        if (puzzle_data[81 + puzzle_idx] != ',') {
            printf("Malformed puzzle row %d, exiting. Found %c instead of ',", i, puzzle_data[81 + puzzle_idx]);
            exit(1);
        }
        for (int j = 0; j < 81; j++) {
                unsolved[i][j] = puzzle_data[j + puzzle_idx] - '0';
                solved[i][j] = puzzle_data[j + puzzle_idx + 82] - '0';
        }
    }


#else
    pthread_t t1;

    puzzle_instructions is = {
        file_size / CHUNK_SIZE, // NOLINT(*-narrowing-conversions)
        file,
        unsolved,
        solved
    };

    pthread_create(&t1, NULL, read_puzzles, &is);
    pthread_join(t1, NULL);

#endif

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

#ifdef MMAP
    printf("Using MMAP for file reading\n");
#endif

    struct timespec start, stop;

    clock_gettime( CLOCK_REALTIME, &start);

    const puzzle_reader reader = read_puzzles_threaded("../sudoku.csv", 8);

    clock_gettime( CLOCK_REALTIME, &stop);

    const double delta = stop.tv_sec - start.tv_sec // NOLINT(*-narrowing-conversions)
            + (double)( stop.tv_nsec - start.tv_nsec )
              / (double)BILLION;

    printf("Parsed %ld puzzles in %f milliseconds\n", reader.num_puzzles, delta * 1000);

    printf("%d", reader.solved[8999999][18]);

    destroy_puzzles(reader);

    return 0;
}
