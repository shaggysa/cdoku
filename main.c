#include "library.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

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
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
long get_processor_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}
#endif

typedef struct thread_instructions {
    long num_puzzles;
    const char *puzzle_file;

    uint8_t (*unsolved)[81];
    uint8_t (*solved)[81];
} thread_instructions;
#ifdef _WIN32
DWORD WINAPI read_puzzles(void *args) {
#else
void *read_puzzles(void *args) {
#endif

    const thread_instructions instructions = *(thread_instructions *)args;

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
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
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

    if (unsolved == NULL || solved == NULL) {
        printf("Failed to allocate the solved or unsolved array!");
        exit(1);
    }

    thread_instructions *instructions = malloc(sizeof(thread_instructions) * num_threads);
    if (instructions == NULL) {
        printf("failed to allocate thread instructions!");
        exit(1);
    }
#ifdef _WIN32
    DWORD   *dwThreadIds = malloc(sizeof(DWORD) * num_threads);
    HANDLE  *hThreads = malloc(sizeof(HANDLE) * num_threads);
    if (dwThreadIds == NULL || hThreads == NULL) {
        printf("failed to allocate windows thread array!");
        exit(1);
    }
#else
    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    if (threads == NULL) {
        printf("failed to allocate thread array!");
        exit(1);
    }
#endif


    fclose(file);

#ifdef _WIN32
    HANDLE file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file_handle == INVALID_HANDLE_VALUE) {
        printf("failed to create a windows file handle!");
        exit(1);
    }

    HANDLE file_mapping = CreateFileMappingA(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(file_handle);
    if (file_mapping == NULL) {
        printf("failed to create a windows file mapping!");
        exit(1);
    }

    const char *puzzle_data = MapViewOfFile(file_mapping, FILE_MAP_READ, 0, 0, 0) + PUZZLE_FILE_HEADER_SIZE;
    if (puzzle_data == NULL) {
        printf("windows file map view failed!");
        exit(1);
    }
#else
    const int fd = open(filename, O_RDONLY);

    if (fd == -1) {
        perror("file open");
        exit(1);
    }

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
#ifdef _WIN32
        hThreads[i] = CreateThread(NULL, 0, read_puzzles, instructions + i, 0, dwThreadIds + i);
        if (hThreads[i] == 0) {
            printf("failed to create a WINAPI thread!");
            exit(1);
        }
#else
        if (pthread_create(threads + i, NULL, read_puzzles, instructions + i) != 0) {
            perror("pthread creation");
            exit(1);
        }
#endif
        allocated_puzzles += thread_puzzles;
    }
#ifdef _WIN32
    WaitForMultipleObjects(num_threads, hThreads, TRUE, INFINITE);
    for (int i = 0; i < num_threads; i++) {
        CloseHandle(hThreads[i]);
    }
    free(hThreads);
    free(dwThreadIds);
#else
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);
#endif
    free(instructions);


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
