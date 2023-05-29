/*
 * Copyright 2023 NXP
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
#include <string.h>
#include "pitcher.h"
#include "pitcher_v4l2.h"
#include "pitcher_def.h"
#include "platform.h"
#include "platform_8x.h"

uint32_t get_platform_type(void)
{
	FILE *f = fopen("/sys/devices/soc0/soc_id", "r");
	char buf[32];
	int ret;

	ret = fread(buf, 1, sizeof(buf), f);
	if (!ret)
		return OTHERS;
	if (!strncmp(buf, "i.MX8QM", 7) || !strncmp(buf, "i.MX8QXP", 8))
		return IMX_8X;
	return IMX_8M;
}

int set_decoder_parameter(void *arg)
{
	struct platform_t *platform = NULL;
	int ret = RET_OK;

	if (!arg)
		return -RET_E_INVAL;

	platform = (struct platform_t *)arg;

	/* imx series common custom ioctl */
	if (platform->dis_reorder)
		ret = set_ctrl(platform->fd, V4L2_CID_DIS_REORDER, 1);

	/* platform special ioctl */
	if (platform->type == IMX_8X)
		set_decoder_parameter_8x(platform);

	return ret;
}
