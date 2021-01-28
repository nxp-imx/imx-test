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
 * parse.c
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "pitcher_v4l2.h"
#include "platform_8x.h"
#include "parse.h"

struct parse_handler {
	unsigned int format;
	int (*handle_parse)(Parser p, void *arg);
};

void get_kmp_next(const char *p, int64_t *next, int64_t size)
{
	int64_t k = -1;
	int64_t j = 0;

	next[0] = -1;
	while (j < size - 1) {
		if (k == -1 || p[j] == p[k]) {
			++k;
			++j;
			next[j] = k;
		} else {
			k = next[k];
		}
	}
}

int64_t kmp_search(char *s, int64_t s_len, const char *p, int64_t p_len, int64_t *next)
{
	int64_t i = 0;
	int64_t j = 0;

	while (i < s_len && j < p_len) {
		if (j == -1 || s[i] == p[j]) {
			i++;
			j++;
		} else {
			j = next[j];
		}
	}
	if (j == p_len)
		return i - j;
	else
		return -1;
}

void pitcher_parser_push(Parser p, struct pitcher_frame *frame)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;

	if (!parser || !frame)
		return;

	list_add_tail(&frame->list, &parser->queue);
	parser->number++;
}

struct pitcher_frame *pitcher_parser_cur_frame(Parser p)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;

	if (parser->cur_frame && parser->cur_frame == pitcher_parser_last_frame(p))
		parser->cur_frame->flag = PITCHER_BUFFER_FLAG_LAST;

	return parser->cur_frame;
}

struct pitcher_frame *pitcher_parser_first_frame(Parser p)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;

	return list_first_entry(&parser->queue, struct pitcher_frame, list);
}

struct pitcher_frame * pitcher_parser_last_frame(Parser p)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;

	return list_last_entry(&parser->queue, struct pitcher_frame, list);
}

void pitcher_parser_seek_to_begin(Parser p)
{
    struct pitcher_parser *parser = (struct pitcher_parser *)p;

    parser->cur_frame = pitcher_parser_first_frame(p);
}

void pitcher_parser_to_next_frame(Parser p)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;

	if (!parser->cur_frame)
		return;

	if (parser->cur_frame == pitcher_parser_last_frame(p)) {
		parser->cur_frame = NULL;
		return;
	}

	parser->cur_frame = list_next_entry(parser->cur_frame, list);
}

void pitcher_parser_to_prev_frame(Parser p)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;

	parser->cur_frame = list_prev_entry(parser->cur_frame, list);
}

struct pitcher_parser *pitcher_new_parser(void)
{
	struct pitcher_parser *parser;

	parser = pitcher_calloc(1, sizeof(*parser));
	if (!parser)
		return NULL;

	return parser;
}

void pitcher_init_parser(Parser p)
{
	struct pitcher_parser *parser = NULL;

	if (!p)
		return;

	parser = (struct pitcher_parser *)p;
	INIT_LIST_HEAD(&parser->queue);
}

void pitcher_del_parser(Parser p)
{
	struct pitcher_parser *parser = NULL;
	struct pitcher_frame *frame;
	struct pitcher_frame *tmp;

	if (!p)
		return;

	parser = (struct pitcher_parser *)p;

	list_for_each_entry_safe(frame, tmp, &parser->queue, list) {
		list_del_init(&frame->list);
		SAFE_RELEASE(frame, pitcher_free);
	}

	SAFE_RELEASE(p, pitcher_free);
}

void pitcher_parser_show(Parser p)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;
	struct pitcher_frame *frame;
	struct pitcher_frame *tmp;
	int size = 0;

	if (!parser)
		return;

	list_for_each_entry_safe(frame, tmp, &parser->queue, list) {
		PITCHER_LOG("[%d] size:%ld, offset:0x%x\n", frame->idx, frame->size, frame->offset);
		 PITCHER_LOG("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
						parser->virt[frame->offset],
						parser->virt[frame->offset+1],
						parser->virt[frame->offset+2],
						parser->virt[frame->offset+3],
						parser->virt[frame->offset+4],
						parser->virt[frame->offset+5],
						parser->virt[frame->offset+6],
						parser->virt[frame->offset+7]);
		 size += frame->size;
	}
	PITCHER_LOG("total size: 0x%x\n", size);
}

struct parse_handler parse_handler_table[] = {
	{.format = V4L2_PIX_FMT_H264,
	 .handle_parse = h264_parse,
	},
	{.format = V4L2_PIX_FMT_HEVC,
	 .handle_parse = h265_parse,
	},
	{.format = V4L2_PIX_FMT_JPEG,
	 .handle_parse = jpeg_parse,
	},
	{.format = V4L2_PIX_FMT_H263,
	 .handle_parse = h263_parse,
	},
	{.format = VPU_PIX_FMT_SPK,
	 .handle_parse = spk_parse,
	},
	{.format = V4L2_PIX_FMT_MPEG4,
	 .handle_parse = mpeg4_parse,
	},
	{.format = V4L2_PIX_FMT_MPEG2,
	 .handle_parse = mpeg2_parse,
	},
	{.format = V4L2_PIX_FMT_XVID,
	 .handle_parse = xvid_parse,
	},
	{.format = VPU_PIX_FMT_AVS,
	 .handle_parse = avs_parse,
	},
	{.format = V4L2_PIX_FMT_VP8,
	 .handle_parse = vp8_parse,
	},
	{.format = V4L2_PIX_FMT_VP9,
	 .handle_parse = vp9_parse,
	},
	{.format = V4L2_PIX_FMT_VC1_ANNEX_L,
	 .handle_parse = vc1l_parse,
	},
	{.format = V4L2_PIX_FMT_VC1_ANNEX_G,
	 .handle_parse = vc1g_parse,
	},
	{.format = VPU_PIX_FMT_VP6,
	 .handle_parse = vp6_parse,
	},
	{.format = VPU_PIX_FMT_DIVX,
	 .handle_parse = divx_parse,
	},
#ifdef RV_PARSE
	{.format = VPU_PIX_FMT_RV,
	 .handle_parse = rv_parse,
	},
#endif
	{0, 0},
};

