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

int read_full(int fd, void *_buffer, size_t size)
{
	uint8_t *buffer = (uint8_t *)_buffer;
	int r = 0;

	while (size > 0) {
		r = read(fd, buffer, size);
		if (r <= 0) {
			fprintf(stderr, "read failed(%s)\n", strerror(errno));
			return r;
		}
		buffer += r;
		size -= r;
	}
	return r;
}

