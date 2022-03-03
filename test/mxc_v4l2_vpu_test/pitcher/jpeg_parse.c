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

static struct pitcher_parser_scode jpeg_scode = {
	.scode = 0xFFD8,
	.mask = 0xFFFF,
	.num = 2,
};

int jpeg_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &jpeg_scode);
}
