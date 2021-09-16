/*
 * Copyright 2021 NXP
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
 * mpegx_parse.c
 *
 * for mpeg4 / mpeg2 / xivd / avs / Divx4/5/6(unsupport Divx3)
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "parse.h"

static int mpeg4_check_frame(uint8_t *p, uint32_t size, void *priv)
{
	uint8_t type;

	if (!p || !size)
		return PARSER_TYPE_UNKNOWN;

	type = p[0];

	if (type == 0xB6)	//VOP
		return PARSER_TYPE_FRAME;
	else if (type == 0xB0)	//VOS
		return PARSER_TYPE_CONFIG;
	else if (type == 0xB3)	//group of vop start code
		return PARSER_TYPE_CONFIG;
	else if (type == 0xB5)	//visual object start code
		return PARSER_TYPE_CONFIG;
	else if (type >= 0 && type <= 0x2F)	//object/object layer start code
		return PARSER_TYPE_CONFIG;
	else
		return PARSER_TYPE_UNKNOWN;
}

static int mpeg2_check_frame(uint8_t *p, uint32_t size, void *priv)
{
	uint8_t type;

	if (!p || !size)
		return PARSER_TYPE_UNKNOWN;

	type = p[0];
	switch (type) {
	case 0x00: //Picture
		return PARSER_TYPE_FRAME;
	case 0xB2: //User Data
	case 0xB3: //Sequence
	case 0xB5: //Extension
	case 0xB8: //GOP
		return PARSER_TYPE_CONFIG;
	default:
		return PARSER_TYPE_UNKNOWN;
	}
}

static int avs_check_frame(uint8_t *p, uint32_t size, void *priv)
{
	uint8_t type;

	if (!p || !size)
		return PARSER_TYPE_UNKNOWN;

	type = p[0];
	switch (type) {
	case 0xB3: //I-Picture
	case 0xB6: //PB-Picture
		return PARSER_TYPE_FRAME;
	case 0xB0: //Sequence Header
	case 0xB2: //User Data
	case 0xB5: //Extension
		return PARSER_TYPE_CONFIG;
	default:
		return PARSER_TYPE_UNKNOWN;
	}
}

static struct pitcher_parser_scode mpeg2_scode = {
	.scode = 0x000001,
	.mask = 0xffffff,
	.num = 3,
	.check_frame = mpeg2_check_frame,
};

static struct pitcher_parser_scode mpeg4_scode = {
	.scode = 0x000001,
	.mask = 0xffffff,
	.num = 3,
	.check_frame = mpeg4_check_frame,
};

static struct pitcher_parser_scode avs_scode = {
	.scode = 0x000001,
	.mask = 0xffffff,
	.num = 3,
	.check_frame = avs_check_frame,
};

int mpeg4_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &mpeg4_scode);
}

int mpeg2_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &mpeg2_scode);
}

int xvid_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &mpeg4_scode);
}

int avs_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &avs_scode);
}

int divx_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &mpeg4_scode);
}
