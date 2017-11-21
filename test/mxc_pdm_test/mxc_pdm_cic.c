/*
 * Copyright 2017 NXP.
 *
 * mxc_cic.c -- Cascade integrator comb filter
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>

void mxc_pdm_fir(const int16_t *s, const int16_t *c, int32_t *y, const uint32_t
		num_samples)
{
	uint32_t i, j;
	int32_t result;

	for (i = 0; i < num_samples; i++) {
		result = 0;
		for (j = 0; j < 16; j++) {
			result += c[j] * s[i+j];
		}
		y[i] = result;
	}
}

int32_t mxc_pdm_cic(int32_t *cic_int, int32_t *cic_comb, uint64_t data)
{
	int32_t i, tmp0, tmp1;

	for (i = 0; i < 64; i++) {
		cic_int[0] = (data & 0x1) ? cic_int[0] + 1 : cic_int[0] - 1;
		cic_int[1] += cic_int[0];
		cic_int[2] += cic_int[1];
		cic_int[3] += cic_int[2];
		data = data >> 1;
	}

	tmp1 = cic_comb[0];
	cic_comb[0] = cic_int[3];
	tmp0 = cic_int[3] - tmp1;

	tmp1 = cic_comb[1];
	cic_comb[1] = tmp0;
	tmp0 = tmp0 - tmp1;

	tmp1 = cic_comb[2];
	cic_comb[2] = tmp0;
	tmp0 = tmp0 - tmp1;

	tmp1 = cic_comb[3];
	cic_comb[3] = tmp0;
	tmp0 = tmp0 - tmp1;

	return tmp0;
}
