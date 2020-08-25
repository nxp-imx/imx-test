/*
 * Copyright 2018 NXP
 *
 * core/pipe.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "queue.h"
#include "pipe.h"

struct pitcher_pipe {
	void *src;
	void *dst;
	Queue queue;
	struct {
		uint32_t numerator;
		uint32_t denominator;
		uint32_t idx;
	} skip;
	notify_callback notify;
};

Pipe pitcher_new_pipe(void)
{
	struct pitcher_pipe *pipe;

	pipe = pitcher_calloc(1, sizeof(*pipe));
	if (!pipe)
		return NULL;

	pipe->queue = pitcher_init_queue();
	if (!pipe->queue) {
		SAFE_RELEASE(pipe, pitcher_free);
		return NULL;
	}

	pitcher_set_pipe_skip(pipe, 0, 1);

	return pipe;
}

static int __clear_queue_item(unsigned long item, void *arg)
{
	struct pitcher_buffer *buffer = (struct pitcher_buffer *)item;

	SAFE_RELEASE(buffer, pitcher_put_buffer);

	return 1;
}

void pitcher_del_pipe(Pipe p)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	if (pipe->queue) {
		pitcher_queue_clear(pipe->queue, __clear_queue_item, NULL);
		SAFE_RELEASE(pipe->queue, pitcher_destroy_queue);
	}

	SAFE_RELEASE(pipe, pitcher_free);
}

void *pitcher_get_pipe_dst(Pipe p)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	return pipe->dst;
}

void pitcher_set_pipe_dst(Pipe p, void *dst)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	pipe->dst = dst;
}

void *pitcher_get_pipe_src(Pipe p)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	return pipe->src;
}

void pitcher_set_pipe_src(Pipe p, void *src)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	pipe->src = src;
}

static int __check_is_need_skip(struct pitcher_pipe *pipe)
{
	int skip = 0;

	if (pipe->skip.numerator == 0 || pipe->skip.denominator == 0)
		return 0;

	if (pipe->skip.idx < pipe->skip.denominator - pipe->skip.numerator)
		skip = 0;
	else
		skip = 1;

	pipe->skip.idx++;
	pipe->skip.idx %= pipe->skip.denominator;

	return skip;
}

int pitcher_pipe_push_back(Pipe p, struct pitcher_buffer *buffer)
{
	struct pitcher_pipe *pipe = p;
	int ret;

	assert(pipe);
	if (!buffer)
		return -RET_E_NULL_POINTER;

	if (__check_is_need_skip(pipe))
		return RET_OK;

	pitcher_get_buffer(buffer);
	ret = pitcher_queue_push_back(pipe->queue, (unsigned long)buffer);
	if (pipe->dst && pipe->notify)
		pipe->notify(pipe->dst);

	return ret;
}

struct pitcher_buffer *pitcher_pipe_pop(Pipe p)
{
	struct pitcher_pipe *pipe = p;
	unsigned long item;
	int ret;

	assert(pipe);

	ret = pitcher_queue_pop(pipe->queue, &item);
	if (ret < 0)
		return NULL;

	return (struct pitcher_buffer *)item;
}

int pitcher_pipe_poll(Pipe p)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	if (pitcher_queue_is_empty(pipe->queue))
		return false;

	return true;
}

int pitcher_pipe_clear(Pipe p)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	pitcher_queue_clear(pipe->queue, __clear_queue_item, NULL);
	return RET_OK;
}

int pitcher_set_pipe_skip(Pipe p, uint32_t numerator, uint32_t denominator)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	if (!denominator || numerator > denominator)
		return -RET_E_INVAL;

	if (numerator && denominator) {
		uint32_t m = denominator;
		uint32_t n = numerator;

		while (n) {
			uint32_t tmp = m % n;

			m = n;
			n = tmp;
		}

		numerator /= m;
		denominator /= m;
	}

	pipe->skip.numerator = numerator;
	pipe->skip.denominator = denominator;

	return RET_OK;
}

int pitcher_set_pipe_notify(Pipe p, notify_callback notify)
{
	struct pitcher_pipe *pipe = p;

	assert(pipe);

	pipe->notify = notify;

	return RET_OK;
}

