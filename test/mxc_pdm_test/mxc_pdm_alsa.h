/*
 * Copyright 2017 NXP
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __MXC_PDM_ALSA_H
#define __MXC_PDM_ALSA_H

/* macros */
#define ALSA_PCM_NEW_HW_PARAMS_API
#define MXC_APP_PERIOD_SIZE 4096
#define MXC_APP_NUM_FRAMES  1024
#define MXC_APP_NUM_PERIODS 256
#define MXC_APP_WAIT_TIMEOUT 1000 /* ms max */
#define MXC_DRV_NUM_PERIODS 4

#include <alsa/asoundlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdint.h>
/* structs */
struct mxc_pdm_priv {
	snd_pcm_t *pcm_handle;
	snd_pcm_format_t format;
	snd_pcm_uframes_t period_frames;
	snd_pcm_uframes_t buffer_frames;
	unsigned int channels;
	unsigned int rate;
	unsigned int period_bytes;
	unsigned int access_mode;
	unsigned int time;
	unsigned int seconds;
	int bits_per_sample;
	int bits_per_frame;
	int write_pos;
	int read_pos;
	int rperiods;
	int wperiods;
	int frames;
	int debug_info;
	float avg_time_used;
	float time_used;
	/* sound buffer */
	size_t buffer_size;
	char *buffer;
	char *device;
	uint64_t *samples;
	int32_t *cframes;
	/* file descriptors */
	FILE *fd_out;
	/* thread */
	pthread_t thd_id[2];
	pthread_mutex_t mutex;
	pthread_rwlock_t lock;
	sem_t sem;
};

/* functions */
int mxc_alsa_pdm_process(struct mxc_pdm_priv *priv);

#endif /* __MXC_PDM_ALSA_H */
