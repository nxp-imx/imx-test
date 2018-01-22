/*
 * Copyright 2003-2017 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPD_BIT_REVERSE_H
#define MPD_BIT_REVERSE_H

#include <stdint.h>

extern const uint8_t bit_reverse_table[256];

static inline uint8_t bit_reverse(uint8_t x)
{
	return bit_reverse_table[x];
}

static inline void bit_reverse_buffer(uint8_t *p, uint8_t *end)
{
	for (; p < end; ++p) {
		*p = bit_reverse(*p);
	}
}

#endif
