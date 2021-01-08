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
 * h265_parse.c
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "parse.h"

#define HEVC_SCODE       {0x0, 0x0, 0x0, 0x1}
#define HEVC_NAL_TYPE   {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,\
                         0x8, 0x9, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15}

static int h265_check_frame_nal(char *p)
{
	int i;
	char nal_type[] = HEVC_NAL_TYPE;

	for (i = 0; i < ARRAY_SIZE(nal_type); i++) {
		if (((p[0] & 0x7E) >> 1) == nal_type[i] && (p[2] & 0x80))
			return TRUE;
	}

	return FALSE;
}

int h265_parse(Parser p, void *arg)
{
	return pitcher_parse_h26x(p, h265_check_frame_nal);
}

