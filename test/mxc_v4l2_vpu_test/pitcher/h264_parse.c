
/*
 * Copyright 2020 NXP
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
#include "h264_parse.h"

static int h264_check_frame_nal(int type)
{
	int i;
	char nal_type[] = AVC_NAL_TYPE;

	type = type & 0x1f;

	for (i = 0; i < ARRAY_SIZE(nal_type); i++) {
		if (type == nal_type[i])
			return TRUE;
	}

	return FALSE;
}

int h264_parse(Parser p, void *arg)
{
	return pitcher_parse_h26x(p, h264_check_frame_nal);
}
