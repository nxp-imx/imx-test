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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "queue.h"
#include "loop.h"
#include "pipe.h"
#include "unit.h"
#include "list.h"

struct pitcher_core {
	Queue pipes;
	Queue chns;
	Loop loop;
	struct pitcher_timer_task task;
	unsigned int total_count;
	unsigned int enable_count;
};

struct pitcher_chn {
	struct list_head list;
	char name[64];
	unsigned int chnno;
	unsigned int error;
	Unit unit;
	unsigned int state;
	struct pitcher_poll_fd pfd;
	struct pitcher_core *core;
	unsigned int ignore_pollerr;
	uint32_t preferred_fourcc;
};

struct connect_t {
	struct pitcher_chn *src;
	struct pitcher_chn *dst;
	void *priv;
};

static unsigned long chn_bitmap[256];
static LIST_HEAD(chns);

static int __get_chnno(void)
{
	const unsigned int BITS_PER_LONG = sizeof(unsigned long) * 8;
	unsigned int i;
	unsigned int j;

	for (i = 0; i < ARRAY_SIZE(chn_bitmap); i++) {
		if (chn_bitmap[i] == (~0UL))
			continue;
		for (j = 0; j < BITS_PER_LONG; j++) {
			if (chn_bitmap[i] & (1UL << j))
				continue;
			chn_bitmap[i] |= (1UL << j);
			return i * BITS_PER_LONG + j;
		}
	}

	return -1;
}

static void __put_chnno(unsigned int chnno)
{
	const unsigned int BITS_PER_LONG = sizeof(unsigned long) * 8;
	unsigned int i = chnno / BITS_PER_LONG;
	unsigned int j = chnno % BITS_PER_LONG;

	if (i >= ARRAY_SIZE(chn_bitmap))
		return;

	chn_bitmap[i] &= (~(1UL << j));
}

static void __free_chn(struct pitcher_chn *chn)
{
	if (!chn)
		return;

	list_del_init(&chn->list);
	__put_chnno(chn->chnno);
	SAFE_RELEASE(chn->unit, pitcher_del_unit);
	SAFE_RELEASE(chn, pitcher_free);
}

static int __del_chn_func(unsigned long item, void *arg)
{
	struct pitcher_chn *chn = (struct pitcher_chn *)item;
	unsigned int chnno;

	if (!chn)
		return 0;

	if (arg) {
		chnno = *(unsigned int *)arg;
		if (chn->chnno != chnno)
			return 0;
	}

	__free_chn(chn);

	return 1;
}

static int __disconnect(unsigned long item, void *arg)
{
	Pipe pipe = (Pipe)item;
	struct connect_t *ct = arg;
	struct pitcher_chn *src;
	struct pitcher_chn *dst;

	if (!item)
		return 0;

	src = pitcher_get_pipe_src(pipe);
	dst = pitcher_get_pipe_dst(pipe);
	if (ct) {
		if (src != ct->src)
			return 0;
		if (dst != ct->dst)
			return 0;
	}

	pitcher_set_unit_input(dst, NULL);
	pitcher_rm_unit_output(src, pipe);
	SAFE_RELEASE(pipe, pitcher_del_pipe);

	return 1;
}

PitcherContext pitcher_init(void)
{
	struct pitcher_core *core = NULL;

	core = pitcher_calloc(1, sizeof(*core));

	core->chns = pitcher_init_queue();
	if (!core->chns)
		return NULL;

	core->pipes = pitcher_init_queue();
	if (!core->pipes) {
		pitcher_release(core);
		return NULL;
	}

	core->loop = pitcher_open_loop();
	if (!core->loop) {
		pitcher_release(core);
		return NULL;
	}

	return core;
}

int pitcher_release(PitcherContext context)
{
	struct pitcher_core *core = context;

	if (!core)
		return RET_OK;

	SAFE_RELEASE(core->loop, pitcher_close_loop);
	if (core->pipes)
		pitcher_queue_enumerate(core->pipes, __disconnect, NULL);
	SAFE_RELEASE(core->pipes, pitcher_destroy_queue);
	if (core->chns)
		pitcher_queue_enumerate(core->chns, __del_chn_func, NULL);
	SAFE_RELEASE(core->chns, pitcher_destroy_queue);
	SAFE_RELEASE(core, pitcher_free);

	return RET_OK;
}

static struct pitcher_chn *__find_chn(unsigned int chnno)
{
	struct pitcher_chn *chn;

