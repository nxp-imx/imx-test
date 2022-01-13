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
 * loop.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include "pitcher_def.h"
#include "queue.h"
#include "loop.h"

#define LOOP_TIMEOUT_DEFAULT		1
#define SIG_TERM_LOOP			(__SIGRTMIN + 0x100)

struct pitcher_loop_t {
	int epoll_fd;
	int running;

	Queue pools;
	Queue tasks;

	unsigned int timeout;
	pid_t tid;

	uint64_t tv;
	unsigned int left_time;
};

struct pitcher_loop_node {
	unsigned long key;
	uint64_t tv;
	unsigned int timeout;
	int times;
	struct pitcher_loop_t *loop;
};

static void __set_loop_status(struct pitcher_loop_t *loop, int status)
{
	loop->running = status;
}

static void __set_loop_timeout(struct pitcher_loop_t *loop,
				unsigned int timeout)
{
	loop->timeout = timeout;
}

static void __free_poll_node(struct pitcher_loop_node *node)
{
	struct pitcher_poll_fd *pfd;
	struct pitcher_loop_t *loop;

	if (!node)
		return;

	loop = node->loop;
	pfd = (struct pitcher_poll_fd *)node->key;
	if (loop && pfd)
		epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, pfd->fd, NULL);

	SAFE_RELEASE(node, pitcher_free);
}

static int __del_task(unsigned long item, void *arg)
{
	struct pitcher_loop_node *node = (struct pitcher_loop_node *)item;
	unsigned long key = (unsigned long)arg;

	if (!node)
		return 1;

	if (key && node->key != key)
		return 0;

	SAFE_RELEASE(node, pitcher_free);

	return 1;
}

static int __del_poll(unsigned long item, void *arg)
{
	struct pitcher_loop_node *node = (struct pitcher_loop_node *)item;
	unsigned long key = (unsigned long)arg;

	if (!node)
		return 1;

	if (key && node->key != key)
		return 0;

	__free_poll_node(node);

	return 1;
}

static int __timer_func(unsigned long item, void *arg)
{
	struct pitcher_loop_node *node = (struct pitcher_loop_node *)item;
	struct pitcher_loop_t *loop = arg;
	struct pitcher_timer_task *task;
	uint64_t delta;
	int is_del = 0;

	if (!loop)
		return 0;
	if (!node)
		return 1;
	task = (struct pitcher_timer_task *)node->key;
	if (!task) {
		SAFE_RELEASE(node, pitcher_free);
		return 1;
	}

	delta = (loop->tv - node->tv) / NSEC_PER_MSEC;
	node->tv = loop->tv;
	if (!task->interval || delta >= node->timeout) {
		node->timeout = task->interval;
		if (node->times > 0)
			node->times--;
		if (task->func)
			task->func(task, &is_del);
	} else {
		node->timeout -= delta;
	}

	if (is_del || node->times == 0) {
		SAFE_RELEASE(node, pitcher_free);
		return 1;
	} else if (loop->left_time > node->timeout) {
		loop->left_time = node->timeout;
	}

	return 0;
}

static int __process_timer(struct pitcher_loop_t *loop)
{
	pitcher_queue_enumerate(loop->tasks, __timer_func, (void *)loop);

	return RET_OK;
}

static int __epoll_func(unsigned long item, void *arg)
{
	struct pitcher_loop_node *node = (struct pitcher_loop_node *)item;
	struct pitcher_loop_t *loop = arg;
	struct pitcher_poll_fd *pfd;
	uint64_t delta;
	int is_del = 0;

	if (!loop)
		return 0;
	if (!node)
		return 1;

	pfd = (struct pitcher_poll_fd *)node->key;
	if (!pfd) {
		SAFE_RELEASE(node, pitcher_free);
		return 1;
	}

	delta = (loop->tv - node->tv) / NSEC_PER_MSEC;
	node->tv = loop->tv;
	if (delta >= node->timeout) {
		node->timeout = pfd->timeout;
		if (pfd->func)
			pfd->func(pfd, 0, &is_del);
	} else {
		node->timeout -= delta;
	}

	if (is_del) {
		__free_poll_node(node);
		return 1;
	} else if (loop->left_time > node->timeout) {
		loop->left_time = node->timeout;
	}

	return 0;
}

static int __process_poll(struct pitcher_loop_t *loop)
{
	int nfds;
	int i;
	const int ECOUNT = 3;
	struct epoll_event events[ECOUNT];

	pitcher_queue_enumerate(loop->pools, __epoll_func, (void *)loop);

	nfds = epoll_wait(loop->epoll_fd, events, ECOUNT, loop->left_time);

	for (i = 0; i < nfds; i++) {
		struct pitcher_loop_node *node = events[i].data.ptr;
		struct pitcher_poll_fd *pfd;
		int is_del = 0;

		if (!node)
			continue;
		pfd = (struct pitcher_poll_fd *)node->key;
		if (!pfd)
			continue;
		if (pfd->func)
			pfd->func(pfd, events[i].events, &is_del);

		if (is_del)
			pitcher_queue_enumerate(loop->pools,
						__del_poll, (void *)pfd);
	}

	return RET_OK;
}

static int __pre_process(struct pitcher_loop_t *loop)
{
	loop->left_time = loop->timeout;
	loop->tv = pitcher_get_monotonic_raw_time();

	return RET_OK;
}

static int __post_process(struct pitcher_loop_t *loop)
{
	return RET_OK;
}

