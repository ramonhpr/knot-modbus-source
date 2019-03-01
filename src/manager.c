/*
 * This file is part of the KNOT Project
 *
 * Copyright (c) 2019, CESAR. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <ell/ell.h>

#include "manager.h"

typedef void (*foreach_device_func) (const char *id, const char *ip, int port);

static struct l_settings *settings;

static void print_keys(const char *id, const char *ip, int port)
{
	/* TODO: Connect to peer */
}

static void foreach_device(const struct l_settings *settings,
			   foreach_device_func func, void *user_data)
{
	char **groups;
	char *ip;
	int index;
	int port;

	groups = l_settings_get_groups(settings);
	if (!groups)
		return;

	for (index = 0; groups[index] != NULL; index++) {

		if (!l_settings_get_int(settings, groups[index], "Port", &port))
			continue;

		ip = l_settings_get_string(settings, groups[index], "IP");

		func(groups[index], ip, port);

		l_free(ip);
	}

	l_strfreev(groups);
}

static void settings_debug(const char *str, void *userdata)
{
        l_info("%s\n", str);
}

int manager_start(const char *config_file)
{
	settings = l_settings_new();
	if (settings == NULL)
		return -ENOMEM;

	l_settings_set_debug(settings, settings_debug, NULL, NULL);
	if (!l_settings_load_from_file(settings, config_file))
		return -EIO;

	foreach_device(settings, print_keys, NULL);

	return 0;
}

void manager_stop(void)
{
	l_settings_free(settings);
}
