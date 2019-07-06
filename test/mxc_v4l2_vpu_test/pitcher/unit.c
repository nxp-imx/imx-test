/*
 * Copyright 2018 NXP
 *
 * core/unit.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "queue.h"
#include "pipe.h"
#include "unit.h"

#ifndef MAX_UNIT_OUTPUT_COUNT
#define MAX_UNIT_OUTPUT_COUNT		8
#endif

struct pitcher_unit {
	struct pitcher_unit_desc desc;
	void *arg;
	Pipe in;
	Pipe outs[MAX_UNIT_OUTPUT_COUNT];
	Queue idles;
	unsigned int buffer_count;
	unsigned int enable;
};

Unit pitcher_new_unit(struct pitcher_unit_desc *desc, void *arg)
{
	struct pitcher_unit *unit = NULL;
	int ret;

	if (!desc || !desc->check_ready || !desc->runfunc)
		return NULL;

	unit = pitcher_calloc(1, sizeof(*unit));
	if (!unit)
		return NULL;

	memcpy(&unit->desc, desc, sizeof(*desc));
	unit->arg = arg;

	unit->idles = pitcher_init_queue();
	if (!unit->idles)
		goto error;

	if (unit->desc.init) {
		ret = unit->desc.init(unit->arg);
		if (ret < 0)
			goto error;
	}

	return unit;
error:
	SAFE_RELEASE(unit->idles, pitcher_destroy_queue);
	SAFE_RELEASE(unit, pitcher_free);
	return NULL;
}

static int __clear_buffer(unsigned long item, void *arg)
{
	struct pitcher_buffer *buffer = (struct pitcher_buffer *)item;

	SAFE_RELEASE(buffer, pitcher_put_buffer);

	return 1;
}

void pitcher_del_unit(Unit u)
{
	struct pitcher_unit *unit = u;

	if (!unit)
		return;

	if (unit->desc.cleanup)
		unit->desc.cleanup(unit->arg);
	if (unit->idles)
		pitcher_queue_clear(unit->idles, __clear_buffer, NULL);
	SAFE_RELEASE(unit->idles, pitcher_destroy_queue);
	SAFE_RELEASE(unit, pitcher_free);
}

int pitcher_set_unit_input(Unit u, Pipe p)
{
	struct pitcher_unit *unit = u;

	assert(unit);

	unit->in = p;

	return RET_OK;
}

int pitcher_add_unit_output(Unit u, Pipe p)
{
	struct pitcher_unit *unit = u;
	int i;

	assert(unit);

	for (i = 0; i < ARRAY_SIZE(unit->outs); i++) {
		if (unit->outs[i] == p)
			return RET_OK;
	}

	for (i = 0; i < ARRAY_SIZE(unit->outs); i++) {
		if (!unit->outs[i]) {
			unit->outs[i] = p;
			return RET_OK;
		}
	}

	return -RET_E_FULL;
}

int pitcher_rm_unit_output(Unit u, Pipe p)
{
	struct pitcher_unit *unit = u;
	int i;

	assert(unit);

	for (i = 0; i < ARRAY_SIZE(unit->outs); i++) {
		if (unit->outs[i] == p)
			unit->outs[i] = NULL;
	}

	return RET_OK;
}

static int __alloc_buffer(struct pitcher_unit *unit)
{
	int i;
	struct pitcher_buffer *buffer = NULL;

	assert(unit);

	if (!unit->desc.buffer_count)
		return RET_OK;
	if (!unit->desc.alloc_buffer)
		return -RET_E_NOSYS;

	for (i = 0; i < unit->desc.buffer_count; i++) {
		buffer = unit->desc.alloc_buffer(unit->arg);
		if (!buffer)
			break;
		pitcher_queue_push_back(unit->idles, (unsigned long)buffer);
	}

	unit->buffer_count = i;
	if (!unit->buffer_count)
		return -RET_E_NO_MEMORY;

	return RET_OK;
}

static int __del_buffer_func(unsigned long item, void *arg)
{
	struct pitcher_buffer *buffer = (struct pitcher_buffer *)item;

	SAFE_RELEASE(buffer, pitcher_put_buffer);

	return 1;
}

static int __free_buffer(struct pitcher_unit *unit)
{
	assert(unit);

	if (!unit->buffer_count)
		return RET_OK;

	pitcher_queue_clear(unit->idles, __del_buffer_func, unit);

	return RET_OK;
}

static int __clear_input_buffer(struct pitcher_unit *unit)
{
	assert(unit);

	if (!unit->in)
		return RET_OK;

	return pitcher_pipe_clear(unit->in);
}

int pitcher_unit_start(Unit u)
{
	struct pitcher_unit *unit = u;
	int ret;

	assert(unit);

	PITCHER_LOG("start : %s\n", unit->desc.name);

	ret = __alloc_buffer(unit);
	if (ret < 0) {
		PITCHER_ERR("%s alloc buffer fail\n", unit->desc.name);
		return ret;
	}

	if (unit->desc.start)
		ret = unit->desc.start(unit->arg);
	else
		ret = RET_OK;

	if (ret < 0) {
		PITCHER_ERR("[%s] start fail\n", unit->desc.name);
		__free_buffer(unit);
		return ret;
	}

	unit->enable = true;

	return ret;
}

int pitcher_unit_stop(Unit u)
{
	struct pitcher_unit *unit = u;
	int ret;

	assert(unit);

	PITCHER_LOG("stop : %s\n", unit->desc.name);

	if (unit->desc.stop)
		ret = unit->desc.stop(unit->arg);
	else
		ret = RET_OK;
	if (ret < 0) {
		PITCHER_ERR("[%s] stop fail\n", unit->desc.name);
		return ret;
	}

	unit->enable = false;

	ret = __free_buffer(unit);
	if (ret < 0)
		PITCHER_ERR("%s free buffer fail\n", unit->desc.name);

	__clear_input_buffer(unit);

	return ret;
}

int pitcher_unit_check_ready(Unit u, int *is_end)
{
	struct pitcher_unit *unit = u;
	int ret;
	int end = 0;

	assert(unit);
	assert(unit->desc.check_ready);

	ret = unit->desc.check_ready(unit->arg, &end);
	if (is_end)
		*is_end = end;

	return ret;
}

int pitcher_unit_run(Unit u)
{
	struct pitcher_unit *unit = u;
	struct pitcher_buffer *buffer = NULL;
	int ret;

	assert(unit);
	assert(unit->desc.runfunc);

	if (!unit->enable)
		return -RET_E_NOT_READY;

	if (unit->in)
		buffer = pitcher_pipe_pop(unit->in);

	ret = unit->desc.runfunc(unit->arg, buffer);
	SAFE_RELEASE(buffer, pitcher_put_buffer);

	return ret;
}

int pitcher_is_unit_idle_empty(Unit u)
{
	struct pitcher_unit *unit = u;

	assert(unit && unit->idles);

	return pitcher_queue_is_empty(unit->idles);
}

struct pitcher_buffer *pitcher_get_unit_idle_buffer(Unit u)
{
	struct pitcher_unit *unit = u;
	unsigned long item;
	int ret;

	assert(unit && unit->idles);
	ret = pitcher_queue_pop(unit->idles, &item);
	if (ret < 0)
		return NULL;

	return (struct pitcher_buffer *)item;
}

void pitcher_put_unit_buffer_idle(Unit u, struct pitcher_buffer *buffer)
{
	struct pitcher_unit *unit = u;

	assert(unit && unit->idles);
	if (!unit->enable)
		return;
	if (!buffer)
		return;

	pitcher_get_buffer(buffer);
	pitcher_queue_push_back(unit->idles, (unsigned long)buffer);
}

void pitcher_unit_push_back_output(Unit u, struct pitcher_buffer *buffer)
{
	struct pitcher_unit *unit = u;
	int i;

	assert(unit);

	if (!buffer)
		return;

	for (i = 0; i < ARRAY_SIZE(unit->outs); i++) {
		if (!unit->outs[i])
			continue;
		pitcher_pipe_push_back(unit->outs[i], buffer);
	}
}

Pipe pitcher_get_unit_source(Unit u)
{
	struct pitcher_unit *unit = u;

	assert(unit);

	return unit->in;
}
