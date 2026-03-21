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
#define MAX_PUZZLES_PER_CHUNK 50000
#define CHUNK_SIZE (PUZZLES_PER_CHUNK * PUZZLE_FILE_ROW_SIZE)

#ifdef MMAP
typedef struct puzzle_instructions {
    long num_puzzles;
    const char *puzzle_file;

    uint8_t (*unsolved)[81];
    uint8_t (*solved)[81];
} puzzle_instructions;

void *read_puzzles(void *args) {
    const puzzle_instructions instructions = *(puzzle_instructions *)args;

    const char *buff = instructions.puzzle_file;

    for (int p = 0; p < instructions.num_puzzles; p++) {
        const int puzzle_idx = p * PUZZLE_FILE_ROW_SIZE;

        if (buff[81 + puzzle_idx] != ',') {
            printf("Malformed puzzle row %d, exiting. Found %c instead of ',", p, buff[81 + puzzle_idx]);
            exit(1);
        }

        for (int j = 0; j < 81; j++) {
            instructions.unsolved[p][j] = buff[j + puzzle_idx] - '0';
            instructions.solved[p][j] = buff[j + puzzle_idx + 82] - '0';
        }
    }

    return NULL;
}
#else
typedef struct puzzle_instructions {
    long num_chunks;
    long puzzles_per_chunk;

    // the file must have been seeked to first puzzle the thread should solve
    FILE* file;

    // these pointers must match the first position the puzzle expects.
    uint8_t (*unsolved)[81];
    uint8_t (*solved)[81];

} puzzle_instructions;

void *read_puzzles(void *args) {
    const puzzle_instructions instructions = *(puzzle_instructions *)args;
    const long chunk_size = instructions.puzzles_per_chunk * PUZZLE_FILE_ROW_SIZE;

    char *buff = malloc(chunk_size);

    for (long chunk = 0; chunk < instructions.num_chunks; chunk++) {
            fread(buff, 1, chunk_size, instructions.file);

            for (int p = 0; p < instructions.puzzles_per_chunk; p++) {
                const int puzzle_idx = p * PUZZLE_FILE_ROW_SIZE;

                if (buff[81 + puzzle_idx] != ',') {
                    printf("Malformed puzzle row %d, exiting. Found %c instead of ',", p, buff[81 + puzzle_idx]);
                    exit(1);
                }

                for (int j = 0; j < 81; j++) {
                    instructions.unsolved[p + instructions.puzzles_per_chunk * chunk][j] = buff[j + puzzle_idx] - '0';
                    instructions.solved[p + instructions.puzzles_per_chunk * chunk][j] = buff[j + puzzle_idx + 82] - '0';
                }
            }
    }
    free(buff);

    return NULL;
}
#endif

typedef struct puzzle_reader {
    long num_puzzles;
    uint8_t (*unsolved)[PUZZLE_SIZE];
    uint8_t (*solved)[PUZZLE_SIZE];
} puzzle_reader;

puzzle_reader read_puzzles_threaded(const char *filename, const long num_threads) {

    FILE *file = fopen(filename, "rb");

    if (file == NULL) {
        perror("Unable to open the puzzle file");
        exit(1);
    }

    fseek(file, 0, SEEK_END);

    const long file_size = ftell(file);

    const long num_puzzles = (file_size - PUZZLE_FILE_HEADER_SIZE) / PUZZLE_FILE_ROW_SIZE;

    fseek(file, PUZZLE_FILE_HEADER_SIZE, SEEK_SET);

    uint8_t (*unsolved)[PUZZLE_SIZE] = malloc(num_puzzles * sizeof(uint8_t[PUZZLE_SIZE]));
    uint8_t (*solved)[PUZZLE_SIZE] = malloc(num_puzzles * sizeof(uint8_t[PUZZLE_SIZE]));

    puzzle_instructions *instructions = malloc(sizeof(puzzle_instructions) * num_threads);
    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);

