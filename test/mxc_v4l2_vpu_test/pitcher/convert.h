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
	struct pitcher_buffer *src;
	struct pitcher_buffer *dst;
	int (*convert_frame)(struct convert_ctx *cvrt_ctx);
	void (*free)(struct convert_ctx *cvrt_ctx);
	void *priv;
};

struct convert_ctx *pitcher_create_sw_convert(void);
#ifdef ENABLE_G2D
struct convert_ctx *pitcher_create_g2d_convert(void);
#else
static inline struct convert_ctx *pitcher_create_g2d_convert(void) {return NULL;}
#endif

#ifdef __cplusplus
}
#endif
#endif
