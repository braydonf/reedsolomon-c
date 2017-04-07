#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "../rs.h"

int main(int argc, char *argv[]) {
    struct stat st;
    int parity_shards = 0;
    int data_shards = 0;
    int total_shards;
    uint64_t block_size;
    char* outfilename = NULL;
    char* filename = NULL, f[256];
    int i;
    int fd;
    reed_solomon* rs = NULL;
    uint8_t **data_blocks = NULL;
    uint8_t **fec_blocks = NULL;
    char output[256];
    uint64_t size;

    while(-1 != (i = getopt(argc, argv, "d:p:o:f:"))) {
        switch(i) {
            case 'd':
                data_shards = atoi(optarg);
                break;
            case 'p':
                parity_shards = atoi(optarg);
                break;
            case 'o':
                outfilename = optarg;
                break;
            case 'f':
                strcpy(f, optarg);
                filename = f;
                break;
            default:
                fprintf(stderr, "simple-encoder -d 10 -p 3 -o output -f " \
                        "filename.ext\n");
                exit(1);
        }
    }

    if (0 == parity_shards || 0 == data_shards || NULL == filename) {
        fprintf(stderr, "error input, example:\nsimple-encoder -d 10 -p 3 " \
                "-o output -f filename.ext\n");
        exit(1);
    }

    fec_init();

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "input file: %s not found\n", filename);
        exit(1);
    }

    fstat(fd, &st);
    size = st.st_size;
    block_size = (size+data_shards-1) / data_shards;
    total_shards = data_shards + parity_shards;
    printf("filename=%s size=%lu block_size=%li total_shards=%d\n", filename,
           size, block_size, total_shards);

    uint8_t *map = (uint8_t *)mmap(NULL, size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "input file: %s failed to memory map\n", filename);
        exit(1);
    }

    uint64_t parity_size = total_shards * block_size - size;
    printf("total_shards: %i\n", total_shards);
    printf("block_size: %lu\n", block_size);
    printf("parity_size: %lu\n", parity_size);

    int fd_parity = open(outfilename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (!fd_parity) {
        fprintf(stderr, "output file: %s failed\n", outfilename);
        exit(1);
    }
    int falloc_status = fallocate(fd_parity, FALLOC_FL_ZERO_RANGE, 0, parity_size);
    if (falloc_status) {
        fprintf(stderr, "output file: %s failed to allocate: %i\n", outfilename, falloc_status);
        exit(1);
    }
    uint8_t *map_parity = (uint8_t *)mmap(NULL, parity_size, PROT_READ | PROT_WRITE,
                                          MAP_SHARED, fd_parity, 0);
    if (map_parity == MAP_FAILED) {
        fprintf(stderr, "output file: %s failed to memory map\n", outfilename);
        exit(1);
    }

    data_blocks = (uint8_t**)malloc(data_shards * sizeof(uint8_t *));
    if (!data_blocks) {
        fprintf(stderr, "memory error: unable to malloc");
        exit(1);
    }

    for (i = 0; i < data_shards; i++) {
        data_blocks[i] = map + i * block_size;
    }

    fec_blocks = (uint8_t**)malloc(parity_shards * sizeof(uint8_t *));
    if (!fec_blocks) {
        fprintf(stderr, "memory error: unable to malloc");
        exit(1);
    }

    for (i = 0; i < parity_shards; i++) {
        fec_blocks[i] = map_parity + i * block_size;
    }

    printf("start encoding.\n");
    rs = reed_solomon_new(data_shards, parity_shards);
    reed_solomon_encode2(rs, data_blocks, fec_blocks, total_shards, block_size);
    printf("end encoding.\n");

    printf("begin corruption.\n");
    srand(time(NULL));
    uint8_t *zilch = (uint8_t *)calloc(1, total_shards);
    memset(zilch, 0, sizeof(zilch));
    for(i = 0; i < parity_shards; i++) {
        int corr = rand() % data_shards;
        printf("corrupting %d\n", corr);
        memset(map + corr * block_size, 137, block_size);
        zilch[corr] = 1;
    }
    printf("end corruption.\n");

    printf("begin reconstruction.\n");
    reed_solomon_reconstruct(rs, data_blocks, fec_blocks, zilch,
    total_shards, block_size);
    printf("end reconstruction.\n");

    munmap(map, size);
    munmap(map_parity, parity_size);
    close(fd);
    close(fd_parity);

    free(data_blocks);
    reed_solomon_release(rs);
    return 0;
}

