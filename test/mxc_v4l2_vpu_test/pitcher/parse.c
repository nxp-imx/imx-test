/*
 * Copyright 2020 NXP
 *
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
#include "parse.h"
#include "h264_parse.h"
#include "h265_parse.h"
#include "jpeg_parse.h"
#ifdef VSI_PARSE
#include "vsi_parse.h"
#endif

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

void pitcher_init_parser(Parser src, Parser dst)
{
	struct pitcher_parser *p_dst = NULL;

	if (!src || !dst)
		return;

	p_dst = (struct pitcher_parser *)dst;

	memcpy(p_dst, src, sizeof(*p_dst));

	INIT_LIST_HEAD(&p_dst->queue);
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
		PITCHER_LOG("[%d] size:0x%lx, offset:0x%x\n", frame->idx, frame->size, frame->offset);
		 PITCHER_LOG("0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
						parser->virt[frame->offset],
						parser->virt[frame->offset+1],
						parser->virt[frame->offset+2],
						parser->virt[frame->offset+3],
						parser->virt[frame->offset+4]);
		 size += frame->size;
	}
	PITCHER_LOG("total size: 0x%x\n", size);
}

struct parse_handler parse_handler_table[] = {
#ifdef VSI_PARSE
	{.format = V4L2_PIX_FMT_H264,
	 .handle_parse = vsi_parse,
	},
	{.format = V4L2_PIX_FMT_HEVC,
	 .handle_parse = vsi_parse,
	},
	{.format = V4L2_PIX_FMT_VP8,
	 .handle_parse = vsi_parse,
	},
	{.format = V4L2_PIX_FMT_VP9,
	 .handle_parse = vsi_parse,
	},
	{.format = V4L2_PIX_FMT_MPEG2,
	 .handle_parse = vsi_parse,
	},
	{0, 0},
#else
	{.format = V4L2_PIX_FMT_H264,
	 .handle_parse = h264_parse,
	},
	{.format = V4L2_PIX_FMT_HEVC,
	 .handle_parse = h265_parse,
	},
	{.format = V4L2_PIX_FMT_JPEG,
	 .handle_parse = jpeg_parse,
	},
	{0, 0},
#endif
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

int pitcher_parse_h26x(Parser p, int (*check_nal_is_frame)(int))
{
	struct pitcher_parser *parser;
	char *current = NULL;
	char scode[] = {0x0, 0x0, 0x1};
	int64_t next[] = {0x0, 0x0, 0x1};
	int64_t scode_size = ARRAY_SIZE(scode);
	int64_t start = -1;
	int64_t offset = 0;
	int64_t end = 0;
	long left_bytes;
	int frame_count = 0;
	int index = 0;

	if (!p || !check_nal_is_frame)
		return -RET_E_INVAL;

	parser = (struct pitcher_parser *)p;
	current = parser->virt;
	left_bytes = parser->size;
	get_kmp_next(scode, next, scode_size);

	PITCHER_LOG("total file size: 0x%lx\n", left_bytes);
	while (left_bytes > 0) {
		scode_size = ARRAY_SIZE(scode);
		offset = kmp_search(current,
					left_bytes,
					scode, scode_size, next);
		if (offset < 0)
			break;
		if (offset > 0 && current[offset - 1] == 0x0) {
			offset--;
			scode_size++;
		}
		if (start < 0)
			start = offset;

		current += offset + scode_size;
		left_bytes -= (offset + scode_size);
		if (left_bytes <= 0)
			break;
		if (check_nal_is_frame(current[0])) {
			end = (current - parser->virt - scode_size);
			frame_count++;
		}

		if (frame_count > 1) {
			frame_count--;
			pitcher_parser_push_new_frame(parser,
							start,
							end - start,
							index++,
							0);
			start = end;
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

	return RET_OK;
}