struct parse_handler *find_handler(unsigned int fmt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(parse_handler_table); i++) {
		if (parse_handler_table[i].format == fmt)
			return &parse_handler_table[i];
	}

	return NULL;
}

int is_support_parser(unsigned int fmt)
{
	if (find_handler(fmt) == NULL)
		return false;

	return true;
}

int pitcher_parse(Parser p)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;
	struct parse_handler *handler;

	if (!p)
		return -RET_E_INVAL;

	handler = find_handler(parser->format);
	if (handler == NULL)
		return -RET_E_NOT_SUPPORT;

	return handler->handle_parse(p, handler);
}

int pitcher_parser_push_new_frame(Parser p, int64_t offset, int64_t size,
		int idx, int end_flag)
{
	struct pitcher_parser *parser = (struct pitcher_parser *)p;
	struct pitcher_frame *frame = pitcher_calloc(1, sizeof(*frame));

	if (!frame) {
		PITCHER_ERR("allco pitcher_frame fail\n");
		return -RET_E_INVAL;
	}

	frame->offset = offset;
	frame->size = size;
	frame->idx = idx;

	pitcher_parser_push(parser, frame);

	return RET_OK;
}

int pitcher_parse_startcode(Parser p, struct pitcher_parser_scode *psc)
{
	struct pitcher_parser *parser;
	struct pitcher_parser_scode sc;
	uint8_t *buf = NULL;
	uint8_t *current = NULL;
	uint32_t state = 0;
	int64_t start = -1;
	int64_t end = -1;
	int64_t offset = 0;
	int type = PARSER_TYPE_FRAME;
	long i;
	int index = 0;
	int frame_count = 0;
	void *priv = NULL;

	if (!p || !psc || !psc->num)
		return -RET_E_INVAL;

	sc = *psc;
	if (sc.extra_num > sc.num) {
		if ((sc.extra_code & sc.mask) != sc.scode) {
			PITCHER_ERR("invalid extra_code : 0x%x for 0x%x\n",
					sc.extra_code, sc.scode);
			return -RET_E_INVAL;
		}
	} else {
		sc.extra_num = sc.num;
		sc.extra_code = sc.scode;
		sc.extra_mask = sc.mask;
		sc.force_extra_on_first = 0;
	}

	if (sc.check_frame && sc.priv_data_size) {
		priv = pitcher_calloc(1, sc.priv_data_size);
		if (!priv) {
			PITCHER_ERR("alloc priv data fail\n");
			return -RET_E_NO_MEMORY;
		}
	}

	parser = (struct pitcher_parser *)p;
	buf = (uint8_t *)parser->virt;
	PITCHER_LOG("total file size: 0x%lx\n", parser->size);

	for (i = 0; i < parser->size; i++) {
		state = (state << 8) | buf[i];

		if (sc.force_extra_on_first && i < sc.extra_num - 1)
			continue;
		else if (i < sc.num - 1)
			continue;

		if (sc.force_extra_on_first) {
			if ((state & sc.extra_mask) != sc.extra_code)
				continue;
		} else {
			if ((state & sc.mask) != sc.scode)
				continue;
		}

		current = buf + i + 1;
		if (sc.check_frame) {
			type = sc.check_frame(current, parser->size - i - 1, priv);
			if (type == PARSER_TYPE_UNKNOWN)
				continue;
		}
		sc.force_extra_on_first = 0;
		if ((i + 1 >= sc.extra_num) && ((state & sc.extra_mask) == sc.extra_code))
			offset = i + 1 - sc.extra_num;
		else
			offset = i + 1 - sc.num;
		if (start < 0)
			start = offset;
		if (frame_count > 0 && end < 0)
			end = offset;
		if (type == PARSER_TYPE_FRAME)
			frame_count++;
		if (frame_count > 1) {
			frame_count--;
			pitcher_parser_push_new_frame(parser,
							start,
							end - start,
							index++,
							0);
			start = end;
			end = -1;
		}
	}

	end = parser->size;
	if (frame_count) {
		frame_count--;
		pitcher_parser_push_new_frame(parser,
						start,
						end - start,
						index++,
						1);
		start = end;
	}

	if (priv) {
		pitcher_free(priv);
		priv = NULL;
	}

	PITCHER_LOG("total frame number : %d\n", index);
	return 0;
}

