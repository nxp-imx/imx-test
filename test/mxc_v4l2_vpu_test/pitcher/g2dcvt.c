/*
 * Copyright(c) 2021 NXP. All rights reserved.
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
 * pitcher/g2dcvt.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <g2d.h>
#include <g2dExt.h>
#include "pitcher.h"
#include "pitcher_def.h"
#include "pitcher_v4l2.h"
#include "convert.h"
#include "dmabuf.h"
#include "platform_8x.h"
#include "loadso.h"

#define DEFAULT_G2D	"/usr/lib/libg2d.so"
#define LOAD_FUNC(g2d, NAME)	\
	g2d->NAME = pitcher_load_function(g2d->dll_handle, #NAME); \
	if  (!g2d->NAME) { \
		return -RET_E_NOSYS; \
	}

struct g2d_format_map {
	uint32_t format;
	enum g2d_format g2d_format;
	enum g2d_tiling tiling;
	int num_planes;
	int support_input;
	int support_output;
};

struct g2d_cvrt_t {
	void *g2d_handle;
	const struct g2d_format_map *fmt_s;
	const struct g2d_format_map *fmt_d;

	void *dll_handle;
	int (*g2d_open)(void **handle);
	int (*g2d_close)(void *handle);
	int (*g2d_blitEx)(void *handle, struct g2d_surfaceEx *srcEx, struct g2d_surfaceEx *dstEx);
	int (*g2d_finish)(void *handle);
};

const struct g2d_format_map supported_formats[] = {
	{PIX_FMT_NV12, G2D_NV12, G2D_LINEAR, 2, 1, 1},
	{PIX_FMT_I420, G2D_I420, G2D_LINEAR, 3, 1, 1},
	{PIX_FMT_YUYV, G2D_YUYV, G2D_LINEAR, 1, 1, 1},
	{PIX_FMT_NV12_8L128, G2D_NV12, G2D_AMPHION_TILED, 2, 1, 0},
	{PIX_FMT_NV12_10BE_8L128, G2D_NV12, G2D_AMPHION_TILED_10BIT, 2, 1,
	 0},
};

static const struct g2d_format_map *get_format_map(uint32_t format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_formats); i++) {
		if (supported_formats[i].format == format)
			return &supported_formats[i];
	}

	return NULL;
}

static int g2d_cvt_blit(struct g2d_cvrt_t *g2d, struct convert_ctx *ctx)
{
	struct g2d_surfaceEx src;
	struct g2d_surfaceEx dst;
	struct pitcher_buf_ref plane;
	int i;
	int ret;

	src.base.format = g2d->fmt_s->g2d_format;
	src.tiling = g2d->fmt_s->tiling;
	for (i = 0; i < g2d->fmt_s->num_planes; i++) {
		pitcher_get_buffer_plane(ctx->src, i, &plane);
		src.base.planes[i] = plane.phys;
	}

	src.base.left = 0;
	src.base.top = 0;
	src.base.right = src.base.left + ctx->src->format->width;
	src.base.bottom = src.base.top + ctx->src->format->height;
	src.base.global_alpha = 0xff;
	src.base.stride = ctx->src->format->planes[0].line;
	src.base.width = ctx->src->format->width;
	src.base.height = ctx->src->format->height;
	src.base.blendfunc = G2D_ONE;
	src.base.rot = G2D_ROTATION_0;
	if (ctx->src->format->interlaced)
		src.tiling |= G2D_AMPHION_INTERLACED;

	dst.base.format = g2d->fmt_d->g2d_format;
	dst.tiling = g2d->fmt_d->tiling;
	for (i = 0; i < g2d->fmt_d->num_planes; i++) {
		pitcher_get_buffer_plane(ctx->dst, i, &plane);
		dst.base.planes[i] = plane.phys;
	}
	dst.base.left = 0;
	dst.base.top = 0;
	dst.base.right = dst.base.left + ctx->dst->format->width;
	dst.base.bottom = dst.base.top + ctx->dst->format->height;
	dst.base.global_alpha = 0xff;
	/*  G2D stride is pixel, not bytes. */
	dst.base.stride = ctx->dst->format->width;
	dst.base.width = ctx->dst->format->width;
	dst.base.height = ctx->dst->format->height;
	dst.base.clrcolor = 0;
	dst.base.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
	dst.base.rot = G2D_ROTATION_0;

	/*
	 *printf("g2d src : %dx%d,%d(%d,%d-%d,%d), alpha=%d, format=%d, deinterlace: 0x%x\n",
	 *       src.base.width, src.base.height, src.base.stride, src.base.left,
	 *       src.base.top, src.base.right, src.base.bottom, src.base.global_alpha,
	 *       src.base.format, src.tiling);
	 *printf("g2d dest : %dx%d,%d(%d,%d-%d,%d), alpha=%d, format=%d\n",
	 *       dst.base.width, dst.base.height, dst.base.stride, dst.base.left,
	 *       dst.base.top, dst.base.right, dst.base.bottom, dst.base.global_alpha,
	 *       dst.base.format);
	 */
	ret = g2d->g2d_blitEx(g2d->g2d_handle, &src, &dst);
	ret |= g2d->g2d_finish(g2d->g2d_handle);
	if (ret)
		return -1;

	return 0;
}

