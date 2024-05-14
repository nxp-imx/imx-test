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

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "statistics.h"
#include "pitcher_def.h"
#include "list.h"

static struct timeval start_time;
static int stats_enabled;


void stats_init(void)
{
	if (!stats_enabled) {
		stats_enabled = 1;
		gettimeofday(&start_time, NULL);
	}
}

void stats_init_events(struct stats_events_t *ses)
{
	memset(ses, 0, sizeof(*ses));
}

struct stats_event_t *stats_poll_event(struct stats_events_t *ses)
{
	struct stats_event_t *se;

	if (!stats_enabled)
		return NULL;

	if (ses != NULL && ses->size < STATS_MAX_ENTRIES) {
		se = &ses->v[ses->size++];
		se->index = -1;
		return se;
	}
	return NULL;
}

uint32_t get_timestamp(void)
{
	struct timeval now, delta;

	gettimeofday(&now, NULL);
	timersub(&now, &start_time, &delta);
	return delta.tv_sec * 1000000 + delta.tv_usec;
}

void stats_start_event(struct stats_event_t *se)
{
	if (!stats_enabled)
		return;

	if (se != NULL)
		se->start = get_timestamp();
}

void stats_end_event(struct stats_event_t *se)
{
	if (!stats_enabled)
		return;

	if (se != NULL)
		se->end =  get_timestamp();
}

void stats_index_event(struct stats_event_t *se, uint32_t index)
{
	if (!stats_enabled)
		return;

	if (se != NULL)
		se->index = index;
}

void stats_print_event(char *tag, struct stats_event_t *se)
{
	PITCHER_LOG("[%-22s] %10u %10u %10u\n", tag, se->start, se->end, se->end - se->start);
}

void stats_print_events(char *tag, struct stats_events_t *ses, uint32_t limit)
{
	int i;

	for (i = 0; i < ses->size && i < limit; i++) {
		PITCHER_LOG("[%-11s %10d] %10u %10u %10u\n", tag, ses->v[i].index,
			ses->v[i].start, ses->v[i].end, ses->v[i].end - ses->v[i].start);
	}
}

struct stats_event_t *stats_get_by_index(struct stats_events_t *ses, int32_t index)
{
	int i;

	for (i = 0; i < ses->size; i++)
		if (ses->v[i].index == index)
			return &ses->v[i];

	return NULL;
}

uint32_t stats_min_delta_events(struct stats_events_t *ses)
{
	int i;
	uint32_t min_delta;
	uint32_t curr_delta;

	if (ses == NULL || ses->size == 0)
		return 0;
	min_delta = ses->v[0].end - ses->v[0].start;
	for (i = 1; i < ses->size; i++) {
		curr_delta = ses->v[i].end - ses->v[i].start;
		if (curr_delta < min_delta)
			min_delta = curr_delta;
	}

	return min_delta;
}

uint32_t stats_max_delta_events(struct stats_events_t *ses)
{
	int i;
	uint32_t max_delta;
	uint32_t curr_delta;

	if (ses == NULL || ses->size == 0)
		return 0;
	max_delta = ses->v[0].end - ses->v[0].start;
	for (i = 1; i < ses->size; i++) {
		curr_delta = ses->v[i].end - ses->v[i].start;
		if (curr_delta > max_delta)
			max_delta = curr_delta;
	}

	return max_delta;
}

uint32_t stats_sum_delta_events(struct stats_events_t *ses)
{
	int i;
	uint32_t sum_delta = 0;

	if (ses == NULL || ses->size == 0)
		return 0;
	for (i = 0; i < ses->size; i++)
		sum_delta += ses->v[i].end - ses->v[i].start;

	return sum_delta;
}

uint32_t stats_avg_delta_events(struct stats_events_t *ses)
{
	if (ses == NULL || ses->size == 0)
		return 0;
	return stats_sum_delta_events(ses) / ses->size;
}

void stats_init_sizes(struct stats_sizes_t *ss)
{
	memset(ss, 0, sizeof(*ss));
}

void stats_push_size(struct stats_sizes_t *ss, uint32_t size, int32_t index)
{
	if (ss != NULL && ss->size < STATS_MAX_ENTRIES) {
		ss->v[ss->size++] = (struct stats_size_t) {
			.val = size,
			.index = index
		};
	}
}

void stats_print_sizes(char *tag, struct stats_sizes_t *ss, uint32_t factor, uint32_t limit)
{
	int i;

	for (i = 0; i < ss->size && i < limit; i++)
		PITCHER_LOG("[%-11s %10u] %10.2f\n", tag, ss->v[i].index,
			1.0f * ss->v[i].val / factor);
}

uint32_t stats_sum_sizes(struct stats_sizes_t *ss)
{
	int i;
	uint32_t s = 0;

	if (ss == NULL || ss->size == 0)
		return 0;
	for (i = 0; i < ss->size; i++)
		s += ss->v[i].val;

	return s;
}

uint32_t stats_avg_sizes(struct stats_sizes_t *ss)
{
	if (ss == NULL || ss->size == 0)
		return 0;
	return stats_sum_sizes(ss) / ss->size;
}
