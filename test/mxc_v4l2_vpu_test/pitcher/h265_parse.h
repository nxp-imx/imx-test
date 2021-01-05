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
 * h265_parse.h
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#ifndef _INCLUDE_H265_PARSE_H
#define _INCLUDE_H265_PARSE_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "parse.h"


#define HEVC_SCODE       {0x0, 0x0, 0x0, 0x1}
#define HEVC_NAL_TYPE   {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,\
                         0x8, 0x9, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15}

int h265_parse(Parser p, void *arg);

#ifdef __cplusplus
}
#endif
#endif