	list_for_each_entry(chn, &chns, list) {
		if (chnno == chn->chnno)
			return chn;
	}

	return NULL;
}

static void __set_chn_status(struct pitcher_chn *chn, unsigned int state)
{
	if (state == PITCHER_STATE_STOPPING)
		PITCHER_LOG("stopping : %s\n", chn->name);
	if (state < PITCHER_STATE_UNKNOWN)
		chn->state = state;
}

static int __find_source_chn(unsigned long item, void *arg)
{
	Pipe pipe = (Pipe)item;
	struct connect_t *ct = arg;

	if (!pipe || !ct || !ct->dst)
		return 0;

	if (pitcher_get_pipe_dst(pipe) == ct->dst)
		ct->src = pitcher_get_pipe_src(pipe);

	return 0;
}

static int __find_sink_chn(unsigned long item, void *arg)
{
	Pipe pipe = (Pipe)item;
	struct connect_t *ct = arg;

	if (!pipe || !ct || !ct->src)
		return 0;

	if (pitcher_get_pipe_src(pipe) == ct->src)
		ct->dst = pitcher_get_pipe_dst(pipe);

	return 0;
}

static int __find_alive_sink_chn(unsigned long item, void *arg)
{
	Pipe pipe = (Pipe)item;
	struct connect_t *ct = arg;
	struct pitcher_chn *dst;

	if (!pipe || !ct || !ct->src)
		return 0;

	if (pitcher_get_pipe_src(pipe) == ct->src) {
		dst = pitcher_get_pipe_dst(pipe);
		if (dst->state != PITCHER_STATE_STOPPED)
			ct->dst = dst;
	}

	return 0;
}

static int __start_sink_chn(unsigned long item, void *arg)
{
	Pipe pipe = (Pipe)item;
	struct connect_t *ct = arg;

	if (!pipe || !ct || !ct->src)
		return 0;

	if (pitcher_get_pipe_src(pipe) != ct->src)
		return 0;

	ct->dst = pitcher_get_pipe_dst(pipe);
	if (!ct->dst)
		return 0;

	if (ct->dst->state == PITCHER_STATE_STOPPED)
		pitcher_start_chn(ct->dst->chnno);

	return 0;
}

static struct pitcher_chn *__get_source_chn(unsigned int chnno)
{
	struct pitcher_core *core;
	struct pitcher_chn *chn;
	struct connect_t ct;

	chn = __find_chn(chnno);
	if (!chn)
		return NULL;

	core = chn->core;
	assert(core);
	if (!core->chns || !core->pipes)
		return NULL;

	ct.src = NULL;
	ct.dst = chn;
	pitcher_queue_enumerate(core->pipes, __find_source_chn, (void *)&ct);
	if (!ct.src)
		return NULL;

	return ct.src;
}

static struct pitcher_chn *__get_sink_chn(unsigned int chnno)
{
	struct pitcher_core *core;
	struct pitcher_chn *chn;
	struct connect_t ct;

	chn = __find_chn(chnno);
	if (!chn)
		return NULL;

	core = chn->core;
	assert(core);
	if (!core->chns || !core->pipes)
		return NULL;

	ct.src = chn;
	ct.dst = NULL;
	pitcher_queue_enumerate(core->pipes, __find_sink_chn, (void *)&ct);
	if (!ct.dst)
		return NULL;

	return ct.dst;
}

static struct pitcher_chn *__get_alive_sink_chn(unsigned int chnno)
{
	struct pitcher_core *core;
	struct pitcher_chn *chn;
	struct connect_t ct;

	chn = __find_chn(chnno);
	if (!chn)
		return NULL;

	core = chn->core;
	assert(core);
	if (!core->chns || !core->pipes)
		return NULL;

	ct.src = chn;
	ct.dst = NULL;
	pitcher_queue_enumerate(core->pipes, __find_alive_sink_chn, (void *)&ct);
	if (!ct.dst)
		return NULL;

	return ct.dst;
}

static int __start_chn(unsigned long item, void *arg)
{
	struct pitcher_chn *chn = (struct pitcher_chn *)item;
	int ret;

	if (!chn)
		return 0;

	if (chn->state == PITCHER_STATE_ACTIVE)
		return 0;

	ret = pitcher_unit_start(chn->unit);
	if (!ret) {
		__set_chn_status(chn, PITCHER_STATE_ACTIVE);
		if (chn->pfd.func)
			pitcher_loop_add_poll_fd(chn->core->loop, &chn->pfd);
	}

	if (chn->state == PITCHER_STATE_ACTIVE)
		chn->core->enable_count++;
	chn->core->total_count++;

	return 0;
}

