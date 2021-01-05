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
#include "h265_parse.h"

static int h265_check_frame_nal(int type)
{
	int i;
	char nal_type[] = HEVC_NAL_TYPE;

	type = (type & 0x7E) >> 1;
	for (i = 0; i < ARRAY_SIZE(nal_type); i++) {
		if (type == nal_type[i])
			return TRUE;
	}

	return FALSE;
}

int h265_parse(Parser p, void *arg)
{
	return pitcher_parse_h26x(p, h265_check_frame_nal);
}

