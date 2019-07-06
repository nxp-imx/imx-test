/*
 * Copyright 2017 NXP
 *
 * mxc_pdm_cic.h -- Cascade integrator comb filter
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __MXC_PDM_CIC_H
#define __MXC_PDM_CIC_H

/* decimate current pdm sample */
int32_t mxc_pdm_cic(int32_t *cic_int, int32_t *cic_comb, uint64_t data);
/* apply fir filter */
void mxc_cic_fir(int32_t sample, uint16_t bit_per_sample, int32_t *result);

#endif /* __MXC_PDM_CIC_H */
