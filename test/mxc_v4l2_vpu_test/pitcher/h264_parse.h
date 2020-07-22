/*
 * Copyright 2020 NXP
 *
 * include/h264_parse.h
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */
#ifndef _INCLUDE_H264_PARSE_H
#define _INCLUDE_H264_PARSE_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "parse.h"


#define AVC_SCODE       {0x0, 0x0, 0x0, 0x1}
#define AVC_NAL_TYPE    {0X5, 0X1}

int h264_parse(Parser p, void *arg);

#ifdef __cplusplus
}
#endif
#endif
