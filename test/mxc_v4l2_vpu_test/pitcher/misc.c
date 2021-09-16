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
 * misc.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <poll.h>
#include "pitcher_def.h"

static uint64_t __get_time(clockid_t clk_id)
{
	struct timespec tv;
	uint64_t tm;

	clock_gettime(clk_id, &tv);

	tm = (uint64_t)tv.tv_sec * NSEC_PER_SEC + tv.tv_nsec;

	return tm;
}

uint64_t pitcher_get_realtime_time(void)
{
	return __get_time(CLOCK_REALTIME);
}

uint64_t pitcher_get_monotonic_time(void)
{
	return __get_time(CLOCK_MONOTONIC);
}

uint64_t pitcher_get_monotonic_raw_time(void)
{
	return __get_time(CLOCK_MONOTONIC_RAW);
}

int pitcher_poll(int fd, short events, int timeout)
{
	int ret = 0;
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = events;
	ret = poll(&pfd, 1, timeout);
	if (ret <= 0)
		return false;

	if (pfd.revents & events)
		return true;

	return false;
}

long pitcher_get_file_size(const char *filename)
{
	long length;
	FILE *file;
	int ret;

	if (!filename)
		return 0;

	file = fopen(filename, "r");
	if (!file)
		return 0;

	ret = fseek(file, 0l, SEEK_END);
	if (ret < 0) {
		SAFE_RELEASE(file, fclose);
		return 0;
	}

	length = ftell(file);
	if (length < 0)
		length = 0;
	SAFE_RELEASE(file, fclose);

	return length;
}

