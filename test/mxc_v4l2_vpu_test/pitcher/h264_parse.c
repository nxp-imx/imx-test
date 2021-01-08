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
 *
 * h264_parse.c
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "parse.h"

#define AVC_SCODE       {0x0, 0x0, 0x0, 0x1}
#define AVC_NAL_TYPE    {0X5, 0X1}

static int h264_check_frame_nal(char *p)
{
	int i;
	char nal_type[] = AVC_NAL_TYPE;


	for (i = 0; i < ARRAY_SIZE(nal_type); i++) {
		if ((p[0] & 0x1f) == nal_type[i])
			return TRUE;
	}

	return FALSE;
}

int h264_parse(Parser p, void *arg)
{
	return pitcher_parse_h26x(p, h264_check_frame_nal);
}
