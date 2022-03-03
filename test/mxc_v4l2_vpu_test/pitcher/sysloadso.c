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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dlfcn.h>
#include "pitcher.h"
#include "pitcher_def.h"

void *pitcher_load_object(const char *sofile)
{
	void *handle;
	const char *loaderror;

	if (!sofile)
		return NULL;
	handle = dlopen(sofile, RTLD_NOW|RTLD_LOCAL);
	loaderror = dlerror();
	if (handle == NULL)
		PITCHER_ERR("Failed loading %s: %s\n", sofile, loaderror);

	return handle;
}

void *pitcher_load_function(void *handle, const char *name)
{
	void *symbol = NULL;

	if (!handle || !name)
		goto exit;

	symbol = dlsym(handle, name);
	if (symbol == NULL)
		PITCHER_ERR("Failed loading %s: %s\n", name, dlerror());
exit:
	return symbol;
}

void pitcher_unload_object(void *handle)
{
	SAFE_RELEASE(handle, dlclose);
}
