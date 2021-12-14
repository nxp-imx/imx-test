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
 * obj.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include "pitcher_def.h"
#include "obj.h"

void pitcher_init_obj(struct pitcher_obj *obj,
			void (*release)(struct pitcher_obj *obj))
{
	assert(obj);

	pthread_mutex_init(&obj->mutex, NULL);
	obj->refcount = 0;
	obj->release = release;
}

void pitcher_release_obj(struct pitcher_obj *obj)
{
	if (!obj)
		return;

	pthread_mutex_destroy(&obj->mutex);
}

int pitcher_set_obj_name(struct pitcher_obj *obj, const char *format, ...)
{
	va_list ap;
	int num = 0;

	assert(obj);

	va_start(ap, format);
	num += vsnprintf(obj->name, sizeof(obj->name), format, ap);
	va_end(ap);

	return num;
}

char *pitcher_get_obj_name(struct pitcher_obj *obj)
{
	assert(obj);

	return obj->name;
}

static void __release_obj(struct pitcher_obj *obj)
{
	if (obj && obj->release)
		obj->release(obj);
}

void pitcher_put_obj(struct pitcher_obj *obj)
{
	int flag = 0;

	assert(obj);

	pthread_mutex_lock(&obj->mutex);
	if (obj->refcount) {
		obj->refcount--;
		if (obj->refcount == 0)
			flag = 1;
	}
	pthread_mutex_unlock(&obj->mutex);

	if (flag)
		__release_obj(obj);
}

struct pitcher_obj *pitcher_get_obj(struct pitcher_obj *obj)
{
	assert(obj);

	pthread_mutex_lock(&obj->mutex);
	obj->refcount++;
	pthread_mutex_unlock(&obj->mutex);


	return obj;
}

unsigned int pitcher_get_obj_refcount(struct pitcher_obj *obj)
{
	assert(obj);

	return obj->refcount;
}
