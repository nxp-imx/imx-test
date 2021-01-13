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

static int h265_check_frame(uint8_t *p, uint32_t size)
{
	uint8_t type;

	if (size < 3)
		return PARSER_TYPE_UNKNOWN;

	type = (p[0] & 0x7E) >> 1;
	switch (type) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 16:
	case 17:
	case 18:
	case 19:
	case 20:
	case 21:
		if (p[2] & 0x80)
			return PARSER_TYPE_FRAME;
		else
			return PARSER_TYPE_UNKNOWN;
	case 32: //VPS
	case 33: //SPS
	case 34: //PPS
	case 39: //Prefix SEI
	case 40: //Suffix SEI
		return PARSER_TYPE_CONFIG;
	default:
		return PARSER_TYPE_UNKNOWN;

	}
}

static struct pitcher_parser_scode h265_scode = {
	.scode = 0x000001,
	.mask = 0xffffff,
	.num = 3,
	.extra_num = 4,
	.extra_code = 0x00000001,
	.extra_mask = 0xffffffff,
	.force_extra_on_first = 1,
	.check_frame = h265_check_frame
};

int h265_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &h265_scode);
}

