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
#include <getopt.h>
#include <string.h>

#define error(...) do {\
        fprintf(stderr, "%s: %s:%d: ", command, __FUNCTION__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        putc('\n', stderr); \
} while (0)

#define _(X) X

static char *command;
static unsigned period_time = 0;
static unsigned buffer_time = 0;
static snd_pcm_uframes_t period_frames = 0;
static snd_pcm_uframes_t buffer_frames = 0;
static snd_pcm_uframes_t chunk_size = 0;
static int open_mode = 0;
static int verbose = 0;
static int nonblock = 0;
static int no_period_wakeup = 0;

#define ALSA_FORMAT	SND_PCM_FORMAT_DSD_U32_LE
#define FRAMECOUNT	(1024 * 128)

static int open_stream(snd_pcm_t **handle, const char *name, int dir,
			unsigned int rate, unsigned int channels)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	snd_pcm_uframes_t buffer_size;

	const char *dirname = (dir == SND_PCM_STREAM_PLAYBACK) ? "PLAYBACK" : "CAPTURE";
	int err;

	if ((err = snd_pcm_open(handle, name, dir, open_mode)) < 0) {
		fprintf(stderr, "%s (%s): cannot open audio device (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if (nonblock) {
		err = snd_pcm_nonblock(*handle, 1);
		if(err < 0 ) {
			error(_("nonblock setting error: %s"), snd_strerror(err));
			return err;
		}
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

	if ((err = snd_pcm_hw_params_set_channels(*handle, hw_params, channels)) < 0) {
		fprintf(stderr, "%s (%s): cannot set channel count(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(hw_params, &buffer_time, 0);
		if (err < 0 )
			return err;
		if (buffer_time > 500000)
			buffer_time = 500000;
	}

	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0 )
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}

	if (period_time > 0)
		err = snd_pcm_hw_params_set_period_time_near(*handle, hw_params,
							&period_time, 0);
	else
		err = snd_pcm_hw_params_set_period_size_near(*handle, hw_params,
							&period_frames, 0);
	if (err < 0) {
		fprintf(stderr, "%s (%s): cannot set period time(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if (buffer_time > 0 )
		err = snd_pcm_hw_params_set_buffer_time_near(*handle, hw_params,
							&buffer_time, 0);
	else
		err = snd_pcm_hw_params_set_buffer_size_near(*handle, hw_params,
							&buffer_frames);
	if (err  < 0) {
		fprintf(stderr, "%s (%s): cannot set period time(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if (no_period_wakeup)
		snd_pcm_hw_params_set_period_wakeup(*handle, hw_params, 0);

	if ((err = snd_pcm_hw_params(*handle, hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot set parameters(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	snd_pcm_hw_params_get_period_size(hw_params, &chunk_size, 0);
	snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
	if (chunk_size == buffer_size) {
		error(_("can't use period equal to buffer size"));
		return -1;
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
	if ((err = snd_pcm_sw_params_set_avail_min(*handle, sw_params, chunk_size)) < 0) {
		fprintf(stderr, "%s (%s): cannot set minimum available count(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_set_start_threshold(*handle, sw_params, buffer_size)) < 0) {
		fprintf(stderr, "%s (%s): cannot set start threshold(%s)\n",
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
	void (*interleave)(uint8_t *dest, const uint8_t *src, unsigned ch, snd_pcm_format_t fmt);
} parsers[] = {
  { .ext = ".dsf", .read_file = read_dsf_file, .interleave = interleaveDsfBlock },
  { .ext = ".dff", .read_file = read_dff_file, .interleave = interleaveDffBlock },
};

enum {
	OPT_VERSION = 1,
	OPT_PERIOD_SIZE,
	OPT_BUFFER_SIZE,
	OPT_NO_PERIOD_WAKEUP,
};

static void usage(char *command)
{
	printf(
_("Usage: %s [OPTION]... [FILE]...\n"
"\n"
"-h, --help              help\n"
"    --version           print current version\n"
"-D, --device=NAME       select PCM by name\n"
"-F, --period-time=#     distance between interrupts is # microseconds\n"
"-B, --buffer-time=#     buffer duration is # microseconds\n"
"    --period-size=#     distance between interrupts is # frames\n"
"    --buffer-size=#     buffer duration is # frames\n"
"    --no-period-wakeup  set no period wakeup flag\n"
)
		, command);
}

static void version(void)
{
	printf("version 0.0.1\n");
}

static long parse_long(const char *str, int *err)
{
	long val;
	char *endptr;

	errno = 0;
	val = strtol(str, &endptr, 0);

	if (errno != 0 || *endptr != '\0')
		*err = -1;
	else
		*err = 0;

	return val;
}

int main(int argc, char *argv[])
{
	int fd, err, block_size, bytes_per_frame, frames;
	unsigned int len;
	snd_pcm_t *playback_handle;
	char *name;
	struct dsd_params params;
	uint64_t readsize=0;
	uint64_t leftsize=0;
	uint64_t writesize = 0;
	int i, n;
	struct file_parser parser;
	char *pcm_name = "default";
	int c, option_index;

	static const char short_options[] = "hD:NF:B:v";
	static const struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"version", 0, 0, OPT_VERSION},
		{"device", 1, 0, 'D'},
		{"nonblock", 0, 0, 'N'},
		{"period-time", 1, 0, 'F'},
		{"period-size", 1, 0, OPT_PERIOD_SIZE},
		{"buffer-time", 1, 0, 'B'},
		{"buffer-size", 1, 0, OPT_BUFFER_SIZE},
		{"verbose", 0, 0, 'v'},
		{"no-period-wakeup", 0, 0, OPT_NO_PERIOD_WAKEUP},
		{0, 0, 0, 0}
	};

	command = argv[0];

	while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage(command);
			return 0;
		case OPT_VERSION:
			version();
			return 0;
		case 'D':
			pcm_name = optarg;
			break;
		case 'N':
			nonblock = 1;
			open_mode |= SND_PCM_NONBLOCK;
			break;
		case 'F':
			period_time = parse_long(optarg, &err);
			if (err < 0) {
				error(_("invalid period time argument '%s'"), optarg);
				return 1;
			}
			break;
		case 'B':
			buffer_time = parse_long(optarg, &err);
			if (err < 0) {
				error(_("invalid buffer time argument '%s'"), optarg);
				return 1;
			}
			break;
		case OPT_PERIOD_SIZE:
			period_frames = parse_long(optarg, &err);
			if (err < 0) {
				error(_("invalid period size argument '%s'"), optarg);
				return 1;
			}
			break;
		case OPT_BUFFER_SIZE:
			buffer_frames = parse_long(optarg, &err);
			if (err < 0) {
				error(_("invalid buffer size argument '%s'"), optarg);
				return 1;
			}
			break;
		case OPT_NO_PERIOD_WAKEUP:
			no_period_wakeup = 1;
			break;
		case 'v':
			verbose++;
			break;
		default:
			fprintf(stderr, _("Try `%s --help' for more information.\n"), command);
			return 1;
		}
	}

	if (optind <= argc - 1)
		name = argv[optind++];
	else {
		usage(command);
		return EXIT_FAILURE;
	}

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
		if (strncmp(name + len - 4, parsers[i].ext, 4) != 0)
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

	if ((err = open_stream(&playback_handle, pcm_name,
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
		size_t r;
		uint8_t buffer[block_size];
		uint8_t interleaved_buffer[block_size];

		if (leftsize >= block_size)
			readsize = block_size;
		else
			readsize = leftsize;

		r = read_full(fd, buffer, readsize);
		if (r < 0) {
			fprintf(stderr, "reading %lu bytes failed (%d-%s)\n",
					readsize, errno, strerror(errno));
			break;
		}

		/* r == 0 indicates end of file */
		if (r == 0)
			break;

		if (r < block_size) {
			memset(buffer + r, 0x00, block_size - r);
		}

		leftsize = leftsize - r;

		if (params.bits_per_sample == 8)
			bit_reverse_buffer(buffer, buffer + block_size);

		parser.interleave(interleaved_buffer, buffer, params.channel_num, ALSA_FORMAT);

		pcm_write(playback_handle, interleaved_buffer,
			frames, bytes_per_frame);

		writesize += frames;
	}

	if (writesize % chunk_size) {
		uint8_t *zero_buf = malloc((chunk_size - (writesize % chunk_size)) * bytes_per_frame);

		memset(zero_buf, 0, (chunk_size - (writesize % chunk_size)) * bytes_per_frame);
		pcm_write(playback_handle, zero_buf,
				(chunk_size - (writesize % chunk_size)),
				bytes_per_frame);
		free(zero_buf);
	}

	snd_pcm_nonblock(playback_handle, 0);
	snd_pcm_drain(playback_handle);
	snd_pcm_nonblock(playback_handle, nonblock);

	snd_pcm_close(playback_handle);
	close(fd);

	return 0;
}
