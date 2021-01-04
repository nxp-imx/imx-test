/*
 * Copyright 2018-2021 NXP
 *
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * platform.h
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
        INPUT_MODE_FRM_LEVEL = 0,
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
