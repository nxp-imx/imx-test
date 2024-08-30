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
#ifndef _PITCHER_Y4M_H
#define _PITCHER_Y4M_H
#ifdef __cplusplus
extern "C"
{
#endif

int y4m_read_header(uint8_t *pdata, uint32_t size, struct pix_fmt_info *format, uint32_t *fps);
int y4m_read_frame(uint8_t *pdata, uint32_t size);

int y4m_write_header(FILE *filp, uint32_t width, uint32_t height, uint32_t framerate);
int y4m_write_frame(FILE *filp);

#ifdef __cplusplus
}
#endif
#endif
