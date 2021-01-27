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
 * pitcher.h
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#ifndef _INCLUDE_PITCHER_H
#define _INCLUDE_PITCHER_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <sys/epoll.h>

enum {
	PITCHER_STATE_STOPPED = 0,
	PITCHER_STATE_STOPPING,
	PITCHER_STATE_ACTIVE,
	PITCHER_STATE_UNKNOWN
};

enum {
	PITCHER_BUFFER_FLAG_LAST = (1 << 0),
};

enum {
	BUFFER_TYPE_DEFAULT = 0,
	BUFFER_TYPE_KEYFRAME = 1,
	BUFFER_TYPE_PFRAME,
	BUFFER_TYPE_BFRAME,
};

enum {
	MEM_MMAP = 0,
	MEM_ALLOC
};

#define BUFFER_TYPE_MASK	0x3
#define BUFFER_TYPE_OFFSET	16
#define SET_BUFFER_TYPE(flags, type)	\
	(flags) |= (((type) & BUFFER_TYPE_MASK) << BUFFER_TYPE_OFFSET)
#define GET_BUFFER_TYPE(flags, type)	\
	(((flags) >> BUFFER_TYPE_OFFSET) & BUFFER_TYPE_MASK)

struct pitcher_plane {
	void *virt;
	unsigned long size;
	unsigned long phys;
	unsigned long bytesused;
	unsigned long offset;
};

struct pitcher_buffer {
	unsigned int count;
	struct pitcher_plane *planes;
	unsigned int index;
	unsigned int flags;
	void *priv;
};

typedef int (*handle_plane)(struct pitcher_plane *plane,
				unsigned int index, void *arg);
typedef int (*handle_buffer)(struct pitcher_buffer *buffer,
				void *arg, int *del);

struct pitcher_buffer_desc {
	unsigned int plane_count;
	unsigned long plane_size;
	handle_plane init_plane;
	handle_plane uninit_plane;
	handle_buffer recycle;
	void *arg;
};

struct pitcher_buffer *pitcher_new_buffer(struct pitcher_buffer_desc *desc);
struct pitcher_buffer *pitcher_get_buffer(struct pitcher_buffer *buffer);
void pitcher_put_buffer(struct pitcher_buffer *buffer);
unsigned int pitcher_get_buffer_refcount(struct pitcher_buffer *buffer);

int pitcher_alloc_plane(struct pitcher_plane *plane,
			unsigned int index, void *arg);
int pitcher_free_plane(struct pitcher_plane *plane,
			unsigned int index, void *arg);

struct pitcher_unit_desc {
	char name[64];
	int (*init)(void *arg);
	int (*cleanup)(void *arg);
	struct pitcher_buffer *(*alloc_buffer)(void *arg);
	int (*start)(void *arg);
	int (*stop)(void *arg);
	int (*check_ready)(void *arg, int *is_end);
	int (*runfunc)(void *arg, struct pitcher_buffer *buffer);
	unsigned int buffer_count;
	int fd;
	unsigned int events;
};

typedef void *PitcherContext;

PitcherContext pitcher_init(void);
int pitcher_release(PitcherContext context);
int pitcher_start(PitcherContext context);
int pitcher_stop(PitcherContext context);
int pitcher_run(PitcherContext context);
int pitcher_register_chn(PitcherContext context,
			struct pitcher_unit_desc *desc, void *arg);
int pitcher_unregister_chn(unsigned int chnno);
int pitcher_connect(unsigned int src, unsigned int dst);
int pitcher_disconnect(unsigned int src, unsigned int dst);
int pitcher_get_source(unsigned int chnno);
int pitcher_get_sink(unsigned int chnno);
unsigned int pitcher_get_status(unsigned int chnno);
unsigned int pitcher_is_active(unsigned int chnno);
unsigned int pitcher_is_error(unsigned int chnno);
void pitcher_set_error(unsigned int chnno);
int pitcher_poll_idle_buffer(unsigned int chnno);
struct pitcher_buffer *pitcher_get_idle_buffer(unsigned int chnno);
void pitcher_put_buffer_idle(unsigned int chnno, struct pitcher_buffer *buffer);
void pitcher_push_back_output(unsigned int chnno, struct pitcher_buffer *buf);
int pitcher_chn_poll_input(unsigned int chnno);
int pitcher_start_chn(unsigned int chnno);
int pitcher_stop_chn(unsigned int chnno);
int pitcher_set_skip(unsigned int src, unsigned int dst,
			uint32_t numerator, uint32_t denominator);

#ifdef __cplusplus
}
#endif
#endif
