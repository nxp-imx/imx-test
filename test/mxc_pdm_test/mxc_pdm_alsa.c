/*
 * Copyright 2020-2017 NXP
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <alsa/asoundlib.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "mxc_pdm_alsa.h"
#include "mxc_pdm_cic.h"

int capture_exit = 0;
static snd_output_t *snd_log = NULL;
int32_t cic_int[4];
int32_t cic_comb[4];

#ifdef HAS_IMX_SW_PDM
void *mxc_alsa_pdm_simd(void *data)
{
	struct mxc_pdm_priv *priv = (struct mxc_pdm_priv *)data;
	double cpu_time_used = 0;
	clock_t start, end;
	int num_periods;
	char *buffer;

	if (priv->debug_info) {
		fprintf(stderr, "afe.inputbuffer: %u\n",
			priv->afe->inputBufferSizePerChannel);
		fprintf(stderr, "afe.outputbuffer: %u\n",
			priv->afe->outputBufferSizePerChannel);
	}

	while (!capture_exit) {
		/* wait for next frame */
		sem_wait(&priv->sem);
		/* skip first 4 read periods PDM mic startup time */
		if (priv->rperiods > 4) {
			buffer = priv->buffer + priv->write_pos;

			start = clock();
			/* fill AFE input buffer */
			memcpy(priv->afe->inputBuffer, buffer,
				priv->afe->inputBufferSizePerChannel);

			processAfeCic(priv->afe);
			/* write pcm data */
			fwrite(priv->afe->outputBuffer, sizeof(int32_t),
				priv->afe->outputBufferSizePerChannel, priv->fd_out);
			/* update write buffer pointer */
			priv->write_pos += priv->afe->inputBufferSizePerChannel;

			end = clock();
			cpu_time_used = (float)(end - start) / CLOCKS_PER_SEC;
			priv->avg_time_used =
				(cpu_time_used + priv->avg_time_used) / 2;
			priv->time_used += cpu_time_used;

			priv->wperiods++;
			/* reset write buffer position */
			if (priv->write_pos >= priv->buffer_size)
				priv->write_pos = 0;
		}
		/* check number of periods waiting to be written */
		sem_getvalue(&priv->sem, &num_periods);
	}

	return NULL;
}
#endif

void *mxc_alsa_pdm_convert(void *data)
{
	struct mxc_pdm_priv *priv = (struct mxc_pdm_priv *)data;
	double cpu_time_used = 0;
	clock_t start, end;
	int num_periods;
	char *buffer;
	int i, k, n;
	uint64_t tmp;

	while (!capture_exit) {
		/* wait for next frame */
		sem_wait(&priv->sem);
		/* skip first 4 read periods PDM mic startup time */
		if (priv->rperiods > 4) {
			n = 0;
			buffer = priv->buffer + priv->write_pos;
			/* extract samples from read buffer */
			start = clock();
			for (k = 0; k < priv->period_bytes; k += 8) {
				priv->samples[n] = 0;
				for (i = 0; i < 64; i += 8) {
					tmp = buffer[priv->write_pos];
					priv->samples[n] |= tmp << i;
					priv->write_pos++;
					if (priv->debug_info) {
						fprintf(priv->fd_out, "%x:",
							(unsigned int)tmp);
					}
				}
				priv->cframes[n] = mxc_pdm_cic(cic_int,
						cic_comb, priv->samples[n]);
				if (priv->debug_info) {
					fprintf(priv->fd_out,
						"0x%jx\n", priv->samples[n]);
					fprintf(priv->fd_out,
						"0x%x\n", priv->cframes[n]);
				}
				n++;
			}
			end = clock();
			cpu_time_used = (float)(end - start) / CLOCKS_PER_SEC;
			priv->avg_time_used =
				(cpu_time_used + priv->avg_time_used) / 2;
			priv->time_used += cpu_time_used;
			/* write sound data to file */
			if (!priv->debug_info) {
				fwrite(priv->cframes, sizeof(int32_t),
						n, priv->fd_out);
			}

			priv->wperiods++;
			/* reset write buffer position */
			if (priv->write_pos >= priv->buffer_size)
				priv->write_pos = 0;
		}
		/* check number of periods waiting to be written */
		sem_getvalue(&priv->sem, &num_periods);
	}

	return NULL;
}

