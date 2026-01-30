/*
 * Copyright 2023 NXP
 *
 * SPDX-License-Identifier: BSD-3
 *
 */

#include <alsa/asoundlib.h>

snd_pcm_format_t alsa_snd_pcm_format_value(const char* name)
{
	return snd_pcm_format_value(name);
}


int alsa_snd_pcm_format_physical_width(snd_pcm_format_t format)
{
	return snd_pcm_format_physical_width(format);
}

int alsa_snd_pcm_format_width(snd_pcm_format_t format)
{
	return snd_pcm_format_width(format);
}

int alsa_snd_pcm_format_linear(snd_pcm_format_t format)
{
	return snd_pcm_format_linear(format);
}

int alsa_snd_pcm_format_signed(snd_pcm_format_t format)
{
	return snd_pcm_format_signed(format);
}
