/*
 * Copyright 2018 NXP
 *
 * include/pitcher_def.h
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#ifndef _INCLUDE_PITCHER_DEF_H
#define _INCLUDE_PITCHER_DEF_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <poll.h>

#ifndef true
#define true	1
#endif

#ifndef false
#define false	0
#endif

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

enum {
	RET_OK = 0,
	RET_E_NULL_POINTER = 10,
	RET_E_INVAL,
	RET_E_MMAP,
	RET_E_OPEN,
	RET_E_NO_MEMORY,
	RET_E_EMPTY,
	RET_E_FULL,
	RET_E_NOSYS,
	RET_E_NOT_READY,
	RET_E_NOT_MATCH,
	RET_E_NOT_FOUND,
	RET_E_NOT_SUPPORT,
};

#ifndef max
#define max(a,b)        (((a) < (b)) ? (b) : (a))
#endif

#ifndef min
#define min(a,b)        (((a) > (b)) ? (b) : (a))
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p, func)	\
	do {\
		if (p) {\
			func(p);\
			p = NULL;\
		} \
	} while (0)
#endif

#ifndef SAFE_CLOSE
#define SAFE_CLOSE(fd, close_func)	\
	do {\
		if (fd >= 0)\
			close_func(fd);\
		fd = -1;\
	} while (0)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d)  (((n) + (d) - 1) / (d))
#endif

#ifndef DIV_ROUND_OFF
#define DIV_ROUND_OFF(n, d) (((n) * 2 + (d)) / ((d) * 2))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array)	(sizeof(array) / sizeof((array)[0]))
#endif

#ifndef atomic_inc
#define atomic_inc(x)		__sync_add_and_fetch((x), 1)
#endif
#ifndef atomic_dec
#define atomic_dec(x)		__sync_sub_and_fetch((x), 1)
#endif
#ifndef atomic_add
#define atomic_add(x, y)	__sync_add_and_fetch((x), (y))
#endif
#ifndef atomic_sub
#define atomic_sub(x, y)	__sync_sub_and_fetch((x), (y))
#endif

#ifndef NSEC_PER_MSEC
#define	NSEC_PER_MSEC			1000000
#endif
#ifndef USEC_PER_MSEC
#define	USEC_PER_MSEC			1000
#endif

#ifdef LOG_TAG
#define _TAG    "["LOG_TAG"]"
#else
#define _TAG    ""
#endif

#define __PITCHER_LOG(fmt, arg...) \
	printf(_TAG"<%s, %d>"fmt, __func__, __LINE__, ##arg)

#define PITCHER_LOG(fmt, arg...) \
	printf(_TAG""fmt, ##arg)

#define PITCHER_ERR(...)	__PITCHER_LOG(__VA_ARGS__)

#define MAXPATHLEN	255

#ifndef ALIGN
#define ALIGN(x, a)		__ALIGN(x, (typeof(x))(a) - 1)
#define __ALIGN(x, mask)	(((x) + (mask)) & ~(mask))
#endif
#ifndef ALIGN_DOWN
#define ALIGN_DOWN		ALIGN((x) - ((a) - 1), (a))
#endif

int pitcher_poll(int fd, short events, int timeout);
uint64_t pitcher_get_realtime_time(void);
uint64_t pitcher_get_monotonic_time(void);
uint64_t pitcher_get_monotonic_raw_time(void);
long pitcher_get_file_size(const char *filename);
void *_pitcher_malloc(size_t size, const char *func, int line);
void *_pitcher_calloc(size_t nmemb, size_t size, const char *func, int line);
void pitcher_free(void *ptr);
long pitcher_memory_count(void);
#define pitcher_malloc(size)		\
		_pitcher_malloc(size, __func__, __LINE__)
#define pitcher_calloc(nmemb, size)	\
		_pitcher_calloc(nmemb, size, __func__, __LINE__)

#ifdef __cplusplus
}
#endif
#endif
