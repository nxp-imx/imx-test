/*
 * Copyright 2018-2021 NXP
 *
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
/*
 * memory.c
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
	PITCHER_LOG("++ %p <%s, %d>  %ld\n", ptr, func, line, size);
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
	PITCHER_LOG("++ %p <%s, %d>  %ld\n", ptr, func, line, nmemb * size);
#endif

	return ptr;
}

void *_pitcher_realloc(void *ptr, size_t size, const char *func, int line)
{
	void *ret;

	if (!ptr)
		atomic_inc(&total_count);
	ret = realloc(ptr, size);
#ifdef PITCHER_MEM_DEBUG
	PITCHER_LOG("== %p(%p) <%s, %d>  %ld\n", ret, ptr, func, line, size);
#endif
	return ret;
}

long pitcher_memory_count(void)
{
	return total_count;
}
