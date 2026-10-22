#include "library.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

#define PUZZLE_SIZE 81
#define BILLION  1000000000L
#define MAX_PUZZLES_PER_CHUNK 50000
#define CHUNK_SIZE (PUZZLES_PER_CHUNK * PUZZLE_FILE_ROW_SIZE)

#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/fcntl.h>

static int num_solved = 0;

long get_processor_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

typedef struct thread_instructions {
    long num_puzzles;
    const char *puzzle_file;

    uint8_t (*unsolved)[81];
    uint8_t (*solved)[81];
} thread_instructions;

struct pz_empty_element {
    int idx;
    struct pz_empty_element *next;
};

void *read_puzzles(void *args) {

    const thread_instructions instructions = *(thread_instructions *)args;

    const char *buff = instructions.puzzle_file;

    for (int p = 0; p < instructions.num_puzzles; p++) {
        const int puzzle_idx = p * PUZZLE_FILE_ROW_SIZE;

        if (buff[81 + puzzle_idx] != ',') {
            printf("Malformed puzzle row %d, exiting. Found %c instead of ',\n", p, buff[81 + puzzle_idx]);
            exit(1);
        }

        uint16_t rows[9] = {0};
        uint16_t cols[9] = {0};
        uint16_t squares[3][3] = {0};
        struct pz_empty_element empty_elements[81] = {0};
        struct pz_empty_element *head = NULL;
        struct pz_empty_element *tail = NULL;

        for (int j = 0; j < 81; j++) {
            uint8_t unsolved_num = buff[j + puzzle_idx] - '0';

            // if the original value was below '0' the subtraction will cause an underflow and the value will be > 9
            if (unsolved_num > 9) {
                printf("Invalid puzzle square!");
                exit(1);
            }

            if (unsolved_num == 0) {
                empty_elements[j].idx = j;
                if (head != NULL) {
                    tail->next = &empty_elements[j];
                    tail = &empty_elements[j];
                } else {
                    head = &empty_elements[j];
                    tail = head;
                }
            }

            const int row = j / 9;
            const int col = j % 9;

            const uint16_t mask = 1 << unsolved_num;
            rows[row] |= mask;
            cols[col] |= mask;
            squares[row / 3][col / 3] |= mask;


            instructions.unsolved[p][j] = unsolved_num;
            instructions.solved[p][j] = buff[j + puzzle_idx + 82] - '0';
        }

        bool solved = false;

        // this presolver gets about 82% of the 9 million puzzles in the kaggle dataset
        while (true) {
            bool modified = false;
            struct pz_empty_element *ll_head = head;

            // take care of all the leading elements first,
            // we can't set prev->next if prev is invalid
            while (true) {
                const int row = ll_head->idx / 9;
                const int col = ll_head->idx % 9;

                // set a bit for each possibility, forcing the zeroth bit to be a 0
                const uint16_t possibilities = ~(rows[row] | cols[col] | squares[row / 3][col / 3]) & 0b1111111110;

                // if only one bit is set, the position only has a single possibility
                // we can get that possibility by adding one to the number of trailing zeros in the possibilities mask
                // zero is still technically in the bitmask, so 0b0000010 would indicate that 1 is the only possibility
                if (__builtin_popcount(possibilities) == 1) {
                    const uint8_t value = __builtin_ctz(possibilities);
                    instructions.unsolved[p][ll_head->idx] = value;

                    // update the row, column, and square occupied bitmasks
                    rows[row] |= possibilities;
                    cols[col] |= possibilities;
                    squares[row / 3][col / 3] |= possibilities;

                    // if we run through the whole puzzle, we know it's solved
                    if (ll_head->next == NULL) {
                        solved = true;
                        goto solved;
                    }
                    modified = true;
                    ll_head = ll_head->next;
                    head = ll_head;
                } else {
                    break;
                }
            }

            struct pz_empty_element *prev = ll_head;

            while (ll_head != NULL) {
                const int row = ll_head->idx / 9;
                const int col = ll_head->idx % 9;

                const uint16_t possibilities = ~(rows[row] | cols[col] | squares[row / 3][col / 3]) & 0b1111111110;
                 if (__builtin_popcount(possibilities) == 1) {
                     const uint8_t value = __builtin_ctz(possibilities);
                     instructions.unsolved[p][ll_head->idx] = value;

                     rows[row] |= possibilities;
                     cols[col] |= possibilities;
                     squares[row / 3][col / 3] |= possibilities;

                     prev->next = ll_head->next;

                     modified = true;
                 } else {
                     prev = ll_head;
                 }
                ll_head = ll_head->next;
            }

            if (!modified) {
                break;
            }
        }

    /// TODO: Proper backtracking solver to solve the remaining 1.7 million puzzles

    solved:
        if (solved) {
            __atomic_fetch_add(&num_solved, 1, __ATOMIC_RELAXED);
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

    if (unsolved == NULL || solved == NULL) {
        printf("Failed to allocate the solved or unsolved array!\n");
        exit(1);
    }

    thread_instructions *instructions = malloc(sizeof(thread_instructions) * num_threads);
    if (instructions == NULL) {
        printf("failed to allocate thread instructions!\n");
        exit(1);
    }

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    if (threads == NULL) {
        printf("failed to allocate thread array!");
        exit(1);
    }


    fclose(file);


    const int fd = open(filename, O_RDONLY);

    if (fd == -1) {
        perror("file open");
        exit(1);
    }

    char *puzzle_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0) + PUZZLE_FILE_HEADER_SIZE;
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

        if (pthread_create(threads + i, NULL, read_puzzles, instructions + i) != 0) {
            perror("pthread creation");
            exit(1);
        }
        allocated_puzzles += thread_puzzles;
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);
    if (munmap(puzzle_data - PUZZLE_FILE_HEADER_SIZE, file_size) == -1) {
        perror("failed to unmap puzzle file");
    }

    free(instructions);


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

int main(int argc, char **argv) {
    if (argc < 2) {
        perror("Missing puzzle file path");
        exit(1);
    }

    const long logical_cores = get_processor_count();

    if (logical_cores == -1) {
        perror("Logical core count unavailable.");
        exit(1);
    }

    printf("Logical cores: %ld\n", logical_cores);

    struct timespec start, stop;

    clock_gettime( CLOCK_REALTIME, &start);

    const puzzle_reader reader = read_puzzles_threaded(argv[1], logical_cores * 2);

    clock_gettime( CLOCK_REALTIME, &stop);

    const double delta = stop.tv_sec - start.tv_sec // NOLINT(*-narrowing-conversions)
            + (double)( stop.tv_nsec - start.tv_nsec )
              / (double)BILLION;

    printf("Read and parsed %ld puzzles, solved %d puzzles (%.2f%%) with the presolver in %f milliseconds (%f ns / puzzle)\n", reader.num_puzzles, num_solved, (num_solved / (float) reader.num_puzzles) * 100, delta * 1000, delta * 1000000000 / reader.num_puzzles ); // NOLINT(*-narrowing-conversions)

    destroy_puzzles(reader);

    return 0;
}
