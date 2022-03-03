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

	if (platform->frame_mode == INPUT_MODE_NON_FRM_LEVEL)
		ret = set_ctrl(platform->fd, V4L2_CID_NON_FRAME, 1);

	return ret;
}
