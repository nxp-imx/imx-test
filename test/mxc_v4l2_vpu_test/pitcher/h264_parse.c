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

static int h264_check_frame(uint8_t *p, uint32_t size)
{
	uint8_t type;

	if (size < 2)
		return PARSER_TYPE_UNKNOWN;

	type = p[0] & 0x1f;
	switch (type) {
	case 1: //Non-IDR
	case 5: //IDR
		if (p[1] & 0x80)
			return PARSER_TYPE_FRAME;
		else
			return PARSER_TYPE_UNKNOWN;
	case 7: //SPS
	case 8: //PPS
	case 6: //SEI
		return PARSER_TYPE_CONFIG;
	default:
		return PARSER_TYPE_UNKNOWN;
	}
}

static struct pitcher_parser_scode h264_scode = {
	.scode = 0x000001,
	.mask = 0xffffff,
	.num = 3,
	.extra_num = 4,
	.extra_code = 0x00000001,
	.extra_mask = 0xffffffff,
	.force_extra_on_first = 1,
	.check_frame = h264_check_frame
};

int h264_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &h264_scode);
}
