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
 * h264_parse.h
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