static int __stop_chn(unsigned long item, void *arg)
{
	struct pitcher_chn *chn = (struct pitcher_chn *)item;

	if (!chn)
		return 0;

	if (chn->state == PITCHER_STATE_STOPPED)
		return 0;

	if (chn->pfd.func)
		pitcher_loop_del_poll_fd(chn->core->loop, &chn->pfd);

	pitcher_unit_stop(chn->unit);
	__set_chn_status(chn, PITCHER_STATE_STOPPED);

	return 0;
}

static int __process_chn_active(struct pitcher_chn *chn)
{
	int ready = 0;
	int is_end = 0;

	if (!chn)
		return -RET_E_NULL_POINTER;
	if (chn->state != PITCHER_STATE_ACTIVE)
		return RET_OK;

	ready = pitcher_unit_check_ready(chn->unit, &is_end);
	if (ready)
		pitcher_unit_run(chn->unit);

	if (is_end)
		__set_chn_status(chn, PITCHER_STATE_STOPPING);

	return RET_OK;
}

static int __process_chn_stopping(struct pitcher_chn *chn)
{
	struct pitcher_chn *dst;

	if (!chn)
		return -RET_E_NULL_POINTER;
	if (chn->state != PITCHER_STATE_STOPPING)
		return RET_OK;

	dst = __get_alive_sink_chn(chn->chnno);
	if (dst)
		return RET_OK;

	pitcher_unit_stop(chn->unit);
	__set_chn_status(chn, PITCHER_STATE_STOPPED);

	return RET_OK;
}

static int __process_chn_run(struct pitcher_chn *chn)
{
	if (!chn)
		return -RET_E_NULL_POINTER;

	switch (chn->state) {
	case PITCHER_STATE_ACTIVE:
		__process_chn_active(chn);
		break;
	case PITCHER_STATE_STOPPING:
		__process_chn_stopping(chn);
		break;
	default:
		break;
	}

	return RET_OK;
}

static int __run_chn(unsigned long item, void *arg)
{
	struct pitcher_chn *chn = (struct pitcher_chn *)item;
	unsigned int *ptr = arg;

	if (!chn || !ptr)
		return 0;

	if (chn->state == PITCHER_STATE_STOPPED)
		return 0;

	__process_chn_run(chn);

	if (chn->state != PITCHER_STATE_STOPPED)
		(*ptr)++;

	return 0;
}

static int __timer_func(struct pitcher_timer_task *task, int *del)
{
	struct pitcher_core *core;
	unsigned int count = 0;

	core = container_of(task, struct pitcher_core, task);
	pitcher_queue_enumerate(core->chns, __run_chn, (void *)&count);

	if (!count) {
		pitcher_loop_stop(core->loop);
		if (del)
			*del = 1;
	}

	return 0;
}

static int __poll_func(struct pitcher_poll_fd *pfd,
			unsigned int events, int *del)
{
	struct pitcher_chn *chn;
	int is_del = false;

	assert(pfd);

	chn = container_of(pfd, struct pitcher_chn, pfd);
	if (chn->state != PITCHER_STATE_STOPPED) {
		/*if events == 0, it means timeout*/
		if (chn->pfd.events & events || !events)
			__process_chn_run(chn);
		else
			PITCHER_ERR("[%s] want event: 0x%x, but 0x%x\n",
					chn->name, chn->pfd.events, events);
		if ((events & POLLERR) && chn->state == PITCHER_STATE_ACTIVE) {
			PITCHER_LOG("%s POLLERR\n", chn->name);
			if (!chn->ignore_pollerr)
				chn->error = 1;
		}
	}

	if (chn->state == PITCHER_STATE_STOPPED)
		is_del = true;
	if (del)
		*del = is_del;
	return 0;
}

