/*
 * Copyright(c) 2021 NXP. All rights reserved.
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
 * pitcher/loadso.h
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#ifndef _PITCHER_LOADSO_H
#define _PITCHER_LOADSO_H
#ifdef __cplusplus
extern "C"
{
#endif

void *pitcher_load_object(const char *sofile);
void *pitcher_load_function(void *handle, const char *name);
void pitcher_unload_object(void *handle);

#ifdef __cplusplus
}
#endif
#endif
