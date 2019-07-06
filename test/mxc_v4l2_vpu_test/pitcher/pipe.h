/*
 * Copyright 2018 NXP
 *
 * include/pipe.h
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#ifndef _INCLUDE_PIPE_H
#define _INCLUDE_PIPE_H
#ifdef __cplusplus
extern "C"
{
#endif

typedef void *Pipe;
typedef int (*notify_callback)(void *dst);

Pipe pitcher_new_pipe(void);
void pitcher_del_pipe(Pipe p);
void *pitcher_get_pipe_dst(Pipe p);
void pitcher_set_pipe_dst(Pipe p, void *dst);
void *pitcher_get_pipe_src(Pipe p);
void pitcher_set_pipe_src(Pipe p, void *src);
int pitcher_pipe_push_back(Pipe p, struct pitcher_buffer *buffer);
struct pitcher_buffer *pitcher_pipe_pop(Pipe p);
int pitcher_pipe_clear(Pipe p);
int pitcher_set_pipe_skip(Pipe p, uint32_t numerator, uint32_t denominator);
int pitcher_set_pipe_notify(Pipe p, notify_callback notify);
int pitcher_pipe_poll(Pipe p);

#ifdef __cplusplus
}
#endif
#endif
