#include "library.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <pthread.h>

#define PUZZLE_SIZE 81
#define BILLION  1000000000L
#define MAX_PUZZLES_PER_CHUNK 50000
#define CHUNK_SIZE (PUZZLES_PER_CHUNK * PUZZLE_FILE_ROW_SIZE)

#ifdef _WIN32
#include <windows.h>
long get_processor_count() {
    return GetActiveProcessorCount(ALL_PROCESSOR_GROUPS); // NOLINT(*-narrowing-conversions)
}
#else
#include <unistd.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
long get_processor_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}
#endif

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

    fclose(file);

#ifdef _WIN32
    HANDLE file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file_handle == INVALID_HANDLE_VALUE) {
        printf("failed to create a windows file handle!");
        exit(1);
    }
    HANDLE file_mapping = CreateFileMappingA(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (file_mapping == NULL) {
        printf("failed to create a windows file mapping!");
        exit(1);
    }
    CloseHandle(file_handle);
    const char *puzzle_data = MapViewOfFile( file_mapping, FILE_MAP_READ, 0, 0, 0) + PUZZLE_FILE_HEADER_SIZE;
#else
    const int fd = open(filename, O_RDONLY);
    const char *puzzle_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0) + PUZZLE_FILE_HEADER_SIZE;

    close(fd);

    if (puzzle_data == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
#endif

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

    free(instructions);
    free(threads);

#ifdef _WIN32
    CloseHandle(file_mapping - PUZZLE_FILE_HEADER_SIZE);
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
    const long logical_cores = get_processor_count();

    if (logical_cores == -1) {
        perror("Logical core count unavailable.");
        exit(1);
    }

    printf("Logical cores: %ld\n", logical_cores);

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
