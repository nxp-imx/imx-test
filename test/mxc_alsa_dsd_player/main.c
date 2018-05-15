/*
 * Copyright (c) 2013 Daniel Mack
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

/*
 * See README
 */
#include "bit_reverse.h"
#include "read_utils.h"
#include <sys/time.h>

#define ALSA_FORMAT	SND_PCM_FORMAT_DSD_U32_LE
#define FRAMECOUNT	(1024 * 128)

static int open_stream(snd_pcm_t **handle, const char *name, int dir,
		       unsigned int rate, unsigned int channels)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	snd_pcm_uframes_t period_size = 512;
/*	snd_pcm_uframes_t buffer_size = 4096 * 4; */
	const char *dirname = (dir == SND_PCM_STREAM_PLAYBACK) ? "PLAYBACK" : "CAPTURE";
	int err;

	if ((err = snd_pcm_open(handle, name, dir, 0)) < 0) {
		fprintf(stderr, "%s (%s): cannot open audio device (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot allocate hardware parameter structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_any(*handle, hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot initialize hardware parameter structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_access(*handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		fprintf(stderr, "%s (%s): cannot set access type(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_format(*handle, hw_params, ALSA_FORMAT)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample format(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_rate_near(*handle, hw_params, &rate, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample rate(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_period_size_near(*handle, hw_params, &period_size, 0)) < 0) {
		fprintf(stderr, "%s (%s): cannot set period time(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
/*
	if ((err = snd_pcm_hw_params_set_buffer_size_near(*handle, hw_params, &buffer_size)) < 0) {
		fprintf(stderr, "%s (%s): cannot set period time(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
*/
	if ((err = snd_pcm_hw_params_set_channels(*handle, hw_params, channels)) < 0) {
		fprintf(stderr, "%s (%s): cannot set channel count(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params(*handle, hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot set parameters(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	snd_pcm_hw_params_free(hw_params);

	if ((err = snd_pcm_sw_params_malloc(&sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot allocate software parameters structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_current(*handle, sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot initialize software parameters structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_set_avail_min(*handle, sw_params, FRAMECOUNT / 2)) < 0) {
		fprintf(stderr, "%s (%s): cannot set minimum available count(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_set_start_threshold(*handle, sw_params, 0U)) < 0) {
		fprintf(stderr, "%s (%s): cannot set start mode(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params(*handle, sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot set software parameters(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	snd_output_t *log;

	snd_output_stdio_attach(&log, stderr, 0);

	snd_pcm_dump(*handle, log);

	snd_output_close(log);
	return 0;
}

/* I/O error handler */
static void xrun(snd_pcm_t *handle)
{
	snd_pcm_status_t *status;
	int res;

	snd_pcm_status_alloca(&status);
	if ((res = snd_pcm_status(handle, status)) < 0) {
		fprintf(stderr, "status error: %s\n", snd_strerror(res));
		exit(EXIT_FAILURE);
	}

	if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
		if ((res = snd_pcm_prepare(handle))<0) {
			fprintf(stderr, "xrun: prepare error: %s", snd_strerror(res));
			exit(EXIT_FAILURE);
		}
		return; /* ok, data should be accepted again */
	}

	fprintf(stderr, "read/write error, state = %s",
		snd_pcm_state_name(snd_pcm_status_get_state(status)));
	exit(EXIT_FAILURE);
}


/* I/O suspend handler */
static void suspend(snd_pcm_t *handle)
{
	int res;

	fprintf(stderr, "Suspended. Trying resume. "); fflush(stderr);

	while ((res = snd_pcm_resume(handle)) == -EAGAIN)
                sleep(1); /* wait until suspend flag is released */

	if (res < 0) {
		fprintf(stderr, "Failed. Restarting stream. "); fflush(stderr);
		res = snd_pcm_prepare(handle);
		if (res < 0) {
			fprintf(stderr, "suspend: prepare error: %s", snd_strerror(res));
			exit(EXIT_FAILURE);
		}
	}
	fprintf(stderr, "Done.\n"); fflush(stderr);
}

static ssize_t pcm_write(snd_pcm_t *handle, uint8_t *data, size_t count, int bytes_per_frame)
{
	ssize_t r;
	ssize_t result = 0;

	while (count > 0) {
		r = snd_pcm_writei(handle, data, count);
		if (r == -EAGAIN || (r >= 0 && (size_t)r < count)) {
			snd_pcm_wait(handle, 100);
		} else if (r == -EPIPE) {
			xrun(handle);
		} else if (r == -ESTRPIPE) {
			suspend(handle);
		} else if (r < 0) {
			fprintf(stderr, "write error: %s\n", snd_strerror(r));
			exit(EXIT_FAILURE);
		}
		if (r > 0) {
			result += r;
			count -= r;
			data += r * bytes_per_frame;
		}
	}
	return result;
}

static struct file_parser {
	char ext[4];
	int (*read_file)(int fd, struct dsd_params *params);
	void (*interleave)(uint8_t *dest, const uint8_t *src, unsigned ch, unsigned fmt);
} parsers[] = {
  { .ext = ".dsf", .read_file = read_dsf_file, .interleave = interleaveDsfBlock },
  { .ext = ".dff", .read_file = read_dff_file, .interleave = interleaveDffBlock },
};

int main(int argc, char *argv[])
{
	int fd, err, block_size, bytes_per_frame, frames;
	unsigned int len;
	snd_pcm_t *playback_handle;
	char *name;
	struct dsd_params params;
	uint64_t readsize=0;
	uint64_t leftsize=0;
	int i, n;
	struct file_parser parser;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <device> <filename>\n", argv[0]);
		return EXIT_FAILURE;
	}

	name = argv[2];
	fd = open(name, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Unable to open file (%m)\n");
		return fd;
	}

	printf("File: [%s]\n", name);
	printf("===============================\n");

	len = strlen(name);
	if (len <= 4) {
		fprintf(stderr, "%s name too short!\n", name);
		return EXIT_FAILURE;
	}

	for (i = 0, n = sizeof(parsers)/sizeof(parsers[0]); i < n; i++) {
		if (strcmp(name + len - 4, parsers[i].ext) != 0)
			continue;
		parser = parsers[i];
		break;
	}

	if (i == n) {
		fprintf(stderr, "%s format not supported !\n", name);
		return EXIT_FAILURE;
	}

	err = parser.read_file(fd, &params);
	if (err < 0)
		return err;

	if ((err = open_stream(&playback_handle, argv[1],
				SND_PCM_STREAM_PLAYBACK,
				params.sampling_freq / snd_pcm_format_width(ALSA_FORMAT),
				params.channel_num) < 0))
		return err;

	if ((err = snd_pcm_prepare(playback_handle)) < 0) {
		fprintf(stderr, "cannot prepare audio interface for use(%s)\n",
			 snd_strerror(err));
		return err;
	}

	block_size = params.channel_num * DSF_BLOCK_SIZE;
	bytes_per_frame = params.channel_num * snd_pcm_format_width(ALSA_FORMAT) / 8;
	frames = block_size / bytes_per_frame;

	leftsize = params.dsd_chunk_size;

	while (leftsize > 0) {
		int r;
		uint8_t buffer[block_size];
		uint8_t interleaved_buffer[block_size];

		if (leftsize >= block_size)
			readsize = block_size;
		else
			readsize = leftsize;

		r = read_full(fd, buffer, readsize);
		if (r <= 0) {
			fprintf(stderr, "read failed(%s)\n", strerror(errno));
			break;
		}

		if (r < block_size) {
			memset(buffer + r, 0x00, block_size - r);
		}

		leftsize = leftsize - readsize;

		if (params.bits_per_sample == 8)
			bit_reverse_buffer(buffer, buffer + block_size);

		parser.interleave(interleaved_buffer, buffer, params.channel_num, ALSA_FORMAT);

		pcm_write(playback_handle, interleaved_buffer,
			frames, bytes_per_frame);

	}

	snd_pcm_close(playback_handle);
	close(fd);

	return 0;
}