#ifdef MMAP
    fclose(file);
    const int fd = open(filename, O_RDONLY);
    const char* puzzle_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0) + PUZZLE_FILE_HEADER_SIZE;

    close(fd);

    if (puzzle_data == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    const long puzzles_per_thread = num_puzzles / num_threads;
    const long extra_puzzles = num_puzzles % num_threads;
    long allocated_puzzles = 0;

    for (int i = 0; i < num_threads; i++) {
        const long thread_puzzles = i < extra_puzzles ? puzzles_per_thread + 1 : puzzles_per_thread;

        instructions[i].num_puzzles = thread_puzzles;
        instructions[i].puzzle_file = puzzle_data + allocated_puzzles * PUZZLE_FILE_ROW_SIZE;
        instructions[i].unsolved = unsolved + allocated_puzzles;
        instructions[i].solved = solved + allocated_puzzles;

        pthread_create(threads + i, NULL, read_puzzles, instructions + i);
        allocated_puzzles += thread_puzzles;
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }


#else

    const long puzzles_per_chunk = MAX_PUZZLES_PER_CHUNK < num_puzzles / num_threads ? MAX_PUZZLES_PER_CHUNK : num_puzzles / num_threads;
    const long chunk_size = puzzles_per_chunk * PUZZLE_FILE_ROW_SIZE;

    const long total_chunks = (file_size - 15) / chunk_size;

    const long chunks_per_thread = total_chunks / num_threads;
    const long extra_chunks = total_chunks % num_threads;

    long allocated_chunks = 0;

    for (long i = 0; i < num_threads; i++) {
        const long thread_chunks = i < extra_chunks ? chunks_per_thread + 1 : chunks_per_thread;

        FILE *thread_file = fopen(filename, "rb");

        if (thread_file == NULL) {
            perror("Unable to open the puzzle file");
            exit(1);
        }

        fseek(thread_file, PUZZLE_FILE_HEADER_SIZE + allocated_chunks * chunk_size, SEEK_SET);

        instructions[i].file = thread_file;
        instructions[i].puzzles_per_chunk = puzzles_per_chunk;
        instructions[i].num_chunks = thread_chunks;
        instructions[i].unsolved = unsolved + allocated_chunks * puzzles_per_chunk;
        instructions[i].solved = solved + allocated_chunks * puzzles_per_chunk;

        pthread_create(threads + i, NULL, read_puzzles, instructions + i);
        allocated_chunks += thread_chunks;
    }

    // take care of orphaned puzzles in the main thread
    fseek(file, PUZZLE_FILE_HEADER_SIZE + allocated_chunks * chunk_size, SEEK_SET);
    const long remaining_size = file_size - ftell(file);

    const long remaining_puzzles = remaining_size / PUZZLE_FILE_ROW_SIZE;

    uint8_t (*offset_unsolved)[81] = unsolved + (num_puzzles - remaining_puzzles);
    uint8_t (*offset_solved)[81] = solved + (num_puzzles - remaining_puzzles);

    char *buff = malloc(remaining_size);

    fread(buff, 1, remaining_size, file);

    for (long p = 0; p < remaining_puzzles ; p++) {
        const long puzzle_idx = p * PUZZLE_FILE_ROW_SIZE;

        if (buff[81 + puzzle_idx] != ',') {
            printf("Malformed puzzle row %ld, exiting. Found %c instead of ',", p, buff[81 + puzzle_idx]);
            exit(1);
        }

        for (int j = 0; j < 81; j++) {
            offset_unsolved[p][j] = buff[j + puzzle_idx] - '0';
            offset_solved[p][j] = buff[j + puzzle_idx + 82] - '0';
        }
    }

    free(buff);

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

#endif
    free(instructions);
    free(threads);

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
    puts("Using MMAP for file reading");
#else
    puts("Reading file chunks into a buffer");
#endif

    const long logical_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (logical_cores == -1) {
        perror("Logical core count unavailable.");
        exit(1);
    }

    printf("Logical cores (sysconf): %ld\n", logical_cores);


    struct timespec start, stop;

    clock_gettime( CLOCK_REALTIME, &start);

    const puzzle_reader reader = read_puzzles_threaded("../sudoku.csv", logical_cores * 2);

    clock_gettime( CLOCK_REALTIME, &stop);

    const double delta = stop.tv_sec - start.tv_sec // NOLINT(*-narrowing-conversions)
            + (double)( stop.tv_nsec - start.tv_nsec )
              / (double)BILLION;

    printf("Read and parsed %ld puzzles in %f milliseconds (%f ns / puzzle)\n", reader.num_puzzles, delta * 1000, delta * 1000000000 / reader.num_puzzles ); // NOLINT(*-narrowing-conversions)

    destroy_puzzles(reader);

    return 0;
}
