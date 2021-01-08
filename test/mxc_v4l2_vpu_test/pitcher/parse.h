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
 * parse.h
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */
#ifndef _INCLUDE_PARSE_H
#define _INCLUDE_PARSE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "queue.h"
#include "list.h"

typedef void *Parser;

struct pitcher_frame {
	struct list_head list;
	unsigned int idx;
	unsigned long size;
	unsigned int offset;
	unsigned int flag;
};

struct pitcher_parser {
	struct list_head queue;
	unsigned int format;
	struct pitcher_frame *cur_frame;
	unsigned long number;
	char *virt;
	unsigned long size;
};

struct pitcher_parser *pitcher_new_parser(void);
void pitcher_init_parser(Parser src, Parser dst);
void pitcher_del_parser(Parser p);
struct pitcher_frame *pitcher_parser_cur_frame(Parser p);
void pitcher_parser_push(Parser p, struct pitcher_frame *frame);
void pitcher_parser_to_next_frame(Parser p);
void pitcher_parser_to_prev_frame(Parser p);
struct pitcher_frame *pitcher_parser_first_frame(Parser p);
struct pitcher_frame *pitcher_parser_last_frame(Parser p);
void pitcher_parser_seek_to_begin(Parser p);
int pitcher_parse(Parser p);
int is_support_parser(unsigned int fmt);
void pitcher_parser_show(Parser p);

void get_kmp_next(const char *p, int64_t *next, int64_t size);
int64_t kmp_search(char *s, int64_t s_len, const char *p, int64_t p_len, int64_t *next);
int pitcher_parser_push_new_frame(Parser p, int64_t offset, int64_t size,
		int idx, int end_flag);

int pitcher_parse_h26x(Parser p, int (*check_nal_is_frame)(char *));
int h264_parse(Parser p, void *arg);
int h265_parse(Parser p, void *arg);
int h263_parse(Parser p, void *arg);
int jpeg_parse(Parser p, void *arg);
int spk_parse(Parser p, void *arg);
int mpeg4_parse(Parser p, void *arg);
int mpeg2_parse(Parser p, void *arg);
int xvid_parse(Parser p, void *arg);
int avs_parse(Parser p, void *arg);
int vp8_parse(Parser p, void *arg);
int vp9_parse(Parser p, void *arg);
int vc1l_parse(Parser p, void *arg);
int vc1g_parse(Parser p, void *arg);

#ifdef __cplusplus
}
#endif
#endif
