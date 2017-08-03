/*
 *  * mxc_alsa_hw_params.c  -- Alsa tool to get audio sound card parameters
 *
 * Copyright 2017 NXP
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>

/*===== Global variable =====*/
#define rate_count 12
unsigned int full_rates[rate_count] = {8000, 11025, 16000, 22050, 32000, 44100,
                                       48000, 64000, 88200, 96000, 176400, 192000};

/*===== help =====*/
/**
 @brief  Inform of the available parameters and how to use this tool.
 @param  None.
 @return None.
 **/
void help(void)
{
    printf("Usage: mxc_alsa_hw_params.out <device> <substream> <attribute>\n");
    printf("This tool is used to get sound card hardware parameters");
    printf("<device> : the audio device like hw:0,0\n");
    printf("<substeam> : p is the playback substream and c is the capture substream\n");
    printf("<attribute> : f to get the supported format list; r to get the supported rate list;\n");
    printf("            c to get the supported channel list\n");
    printf("For example: mxc_alsa_hw_params.out hw:0,0 p r\n");
    exit(1);
}

/*===== open_stream =====*/
/**
 @brief  Open the given pcm device for given substream.
 @param  Input:    device - sound card device name in system.
                   substeam - substream of this sound card.
         Output:   handle - opened audio stream handle.
 @return On failure - None zero value.
         On success - zero.
 **/
int open_stream(char *device, char *substream, snd_pcm_t **handle)
{
    int rv = 0;
    snd_pcm_stream_t sub_full_name;
    char *sub_stream;

    switch(*substream)
    {
        case 'p':
            sub_full_name = SND_PCM_STREAM_PLAYBACK;
            sub_stream = "PLAYBACK";
            break;
        case 'c':
            sub_full_name = SND_PCM_STREAM_CAPTURE;
            sub_stream = "CAPTURE";
            break;
        default:
            printf("Unsupported substream arg: %s\n", substream);
            exit(1);
            break;
    }
    rv = snd_pcm_open(handle, device, sub_full_name, 0);
    if (rv < 0)
    {
        printf("Can't open audio card: %s, substeam: %s\n", device, sub_stream);
        exit(1);
    }
    return rv;
}


/*===== get_rates =====*/
/**
 @brief  Get hardware supported rates for given audio substeam.
 @param  Input:    handle - audio substeam handle.
 @return On failure - None zero value.
         On success - zero.
 **/
int get_rates(snd_pcm_t *handle)
{
    int rv = 0;
    int rc = 0;
    int dir = 0;
    int i = 0;
    snd_pcm_hw_params_t *params;

    snd_pcm_hw_params_alloca(&params);
    /* Fill the params with hardware capacity information */
    rv = snd_pcm_hw_params_any(handle,params);
    if (rv < 0)
    {
        printf("Get full range parameters error.\n");
        exit(1);
    }

    rv = 1;
    for (i = 0; i < rate_count; i++)
    {
        rc = snd_pcm_hw_params_test_rate(handle, params, full_rates[i], dir);
        if (rc == 0)
        {
           printf("%d ", full_rates[i]);
           rv = 0;
        }
    }
    if (rv == 1)
    {
        printf("Can't find any supported frequency.\n");
        exit(1);
    }
    return rv;
}

/*===== get_formats =====*/
/**
 @brief  Get hardware supported formats for given audio substeam.
 @param  Input:    handle - audio substeam handle.
 @return On failure - None zero value.
         On success - zero.
 **/
int get_formats(snd_pcm_t *handle)
{
    int rv = 0;
    int fmt = 0;
    snd_pcm_format_mask_t *f_mask;
    snd_pcm_hw_params_t *params;

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_format_mask_alloca(&f_mask);
    /* Fill the params with hardware capacity information */
    rv = snd_pcm_hw_params_any(handle,params);
    if (rv < 0)
    {
        printf("Get full range parameters error.\n");
        exit(1);
    }
    /* get the format mask */
    snd_pcm_hw_params_get_format_mask(params, f_mask);
    rv = 1;
    /* print the formats */
    for (fmt = 0; fmt <= SND_PCM_FORMAT_LAST; fmt++)
    {
        if (snd_pcm_format_mask_test(f_mask,(snd_pcm_format_t)fmt))
        {
            printf("%s ", snd_pcm_format_name((snd_pcm_format_t)fmt));
            rv = 0;
        }
    }
    if (rv != 0)
    {
        printf("No available format.\n");
        exit(1);
    }
    return rv;
}

/*===== get_channels =====*/
/**
 @brief  Get hardware supported channels for given audio substeam.
 @param  Input:    handle - audio substeam handle.
 @return On failure - None zero value.
         On success - zero.
 **/
int get_channels(snd_pcm_t *handle)
{
    int rv = 0;
    int rc = 0;
    int i = 0;
    unsigned int min_chn = 0;
    unsigned int max_chn = 0;
    snd_pcm_hw_params_t *params;

    snd_pcm_hw_params_alloca(&params);
    /* Fill the params with hardware capacity information */
    rv = snd_pcm_hw_params_any(handle,params);
    if (rv < 0)
    {
        printf("Get full range parameters error.\n");
        exit(1);
    }

    snd_pcm_hw_params_get_channels_max(params, &max_chn);
    snd_pcm_hw_params_get_channels_min(params, &min_chn);
    rv = 1;
    for (i = min_chn; i <= max_chn; i++)
    {
        rc = snd_pcm_hw_params_test_channels(handle, params, i);
        if (rc == 0)
        {
           printf("%d ", i);
           rv = 0;
        }
    }
    if (rv == 1)
    {
        printf("Can't find any supported channel.\n");
        exit(1);
    }
    return rv;
}

int main(int argc, char **argv)
{
    snd_pcm_t *handle;
    /* variable to handle the args */
    char *card;
    char *substream;
    char *attribute;

    if (argc != 4)
    {
        help();
    }
    else
    {
        card = argv[1];
        substream = argv[2];
        attribute = argv[3];
    }

    open_stream(card, substream, &handle);

    switch(*attribute)
    {
        case 'r':
            get_rates(handle);
            break;
        case 'f':
            get_formats(handle);
            break;
        case 'c':
            get_channels(handle);
            break;
        default:
            printf("Unsupported attribute: %s\n", attribute);
            exit(1);
            break;
    }
    printf("\n");
    return 0;
}
