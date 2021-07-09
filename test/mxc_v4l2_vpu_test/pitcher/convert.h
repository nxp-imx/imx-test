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
 * convert.h
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */
#ifndef _INCLUDE_CONVERT_H
#define _INCLUDE_CONVERT_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "pitcher.h"

struct convert_ctx {
	struct pitcher_buffer *src_buf;
	struct pitcher_buffer *dst_buf;
	uint32_t src_fmt;
	uint32_t dst_fmt;
	uint32_t width;
	uint32_t height;
	uint32_t bytesperline;
	uint32_t field;
};

void convert_frame(struct convert_ctx *cvrt_ctx);

#ifdef __cplusplus
}
#endif
#endif