int mxc_alsa_pdm_xrun(snd_pcm_t *handle)
{
	snd_pcm_status_t *status;
	int ret;

	snd_pcm_status_alloca(&status);
	ret = snd_pcm_status(handle, status);
	if (ret < 0) {
		fprintf(stderr, "alsa status error: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_status_get_state(status);
	/* Overrun or capture stoped */
	if (ret == SND_PCM_STATE_XRUN ||
			ret == SND_PCM_STATE_DRAINING) {
		/* try to recover */
		ret = snd_pcm_prepare(handle);
		if (ret < 0) {
			fprintf(stderr, "alsa suspend prepare error: %s\n",
					snd_strerror(ret));
			return ret;
		}
	}

	return 0;
}

int mxc_alsa_pdm_suspend(snd_pcm_t *handle)
{
	int ret;

	/* wait until suspend flag is clear */
	while ((ret = snd_pcm_resume(handle)) == -EAGAIN)
		sleep(1);
	if (ret < 0) {
		fflush(stderr);
		ret = snd_pcm_prepare(handle);
		if (ret < 0) {
			fprintf(stderr, "alsa suspend prepare error: %s\n",
					snd_strerror(ret));
			return ret;
		}
	}

	return 0;
}

size_t mxc_alsa_pdm_read(struct mxc_pdm_priv *priv)
{
	size_t result = 0, size = 0;
	int ret, wait, num_periods;
	snd_pcm_uframes_t frames;
	char *buffer;

	/* check free periods from reader */
	sem_getvalue(&priv->sem, &num_periods);

	frames = priv->period_frames;
	buffer = priv->buffer + priv->read_pos;

	while (frames > 0) {
		size = snd_pcm_readi(priv->pcm_handle, buffer, frames);
		if (size == -EAGAIN || (size >= 0 && (size_t)size < frames)) {
			wait = snd_pcm_wait(priv->pcm_handle, MXC_APP_WAIT_TIMEOUT);
			if (wait <= 0)
				fprintf(stderr, "read timeout error\n");
		} else if (size == -EPIPE) {
			/* I/O error handler */
			ret = mxc_alsa_pdm_xrun(priv->pcm_handle);
			if (ret < 0)
				exit(1);
		} else if (size == -ESTRPIPE) {
			/* try to resume */
			ret = mxc_alsa_pdm_suspend(priv->pcm_handle);
			if (ret < 0)
				exit(1);
		} else if (size < 0)
			fprintf(stderr, "read error: %s", snd_strerror(size));
		/* update buffer read position */
		if (size > 0) {
			result += size;
			frames -= size;
			buffer += size * priv->bits_per_frame / 8;
		}
	}

	/* track number frame periods */
	priv->rperiods++;
	/* update reader pointer */
	priv->read_pos += result * priv->bits_per_frame / 8;
	/* reset reader pointer if end of buffer */
	if (priv->read_pos >= priv->buffer_size)
		priv->read_pos = 0;
	/* notify new frame available */
	sem_post(&priv->sem);

	return result;
}

int mxc_alsa_pdm_set_params(struct mxc_pdm_priv *priv)
{
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *swparams;
	int ret;

	/* allocate a hardware parameters object. */
	snd_pcm_hw_params_alloca(&params);
	/* allocate software parameters (alsa-lib) */
	snd_pcm_sw_params_alloca(&swparams);
	/* default values. */
	ret = snd_pcm_hw_params_any(priv->pcm_handle, params);
	if (ret < 0) {
		fprintf(stderr, "Set default hw params error\n");
		return -ENODEV;
	}
	/* set access mode */
	ret = snd_pcm_hw_params_set_access(priv->pcm_handle, params,
			priv->access_mode);
	if (ret < 0) {
		fprintf(stderr, "Set access type not supported\n");
		return -EINVAL;
	}
	/* set format */
	ret = snd_pcm_hw_params_set_format(priv->pcm_handle, params,
			priv->format);
	if (ret < 0) {
		fprintf(stderr, "Set pcm format not supported\n");
		return -EINVAL;
	}
	/* Set channels */
	ret = snd_pcm_hw_params_set_channels(priv->pcm_handle, params,
			priv->channels);
	if (ret < 0) {
		fprintf(stderr, "Set number of channels not supported\n");
	}
	/* Sample rate */
	ret = snd_pcm_hw_params_set_rate_near(priv->pcm_handle, params,
			&priv->rate, 0);
	if (ret < 0) {
		fprintf(stderr, "Set sample rate not supported\n");
		return -EINVAL;
	}
	/* configure hardware buffering */
	/* sample period settings */
	priv->bits_per_sample =
			snd_pcm_format_physical_width(priv->format);
	priv->bits_per_frame =
			priv->bits_per_sample * priv->channels;
	/* get max supported buffer size */
	ret = snd_pcm_hw_params_get_buffer_size_max(params,
			&priv->buffer_frames);
	if (ret < 0) {
		fprintf(stderr, "get buffer size max error: %d\n", ret);
		return -EINVAL;
	}

	ret = snd_pcm_hw_params_get_period_size_max(params,
			&priv->period_bytes, 0);
	if (ret < 0) {
		fprintf(stderr, "get period size max error: %d\n", ret);
		return -EINVAL;
	}

	priv->period_frames =
			priv->period_bytes / (priv->bits_per_frame >> 3);

	if ((priv->buffer_frames / priv->period_frames) < MXC_DRV_NUM_PERIODS) {
		fprintf(stderr, "alsa buffer to small for needed frames: %lu\n",
				(priv->buffer_frames / priv->period_frames));
		return -EINVAL;
	}

	/* Set period size */
	ret = snd_pcm_hw_params_set_period_size(priv->pcm_handle,
			params, priv->period_frames, 0);
	if (ret < 0) {
		fprintf(stderr, "Set pcm period size error: %d\n", ret);
		return -EINVAL;
	}

	/* set required buffer size or nearest */
	ret = snd_pcm_hw_params_set_buffer_size_near(priv->pcm_handle, params,
			&priv->buffer_frames);
	if (ret < 0) {
		fprintf(stderr, "Set buffer size error: %d", ret);
		return -EINVAL;
	}

	/* Write the parameters to the driver */
	ret = snd_pcm_hw_params(priv->pcm_handle, params);
	if (ret < 0) {
		fprintf(stderr, "unable to set hw parameters: %s\n",
				snd_strerror(ret));
		return -EINVAL;
	}

	snd_pcm_hw_params_get_period_time(params, &priv->time, NULL);
	snd_pcm_hw_params_get_period_size(params, &priv->period_frames, NULL);
	snd_pcm_hw_params_get_buffer_size(params, &priv->buffer_frames);

	/* set software audio parameters */
	snd_pcm_sw_params_current(priv->pcm_handle, swparams);
	/* minimal frames to consider */
	ret = snd_pcm_sw_params_set_avail_min(priv->pcm_handle, swparams,
			priv->period_frames);
	if (ret < 0) {
		fprintf(stderr, "Set min frames avilable error: %d\n", ret);
		return -EINVAL;
	}
	/* if frames >= buffer set stop threshold */
	ret = snd_pcm_sw_params_set_stop_threshold(priv->pcm_handle,
			swparams, priv->buffer_frames);
	if (ret < 0) {
		fprintf(stderr, "Set stop threshold error: %d\n", ret);
		return -EINVAL;
	}

	/* Write the parameters to alsa-lib */
	ret = snd_pcm_sw_params(priv->pcm_handle, swparams);
	if (ret < 0) {
		fprintf(stderr, "Set swparams fail: %d\n", ret);
		return -EINVAL;
	}

	return 0;
}

int mxc_alsa_pdm_init(struct mxc_pdm_priv *priv)
{
	int i, ret;

	/* Default configuration */
	if (!priv->channels)
		priv->channels = 1;
	if (!priv->rate)
		priv->rate = 16000;
	priv->format = SND_PCM_FORMAT_DSD_U32_LE;
	priv->access_mode = SND_PCM_ACCESS_RW_INTERLEAVED;
	priv->read_pos =  0;
	priv->write_pos = 0;
	priv->rperiods =  0;
	priv->wperiods =  0;
	priv->avg_time_used = 0;
	priv->time_used = 0;
	/*pdm to pcm simd defaults */
	if (!priv->samples_per_channel)
		priv->samples_per_channel = 40;
	if (!priv->gain)
		priv->gain = 0;

	if (priv->type ==
	    CIC_pdmToPcmType_cic_order_5_cic_downsample_unavailable) {
		priv->samples =
			(uint64_t *)malloc(MXC_APP_NUM_FRAMES * sizeof(uint64_t *));
		if (!priv->samples)
			return -ENOMEM;

		priv->cframes =
			(int32_t *)malloc(MXC_APP_NUM_FRAMES * sizeof(int32_t *));
		if (!priv->cframes)
			return -ENOMEM;
	} else {
#ifdef HAS_IMX_SW_PDM
		bool val;
		/* pdm to pcm simd */
		priv->afe = malloc(sizeof(afe_t));
		if (!priv->afe)
			return -ENOMEM;
		/* set base parameters */
		priv->afe->cicDecoderType = priv->type;
		if (priv->gain)
			priv->afe->outputGainFactor = priv->gain;
		priv->afe->numberOfChannels = (unsigned)priv->channels;
		/* init pdm to pcm simd */
		val = constructAfeCicDecoder(priv->type, priv->afe,
			priv->gain, priv->samples_per_channel);
		if (val == false) {
			fprintf(stderr, "fail to create AfeCicDecoder\n");
			return -EINVAL;
		}
#endif
	}

	ret = snd_output_stdio_attach(&snd_log, stderr, 0);
	if (ret < 0) {
		fprintf(stderr, "fail to attach log to stderr output\n");
		return ret;
	}

	/* open pcm device for recording (capture). */
	ret = snd_pcm_open(&priv->pcm_handle, priv->device,
			SND_PCM_STREAM_CAPTURE, 0);
	if (ret < 0) {
		fprintf(stderr, "unable to open pcm device: %s\n",
				priv->device);
		return ret;
	}

	ret = mxc_alsa_pdm_set_params(priv);
	if (ret < 0) {
		fprintf(stderr, "fail setting params error: %d\n", ret);
		return ret;
	}

	/* allocate record buffer */
	priv->buffer_size = (size_t)priv->buffer_frames;
	priv->buffer = (char *)malloc(priv->buffer_size);
	if (!priv->buffer)
		return -ENOMEM;

	if (priv->type ==
	    CIC_pdmToPcmType_cic_order_5_cic_downsample_unavailable) {
		for (i = 0; i < 4; i++) {
			cic_int[i] = 0;
			cic_comb[i] = 0;
		}
	}

	/* dump handle properties */
	snd_pcm_dump(priv->pcm_handle, snd_log);

	return 0;
}

void mxc_alsa_pdm_destroy(struct mxc_pdm_priv *priv)
{
	/* free and close resources */
	free(priv->buffer);
	if (priv->type ==
	    CIC_pdmToPcmType_cic_order_5_cic_downsample_unavailable) {
		free(priv->samples);
		free(priv->cframes);
	} else {
#ifdef HAS_IMX_SW_PDM
		deleteAfeCicDecoder(priv->afe);
#endif
	}
	snd_pcm_nonblock(priv->pcm_handle, 0);
	snd_pcm_drain(priv->pcm_handle);
	snd_pcm_close(priv->pcm_handle);
	snd_output_close(snd_log);
}

void mxc_alsa_pdm_escape(int signal)
{
	fprintf(stdout, "Ctrl-C exit.\n");
	capture_exit = 1;
}

int mxc_alsa_pdm_process(struct mxc_pdm_priv *priv)
{
	float dmips, dps;
	long loops = 1;
	int ret;

	ret = mxc_alsa_pdm_init(priv);
	if (ret < 0) {
		fprintf(stderr, "fail to init alsa pdm %d\n", ret);
		return ret;
	}

	/* ctrl-c to exit test app */
	signal(SIGINT, mxc_alsa_pdm_escape);

	/* init thread/mutex */
	sem_init(&priv->sem, 0, 0);
	pthread_mutex_init(&priv->mutex, NULL);
	/* attach convert thread - built in decimation algo */
	if (priv->type ==
	    CIC_pdmToPcmType_cic_order_5_cic_downsample_unavailable) {
		ret = pthread_create(&priv->thd_id[0], NULL,
			mxc_alsa_pdm_convert, priv);
		if (ret < 0) {
			fprintf(stderr, "fail to create thread %d\n", ret);
			return ret;
		}
	} else {
#ifdef HAS_IMX_SW_PDM
		ret = pthread_create(&priv->thd_id[0], NULL,
			mxc_alsa_pdm_simd, priv);
		if (ret < 0) {
			fprintf(stderr, "fail to create thread %d\n", ret);
			return ret;
		}
#endif
	}

	/* Calculate x seconds */
	if (priv->seconds)
		loops = (priv->seconds * 1000000) / priv->time;

	while (!capture_exit) {
		mxc_alsa_pdm_read(priv);
		if (priv->seconds) {
			loops--;
			if (loops < 0)
				capture_exit = 1;
		}
	}

	dps = (float)(priv->wperiods / priv->time_used);
	dmips = dps / 1757;
	fprintf(stdout, "Avg time proccess one buffer period: %f\n",
			priv->avg_time_used);
	fprintf(stdout, "Dhrystones per second: %6.1f, DMIPS: %6.1f\n",
			dps, dmips);
	fprintf(stdout, "Read:Write periods %d:%d\n", priv->rperiods,
			priv->wperiods);

	pthread_join(priv->thd_id[0], NULL);

	sem_destroy(&priv->sem);
	pthread_mutex_destroy(&priv->mutex);
	mxc_alsa_pdm_destroy(priv);

	return 0;
}