int g2d_convert_frame(struct convert_ctx *ctx)
{
	struct g2d_cvrt_t *g2d;
	int ret;

	if (!ctx)
		return -RET_E_NULL_POINTER;

	if (!ctx || !ctx->priv || !ctx->src || !ctx->dst)
		return -RET_E_NULL_POINTER;

	if (!ctx->src->format || !ctx->dst->format)
		return -RET_E_NULL_POINTER;
	if (ctx->src->format->width != ctx->dst->format->width ||
	    ctx->src->format->height != ctx->dst->format->height)
		return -RET_E_INVAL;
	if (ctx->dst->format->interlaced) {
		PITCHER_ERR("not support to convert to interlaced format\n");
		return -RET_E_NOT_SUPPORT;
	}
	if (ctx->src->format->interlaced &&
	    ctx->src->format->format == PIX_FMT_NV12_10BE_8L128) {
		PITCHER_ERR ("not support to convert interlaced format %s\n",
				pitcher_get_format_name(ctx->src->format->format));
		return -RET_E_NOT_SUPPORT;

	}
	if (pitcher_buffer_is_dma_buf(ctx->src)
	    || pitcher_buffer_is_dma_buf(ctx->dst)) {
		PITCHER_ERR("g2d convert only support dma buffer\n");
		return -RET_E_NOT_SUPPORT;
	}

	g2d = ctx->priv;
	g2d->fmt_s = get_format_map(ctx->src->format->format);
	g2d->fmt_d = get_format_map(ctx->dst->format->format);
	if (!g2d->fmt_s || !g2d->fmt_d) {
		PITCHER_LOG
		    ("g2d does not support convert %s to %s\n",
		     pitcher_get_format_name(ctx->src->format->format),
		     pitcher_get_format_name(ctx->dst->format->format));
		return -RET_E_NOT_SUPPORT;
	}
	if (g2d->fmt_s->num_planes != ctx->src->format->num_planes ||
	    g2d->fmt_d->num_planes != ctx->dst->format->num_planes)
		return -RET_E_NOT_MATCH;

	pitcher_end_cpu_access(ctx->src, 1, 1);
	pitcher_end_cpu_access(ctx->dst, 1, 1);
	ret = g2d_cvt_blit(g2d, ctx);
	pitcher_start_cpu_access(ctx->dst, 1, 1);
	pitcher_start_cpu_access(ctx->src, 1, 1);

	return ret;
}

void g2d_free_convert(struct convert_ctx *ctx)
{
	struct g2d_cvrt_t *g2d;

	if (!ctx)
		return;

	g2d = ctx->priv;
	ctx->priv = NULL;
	if (g2d) {
		SAFE_RELEASE(g2d->g2d_handle, g2d->g2d_close);
		SAFE_RELEASE(g2d->dll_handle, pitcher_unload_object);
		SAFE_RELEASE(g2d, pitcher_free);
	}
	SAFE_RELEASE(ctx, pitcher_free);
}

static int g2d_load_function(struct g2d_cvrt_t *g2d)
{
	if (!g2d || !g2d->dll_handle)
		return -RET_E_NULL_POINTER;

	LOAD_FUNC(g2d, g2d_open);
	LOAD_FUNC(g2d, g2d_close);
	LOAD_FUNC(g2d, g2d_blitEx);
	LOAD_FUNC(g2d, g2d_finish);

	return RET_OK;
}

struct convert_ctx *pitcher_create_g2d_convert(void)
{
	struct convert_ctx *ctx = NULL;
	struct g2d_cvrt_t *g2d = NULL;
	int ret;

	ctx = pitcher_calloc(1, sizeof(*ctx));
	if (!ctx)
		goto error;
	g2d = pitcher_calloc(1, sizeof(*g2d));
	if (!g2d)
		goto error;

	g2d->dll_handle = pitcher_load_object(DEFAULT_G2D);
	if (!g2d->dll_handle)
		goto error;

	ret = g2d_load_function(g2d);
	if (ret)
		goto error;

	ret = g2d->g2d_open(&g2d->g2d_handle);
	if (ret < 0)
		goto error;

	ctx->convert_frame = g2d_convert_frame;
	ctx->free = g2d_free_convert;
	ctx->priv = g2d;

	return ctx;
error:
	if (g2d) {
		SAFE_RELEASE(g2d->dll_handle, pitcher_unload_object);
		SAFE_RELEASE(g2d, pitcher_free);
	}
	SAFE_RELEASE(ctx, pitcher_free);
	return NULL;
}
