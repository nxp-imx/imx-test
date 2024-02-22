/*
 * Copyright 2024 NXP
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
#ifndef _INCLUDE_NETWORK_H
#define _INCLUDE_NETWORK_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "pitcher.h"
#include "../mxc_v4l2_vpu_enc.h"

struct network_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;

	int capture_chnno;
	int output_chnno;
	float drop_rate;
	unsigned int packet_size;
	int end;
};

struct test_node *alloc_network_node(void);

#ifdef __cplusplus
}
#endif
#endif
