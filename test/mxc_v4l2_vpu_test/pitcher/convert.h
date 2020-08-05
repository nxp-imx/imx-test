/*
 * Copyright 2020 NXP
 *
 * include/convert.h
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */
#ifndef _INCLUDE_CONVERT_H
#define _INCLUDE_CONVERT_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "pitcher.h"

void convert_frame(struct pitcher_buffer *src, struct pitcher_buffer *dst,
		   uint32_t src_fmt, uint32_t dst_fmt,
		   uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif
#endif
