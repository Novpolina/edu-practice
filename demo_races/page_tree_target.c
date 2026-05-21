// SPDX-License-Identifier: GPL-2.0
/*
 * Small userspace target for Page Tree Oracle demo.
 *
 * It touches heap and mmap-backed pages, prints its PID, and stays alive long
 * enough for /proc/page_tree_oracle to inspect its user page tables.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define HEAP_PAGES 8
#define MMAP_PAGES 4
#define TARGET_SLEEP_SECONDS 60

int main(void)
{
	long page_size = sysconf(_SC_PAGESIZE);
	size_t heap_len;
	size_t mmap_len;
	unsigned char *heap;
	unsigned char *area;
	volatile unsigned long checksum = 0;
	int i;

	if (page_size <= 0) {
		perror("sysconf(_SC_PAGESIZE)");
		return 1;
	}

	heap_len = HEAP_PAGES * (size_t)page_size;
	mmap_len = MMAP_PAGES * (size_t)page_size;

	heap = malloc(heap_len);
	if (!heap) {
		perror("malloc");
		return 1;
	}

	area = mmap(NULL, mmap_len, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == MAP_FAILED) {
		perror("mmap");
		free(heap);
		return 1;
	}

	for (i = 0; i < HEAP_PAGES; i++) {
		heap[i * page_size] = (unsigned char)(0x40 + i);
		checksum += heap[i * page_size];
	}

	for (i = 0; i < MMAP_PAGES; i++) {
		area[i * page_size] = (unsigned char)(0x80 + i);
		checksum += area[i * page_size];
	}

	printf("target pid: %ld\n", (long)getpid());
	printf("page_tree_target: heap=%p mmap=%p checksum=%lu\n",
	       heap, area, checksum);
	fflush(stdout);

	sleep(TARGET_SLEEP_SECONDS);

	munmap(area, mmap_len);
	free(heap);
	return 0;
}
