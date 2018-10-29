/*
 * Copyright 2018 NXP
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "read_utils.h"

int read_u16_t(int fd, uint16_t *val, int be)
{
	int err;
	unsigned char buff[2];

	err = read(fd, buff, sizeof(buff));
	if (err < 0)
		return err;

	if (be) (*val) = buff[0] << 8 | buff[1];
	else    (*val) = buff[1] << 8 | buff[0];

	return 0;
}

int read_u32_t(int fd, uint32_t *val, int be)
{
	int err;
	uint16_t hval, lval;
	uint32_t xval;

	err = read_u16_t(fd, &lval, be);
	if (err < 0)
		return err;

	err = read_u16_t(fd, &hval, be);
	if (err < 0)
		return err;

	xval = hval;

	if (be) (*val) = (lval << 16) | xval;
	else    (*val) = (xval << 16) | lval;

	return 0;
}

int read_u64_t(int fd, uint64_t *val, int be)
{
	int err;
	uint32_t hval, lval;
	uint64_t xval;

	err = read_u32_t(fd, &lval, be);
	if (err < 0)
		return err;

	err = read_u32_t(fd, &hval, be);
	if (err < 0)
		return err;

	xval = hval;

	if (be) {
		(*val) = lval;
		(*val) = (*val) << 32 | xval;
	} else {
		(*val) = (xval << 32) | lval;
	}

	return 0;
}

size_t read_full(int fd, void *_buffer, size_t size)
{
	uint8_t *buffer = (uint8_t *)_buffer;
	size_t r = 0, to_read = size;

	while (to_read > 0) {
		r = read(fd, buffer, to_read);
		if (r < 0)
			return r;
		if (r == 0) {
			/* Indicates end of file */
			break;
		}
		buffer += r;
		to_read -= r;
	}

	return size - to_read;
}

