/*
 * Copyright 2020 NXP
 *
 * platform_8x.c
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher.h"
#include "pitcher_v4l2.h"
#include "pitcher_def.h"
#include "platform.h"
#include "platform_8x.h"


int set_decoder_parameter_8x(void *arg)
{
	struct platform_t *platform = NULL;
	int ret = RET_OK;

	if (!arg)
		return -RET_E_INVAL;

	platform = (struct platform_t *)arg;

	if (platform->frame_mode != INPUT_MODE_INVALID) {
		ret = set_ctrl(platform->fd, V4L2_CID_USER_RAW_BASE, 1);
		ret |= set_ctrl(platform->fd, V4L2_CID_USER_STREAM_INPUT_MODE, platform->frame_mode);
	}

	return ret;
}

