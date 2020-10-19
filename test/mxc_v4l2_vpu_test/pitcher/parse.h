/*
 * Copyright 2020 NXP
 *
 * include/parse.h
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
#ifdef VSI_PARSE
typedef void *vsi_parser;
#endif
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
#ifdef VSI_PARSE
	vsi_parser *h;
#endif
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

int pitcher_parse_h26x(Parser p, int (*check_nal_is_frame)(int));

#ifdef __cplusplus
}
#endif
#endif
