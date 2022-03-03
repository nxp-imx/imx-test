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
#ifndef _INCLUDE_UNIT_H
#define _INCLUDE_UNIT_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "pitcher.h"

typedef void *Unit;

Unit pitcher_new_unit(struct pitcher_unit_desc *desc, void *arg);
void pitcher_del_unit(Unit u);
int pitcher_set_unit_input(Unit u, Pipe p);
Pipe pitcher_get_unit_source(Unit u);
int pitcher_add_unit_output(Unit u, Pipe p);
int pitcher_rm_unit_output(Unit u, Pipe p);
int pitcher_unit_start(Unit u);
int pitcher_unit_stop(Unit u);
int pitcher_unit_check_ready(Unit u, int *is_end);
int pitcher_unit_run(Unit u);
int pitcher_is_unit_idle_empty(Unit u);
struct pitcher_buffer *pitcher_get_unit_idle_buffer(Unit u);
void pitcher_put_unit_buffer_idle(Unit u, struct pitcher_buffer *buffer);
void pitcher_unit_push_back_output(Unit u, struct pitcher_buffer *buffer);
#ifdef __cplusplus
}
#endif
#endif
