/*
 * Copyright 2020 NXP
 *
 * include/platform.h
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */
#ifndef _INCLUDE_PLATFORM_H
#define _INCLUDE_PLATFORM_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>



enum PLATFORM_TYPE {
	IMX_8X = 0,
	IMX_8M,
	OTHERS = 0xffff,
};

enum INPUT_FRAME_MODE {
        INPUT_MODE_INVALID = 0x0,
        INPUT_MODE_FRM_LEVEL,
        INPUT_MODE_NON_FRM_LEVEL,
};

struct platform_t {
        uint32_t type;
        int fd;

        uint32_t frame_mode;
        int (*set_decoder_parameter)(void *arg);
        int (*set_encoder_parameter)(void *arg);
};


#ifdef __cplusplus
}
#endif
#endif
