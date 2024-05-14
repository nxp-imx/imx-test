/*
 * Copyright 2024 NXP
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

#ifndef __STATISTICS_H__
#define __STATISTICS_H__
#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define STATS_MAX_ENTRIES 2048


struct stats_event_t {
	uint32_t start;
	uint32_t end;
	int32_t index;
};

struct stats_events_t {
	struct stats_event_t v[STATS_MAX_ENTRIES];
	uint32_t size;
};

struct stats_size_t {
	uint32_t val;
	int32_t index;
};

struct stats_sizes_t {
	struct stats_size_t v[STATS_MAX_ENTRIES];
	uint32_t size;
};

void stats_init(void);
void stats_init_events(struct stats_events_t *ses);
struct stats_event_t *stats_poll_event(struct stats_events_t *ses);
void stats_start_event(struct stats_event_t *se);
void stats_end_event(struct stats_event_t *se);
void stats_index_event(struct stats_event_t *se, uint32_t index);
void stats_print_event(char *tag, struct stats_event_t *se);
void stats_print_events(char *tag, struct stats_events_t *ses, uint32_t limit);
struct stats_event_t *stats_get_by_index(struct stats_events_t *ses, int32_t index);
uint32_t stats_min_delta_events(struct stats_events_t *ses);
uint32_t stats_max_delta_events(struct stats_events_t *ses);
uint32_t stats_sum_delta_events(struct stats_events_t *ses);
uint32_t stats_avg_delta_events(struct stats_events_t *ses);
void stats_init_sizes(struct stats_sizes_t *ss);
void stats_push_size(struct stats_sizes_t *ss, uint32_t size, int32_t index);
void stats_print_sizes(char *tag, struct stats_sizes_t *ss, uint32_t factor, uint32_t limit);
uint32_t stats_sum_sizes(struct stats_sizes_t *ss);
uint32_t stats_avg_sizes(struct stats_sizes_t *ss);

#ifdef __cplusplus
}
#endif
#endif