Loop pitcher_open_loop(void)
{
	struct pitcher_loop_t *loop;
	unsigned int flags;

	loop = pitcher_calloc(1, sizeof(*loop));
	if (!loop)
		return NULL;
	loop->epoll_fd = -1;

	loop->epoll_fd = epoll_create(1);
	if (!loop->epoll_fd) {
		PITCHER_ERR("epoll create fail, %s\n", strerror(errno));
		goto error;
	}
	flags = fcntl(loop->epoll_fd, F_GETFD);
	flags |= FD_CLOEXEC;
	if (fcntl(loop->epoll_fd, flags) == -1)
		goto error;

	loop->pools = pitcher_init_queue();
	if (!loop->pools)
		goto error;
	loop->tasks = pitcher_init_queue();
	if (!loop->tasks)
		goto error;

	__set_loop_status(loop, 0);
	__set_loop_timeout(loop, LOOP_TIMEOUT_DEFAULT);

	return loop;
error:
	SAFE_RELEASE(loop->pools, pitcher_destroy_queue);
	SAFE_RELEASE(loop->tasks, pitcher_destroy_queue);
	SAFE_CLOSE(loop->epoll_fd, close);
	SAFE_RELEASE(loop, pitcher_free);
	return NULL;
}

void pitcher_close_loop(Loop l)
{
	struct pitcher_loop_t *loop = l;

	assert(loop);
	assert(loop->epoll_fd >= 0);
	assert(!loop->running);

	pitcher_queue_clear(loop->tasks, __del_task, NULL);
	pitcher_queue_clear(loop->pools, __del_poll, NULL);

	SAFE_RELEASE(loop->pools, pitcher_destroy_queue);
	SAFE_RELEASE(loop->tasks, pitcher_destroy_queue);
	SAFE_CLOSE(loop->epoll_fd, close);
	SAFE_RELEASE(loop, pitcher_free);
}

int pitcher_loop_start(Loop l)
{
	struct pitcher_loop_t *loop = l;

	assert(loop);
	assert(loop->epoll_fd >= 0);

	__set_loop_status(loop, 1);

	return RET_OK;
}

int pitcher_loop_stop(Loop l)
{
	struct pitcher_loop_t *loop = l;

	assert(loop);
	assert(loop->epoll_fd >= 0);

	__set_loop_status(loop, 0);
	if (loop->tid && loop->tid != syscall(SYS_gettid))
		kill(loop->tid, SIG_TERM_LOOP);

	return RET_OK;
}

int pitcher_loop_run(Loop l)
{
	struct pitcher_loop_t *loop = l;

	assert(loop);
	assert(loop->epoll_fd >= 0);

	loop->tid = syscall(SYS_gettid);
	usleep(1000);

	PITCHER_LOG("Loop run\n");
	while (loop->running) {
		__pre_process(loop);
		__process_timer(loop);
		__process_poll(loop);
		__post_process(loop);
	}
	PITCHER_LOG("Loop done\n");

	loop->tid = 0;

	return RET_OK;
}

int pitcher_loop_add_poll_fd(Loop l, struct pitcher_poll_fd *fd)
{
	struct pitcher_loop_t *loop = l;
	struct pitcher_loop_node *node = NULL;
	struct epoll_event ev;
	int ret;

	assert(loop);
	assert(loop->epoll_fd >= 0);

	if (!fd || !fd->func || fd->fd < 0 || !fd->events)
		return -RET_E_INVAL;

	if (!fd->timeout)
		fd->timeout = LOOP_TIMEOUT_DEFAULT;

	node = pitcher_calloc(1, sizeof(*node));
	if (!node)
		return -RET_E_NO_MEMORY;

	node->key = (unsigned long)fd;
	node->tv = pitcher_get_monotonic_raw_time();
	node->timeout = fd->timeout;
	node->loop = loop;

	ev.events = fd->events;
	ev.data.ptr = node;
	ret = epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd->fd, &ev);
	if (ret < 0) {
		SAFE_RELEASE(node, pitcher_free);
		return -RET_E_INVAL;
	}

	ret = pitcher_queue_push_back(loop->pools, (unsigned long)node);
	if (ret < 0) {
		SAFE_RELEASE(node, pitcher_free);
		return ret;
	}

	return RET_OK;
}

int pitcher_loop_del_poll_fd(Loop l, struct pitcher_poll_fd *fd)
{
	struct pitcher_loop_t *loop = l;

	assert(loop);
	assert(loop->epoll_fd >= 0);

	pitcher_queue_enumerate(loop->pools, __del_poll, (void *)fd);

	return RET_OK;
}

int pitcher_loop_add_task(Loop l, struct pitcher_timer_task *task)
{
	struct pitcher_loop_t *loop = l;
	struct pitcher_loop_node *node = NULL;
	int ret;

	assert(loop);
	assert(loop->epoll_fd >= 0);

	if (!task || !task->func)
		return -RET_E_INVAL;

	node = pitcher_calloc(1, sizeof(*node));
	if (!node)
		return -RET_E_NO_MEMORY;

	if (!task->interval)
		task->interval = LOOP_TIMEOUT_DEFAULT;
	node->key = (unsigned long)task;
	node->tv = pitcher_get_monotonic_raw_time();
	node->timeout = task->interval;
	node->times = task->times;
	node->loop = loop;

	ret = pitcher_queue_push_back(loop->tasks, (unsigned long)node);
	if (ret < 0) {
		SAFE_RELEASE(node, pitcher_free);
		return ret;
	}

	return RET_OK;
}

int pitcher_loop_del_task(Loop l, struct pitcher_timer_task *task)
{
	struct pitcher_loop_t *loop = l;

	assert(loop);
	assert(loop->epoll_fd >= 0);

	pitcher_queue_enumerate(loop->tasks, __del_task, (void *)task);

	return RET_OK;
}
