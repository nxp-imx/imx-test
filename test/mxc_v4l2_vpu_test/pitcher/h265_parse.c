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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "parse.h"

struct h265_parse_t {
	uint32_t header_cnt;
	uint32_t config_found;
};

static int h265_check_frame(uint8_t *p, uint32_t size, void *priv)
{
	uint8_t type;
	struct h265_parse_t *info = priv;

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
		if (!info->header_cnt)
			return PARSER_TYPE_UNKNOWN;
		if (info->config_found) {
			info->config_found = 0;
			return PARSER_TYPE_FRAME;
		}
		if (p[2] & 0x80)
			return PARSER_TYPE_FRAME;
		else
			return PARSER_TYPE_UNKNOWN;
	case 33: //SPS
		info->header_cnt++;
	case 34: //PPS
	case 32: //VPS
	case 39: //Prefix SEI
	case 40: //Suffix SEI
		info->config_found = 1;
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
	.force_extra_on_first = 0,
	.check_frame = h265_check_frame,
	.priv_data_size = sizeof(struct h265_parse_t),
};

int h265_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &h265_scode);
}