int pitcher_start(PitcherContext context)
{
	struct pitcher_core *core = context;

	assert(core);
	if (!core->loop || !core->chns)
		return -RET_E_INVAL;

	if (pitcher_queue_is_empty(core->chns))
		return -RET_E_EMPTY;

	core->total_count = 0;
	core->enable_count = 0;
	pitcher_queue_enumerate(core->chns, __start_chn, NULL);

	if (core->enable_count < core->total_count) {
		PITCHER_LOG("there are some chn start fail\n");
		pitcher_queue_enumerate(core->chns, __stop_chn, NULL);
	}

	core->task.func = __timer_func;
	core->task.interval = 0;
	core->task.times = -1;
	pitcher_loop_add_task(core->loop, &core->task);

	return pitcher_loop_start(core->loop);
}

int pitcher_run(PitcherContext context)
{
	struct pitcher_core *core = context;

	assert(core);
	if (!core->loop || !core->chns)
		return -RET_E_INVAL;

	return pitcher_loop_run(core->loop);
}

int pitcher_stop(PitcherContext context)
{
	struct pitcher_core *core = context;

	assert(core);
	if (!core->loop || !core->chns)
		return -RET_E_INVAL;

	pitcher_loop_stop(core->loop);
	pitcher_queue_enumerate(core->chns, __stop_chn, NULL);

	return RET_OK;
}

int pitcher_register_chn(PitcherContext context,
			struct pitcher_unit_desc *desc, void *arg)
{
	struct pitcher_chn *chn;
	struct pitcher_core *core = context;
	int chnno;

	assert(core);
	chnno = __get_chnno();
	if (chnno < 0)
		return -RET_E_FULL;

	if (!core->chns)
		return -RET_E_INVAL;

	chn = pitcher_calloc(1, sizeof(*chn));
	if (!chn)
		return -RET_E_NO_MEMORY;

	chn->unit = pitcher_new_unit(desc, arg);
	if (!chn->unit) {
		SAFE_RELEASE(chn, pitcher_free);
		return -RET_E_INVAL;
	}

	if (desc->fd >= 0 && desc->events) {
		chn->pfd.fd = desc->fd;
		chn->pfd.events = desc->events;
		chn->pfd.func = __poll_func;
	}

	snprintf(chn->name, sizeof(chn->name), "%s", desc->name);
	chn->chnno = chnno;
	chn->core = core;
	__set_chn_status(chn, PITCHER_STATE_STOPPED);
	list_add_tail(&chn->list, &chns);
	pitcher_queue_push_back(core->chns, (unsigned long)chn);

	return chn->chnno;
}

int pitcher_unregister_chn(unsigned int chnno)
{
	struct pitcher_chn *chn = __find_chn(chnno);
	struct pitcher_core *core;

	if (!chn)
		return -RET_E_NOT_FOUND;

	core = chn->core;
	assert(core && core->chns);
	pitcher_queue_enumerate(core->chns, __del_chn_func, (void *)&chnno);

	return RET_OK;
}

int pitcher_connect(unsigned int src, unsigned int dst)
{
	struct pitcher_core *core;
	struct pitcher_chn *schn = NULL;
	struct pitcher_chn *dchn = NULL;
	Pipe pipe;

	schn = __find_chn(src);
	dchn = __find_chn(dst);
	if (!schn || !dchn)
		return -RET_E_INVAL;
	if (schn->core != dchn->core)
		return -RET_E_NOT_MATCH;

	core = schn->core;
	assert(core);
	if (!core->chns || !core->pipes)
		return -RET_E_INVAL;

	pipe = pitcher_get_unit_source(dchn->unit);
	if (pipe && pitcher_get_pipe_src(pipe) != schn)
		return -RET_E_INVAL;

	pipe = pitcher_new_pipe();
	if (!pipe)
		return -RET_E_NO_MEMORY;

	pitcher_set_pipe_src(pipe, schn);
	pitcher_set_pipe_dst(pipe, dchn);
	pitcher_set_pipe_notify(pipe, (notify_callback)__process_chn_run);
	pitcher_set_unit_input(dchn->unit, pipe);
	pitcher_add_unit_output(schn->unit, pipe);

	pitcher_queue_push_back(core->pipes, (unsigned long)pipe);

	return RET_OK;
}

int pitcher_disconnect(unsigned int src, unsigned int dst)
{
	struct pitcher_core *core;
	struct connect_t ct;

	ct.src = __find_chn(src);
	ct.dst = __find_chn(dst);
	if (!ct.src || !ct.dst)
		return -RET_E_INVAL;
	if (ct.src->core != ct.dst->core)
		return -RET_E_NOT_MATCH;

	core = ct.src->core;
	assert(core);
	if (!core->chns || !core->pipes)
		return -RET_E_INVAL;

	pitcher_queue_enumerate(core->pipes, __disconnect, (void *)&ct);

	return 0;
}

