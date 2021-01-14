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
 * obj.h
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#ifndef _INCLUDE_OBJ_H
#define _INCLUDE_OBJ_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <pthread.h>

struct pitcher_obj {
	unsigned int refcount;
	char name[64];
	pthread_mutex_t mutex;
	void (*release)(struct pitcher_obj *obj);
};

void pitcher_init_obj(struct pitcher_obj *obj,
			void (*release)(struct pitcher_obj *obj));
void pitcher_release_obj(struct pitcher_obj *obj);
int pitcher_set_obj_name(struct pitcher_obj *obj, const char *format, ...);
char *pitcher_get_obj_name(struct pitcher_obj *obj);
void pitcher_put_obj(struct pitcher_obj *obj);
struct pitcher_obj *pitcher_get_obj(struct pitcher_obj *obj);
unsigned int pitcher_get_obj_refcount(struct pitcher_obj *obj);

#ifdef __cplusplus
}
#endif
#endif
