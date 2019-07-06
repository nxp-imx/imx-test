/*
 * Copyright 2018 NXP
 *
 * include/loop.h
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#ifndef _INCLUDE_LOOP_H
#define _INCLUDE_LOOP_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <sys/epoll.h>

typedef void *Loop;

struct pitcher_poll_fd {
	int (*func)(struct pitcher_poll_fd *fd, unsigned int event, int *del);
	int fd;
	unsigned int events;
	unsigned int timeout;
	void *priv;
};

struct pitcher_timer_task {
	int (*func)(struct pitcher_timer_task *task, int *del);
	void *priv;
	unsigned int interval;
	int times;
};

Loop pitcher_open_loop(void);
void pitcher_close_loop(Loop loop);
int pitcher_loop_start(Loop loop);
int pitcher_loop_stop(Loop loop);
int pitcher_loop_run(Loop loop);
int pitcher_loop_add_poll_fd(Loop loop, struct pitcher_poll_fd *fd);
int pitcher_loop_add_task(Loop loop, struct pitcher_timer_task *task);
int pitcher_loop_del_poll_fd(Loop l, struct pitcher_poll_fd *fd);
int pitcher_loop_del_task(Loop l, struct pitcher_timer_task *task);

#ifdef __cplusplus
}
#endif
#endif