int pitcher_poll_idle_buffer(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return false;

	if (pitcher_is_unit_idle_empty(chn->unit))
		return false;
	return true;
}

struct pitcher_buffer *pitcher_get_idle_buffer(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return NULL;

	return pitcher_get_unit_idle_buffer(chn->unit);
}

void pitcher_put_buffer_idle(unsigned int chnno, struct pitcher_buffer *buffer)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return;

	pitcher_put_unit_buffer_idle(chn->unit, buffer);
}

void pitcher_push_back_output(unsigned int chnno, struct pitcher_buffer *buffer)
{
	struct pitcher_chn *chn;
	struct connect_t ct;

	chn = __find_chn(chnno);
	if (!chn)
		return;
	if (!buffer)
		return;

	ct.src = chn;
	ct.dst = NULL;
	pitcher_queue_enumerate(chn->core->pipes, __start_sink_chn, (void *)&ct);

	pitcher_unit_push_back_output(chn->unit, buffer);
}

unsigned int pitcher_get_status(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return false;

	return chn->state;
}

unsigned int pitcher_is_active(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return false;

	return chn->state == PITCHER_STATE_ACTIVE;
}

unsigned int pitcher_is_error(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return 0;

	return chn->error;
}

void pitcher_set_error(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return;

	PITCHER_LOG("%s error\n", chn->name);
	chn->error = 1;
}

int pitcher_get_source(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __get_source_chn(chnno);
	if (!chn)
		return -RET_E_NOT_FOUND;

	return chn->chnno;
}

int pitcher_get_sink(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __get_sink_chn(chnno);
	if (!chn)
		return -RET_E_NOT_FOUND;

	return chn->chnno;
}

int pitcher_chn_poll_input(unsigned int chnno)
{
	struct pitcher_chn *chn;
	Pipe pipe;

	chn = __find_chn(chnno);
	if (!chn)
		return false;

	pipe = pitcher_get_unit_source(chn->unit);
	if (!pipe)
		return false;

	return pitcher_pipe_poll(pipe);
}

int pitcher_start_chn(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return false;

	return __start_chn((unsigned long)chn, NULL);
}

int pitcher_stop_chn(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return false;

	return __stop_chn((unsigned long)chn, NULL);
}

static int __get_pipe(unsigned long item, void *arg)
{
	Pipe pipe = (Pipe)item;
	struct connect_t *ct = arg;

	if (!item || !ct)
		return 0;

	if (pitcher_get_pipe_src(pipe) != ct->src)
		return 0;
	if (pitcher_get_pipe_dst(pipe) != ct->dst)
		return 0;

	ct->priv = pipe;

	return 0;
}

int pitcher_set_skip(unsigned int src, unsigned int dst,
			uint32_t numerator, uint32_t denominator)
{
	struct pitcher_core *core;
	struct connect_t ct;

	ct.src = __find_chn(src);
	ct.dst = __find_chn(dst);
	ct.priv = NULL;
	if (!ct.src || !ct.dst)
		return -RET_E_INVAL;
	if (ct.src->core != ct.dst->core)
		return -RET_E_NOT_MATCH;

	core = ct.src->core;
	assert(core);
	if (!core->chns || !core->pipes)
		return -RET_E_INVAL;

	pitcher_queue_enumerate(core->pipes, __get_pipe, (void *)&ct);
	if (!ct.priv)
		return -RET_E_NOT_MATCH;

	if (numerator > denominator)
		numerator = denominator;

	PITCHER_LOG("<%s, %s> skip %d/%d\n",
			ct.src->name, ct.dst->name, numerator, denominator);
	return pitcher_set_pipe_skip((Pipe)ct.priv, numerator, denominator);
}

void pitcher_set_ignore_pollerr(unsigned int chnno, unsigned int ignore)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return;

	chn->ignore_pollerr = ignore;
}

void pitcher_set_preferred_fourcc(unsigned int chnno, uint32_t fourcc)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return;

	chn->preferred_fourcc = fourcc;
}

uint32_t pitcher_get_preferred_fourcc(unsigned int chnno)
{
	struct pitcher_chn *chn;

	chn = __find_chn(chnno);
	if (!chn)
		return 0;

	return chn->preferred_fourcc;
}
