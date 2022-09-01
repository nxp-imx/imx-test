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
	char *filename;
	struct list_head queue;
	unsigned int format;
	struct pitcher_frame *cur_frame;
	unsigned long number;
	unsigned long frame_cnt;
	char *virt;
	unsigned long size;
	unsigned long offset;
	unsigned int idx;
	uint32_t width;
	uint32_t height;
};

struct pitcher_parser *pitcher_new_parser(void);
void pitcher_init_parser(Parser p);
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
int vp6_parse(Parser p, void *arg);
int divx_parse(Parser p, void *arg);
int rv_parse(Parser p, void *arg);

void vp8_insert_ivf_seqhdr(FILE *file, uint32_t width, uint32_t height,
			   uint32_t frame_rate);
void vp8_insert_ivf_pichdr(FILE *file, unsigned long frame_size);

enum {
	PARSER_TYPE_UNKNOWN,
	PARSER_TYPE_CONFIG,
	PARSER_TYPE_FRAME
};
struct pitcher_parser_scode {
	uint32_t scode;
	uint32_t mask;
	uint32_t num;

	uint32_t extra_num;
	uint32_t extra_code;
	uint32_t extra_mask;
	uint32_t force_extra_on_first;

	int (*check_frame)(uint8_t *, uint32_t, void *priv);
	uint32_t priv_data_size;
};
int pitcher_parse_startcode(Parser p, struct pitcher_parser_scode *psc);

#ifdef __cplusplus
}
#endif
#endif
