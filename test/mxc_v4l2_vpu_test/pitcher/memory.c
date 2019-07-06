/*
 * Copyright 2018 NXP
 *
 * lib/memory.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"

/*#define PITCHER_MEM_DEBUG	1*/
static long total_count;

void *_pitcher_malloc(size_t size, const char *func, int line)
{
	void *ptr;

	atomic_inc(&total_count);
	ptr = malloc(size);
#ifdef PITCHER_MEM_DEBUG
	PITCHER_LOG("++ <%s, %d> %p, %ld\n", func, line, ptr, size);
#endif

	return ptr;
}

void pitcher_free(void *ptr)
{
	atomic_dec(&total_count);
#ifdef PITCHER_MEM_DEBUG
	PITCHER_LOG("-- %p\n", ptr);
#endif
	free(ptr);
}

void *_pitcher_calloc(size_t nmemb, size_t size, const char *func, int line)
{
	void *ptr;

	atomic_inc(&total_count);
	ptr = calloc(nmemb, size);
#ifdef PITCHER_MEM_DEBUG
	PITCHER_LOG("++ <%s, %d> %p, %ld\n", func, line, ptr, size);
#endif

	return ptr;
}

long pitcher_memory_count(void)
{
	return total_count;
}
