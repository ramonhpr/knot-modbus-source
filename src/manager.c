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

#include "dbus.h"
#include "source.h"
#include "manager.h"

#define MANAGER_INTERFACE		"br.org.cesar.modbus.Manager1"

typedef void (*foreach_source_func) (const char *id, const char *ip, int port);

static struct l_settings *settings;

static void create_from_storage(const char *id, const char *ip, int port)
{
	struct source *source;

	source = source_create(id, ip, port);
	if (source == NULL)
		return;
}

static void foreach_source(const struct l_settings *settings,
			   foreach_source_func func, void *user_data)
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

static struct l_dbus_message *method_source_add(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	struct l_dbus_message_iter dict;
	struct l_dbus_message_iter value;
	const char *key = NULL;
	const char *id = NULL;
	const char *ip = NULL;
	int port = -1;

	if (!l_dbus_message_get_arguments(msg, "a{sv}", &dict))
		return dbus_error_invalid_args(msg);

	while (l_dbus_message_iter_next_entry(&dict, &key, &value)) {
		if (strcmp(key, "Id") == 0)
			l_dbus_message_iter_next_entry(&value, &id);
		else if (strcmp(key, "Ip") == 0)
			l_dbus_message_iter_next_entry(&value, &ip);
		else if (strcmp(key, "Port") == 0)
			l_dbus_message_iter_next_entry(&value, &port);
		else
			return dbus_error_invalid_args(msg);
	}

	if (!id || !ip || port < 0)
		return dbus_error_invalid_args(msg);

	/* TODO: Add to storage and create source object */
	if (source_create(id, ip, port) == NULL)
		return dbus_error_invalid_args(msg);

	return l_dbus_message_new_method_return(msg);
}

static struct l_dbus_message *method_source_remove(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	const char *path;

	if (!l_dbus_message_get_arguments(msg, "o", &path))
		return dbus_error_invalid_args(msg);

	/* TODO: remove from storage and destroy source object */

	return l_dbus_message_new_method_return(msg);
}

static void setup_interface(struct l_dbus_interface *interface)
{

	l_dbus_interface_method(interface, "AddSource", 0,
				method_source_add, "", "a{sv}", "dict");

	l_dbus_interface_method(interface, "RemoveSource", 0,
				method_source_remove, "", "o", "path");
}

static void ready_cb(void *user_data)
{
	if (!l_dbus_register_interface(dbus_get_bus(),
				       MANAGER_INTERFACE,
				       setup_interface,
				       NULL, false))
		fprintf(stderr, "dbus: unable to register %s\n",
		       MANAGER_INTERFACE);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 "/",
					 MANAGER_INTERFACE,
					 NULL))
		fprintf(stderr, "dbus: unable to add %s to '/'\n",
		       MANAGER_INTERFACE);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 "/",
					 L_DBUS_INTERFACE_PROPERTIES,
					 NULL))
		fprintf(stderr, "dbus: unable to add %s to '/'\n",
		       L_DBUS_INTERFACE_PROPERTIES);

	source_start();
}

int manager_start(const char *config_file)
{
	settings = l_settings_new();
	if (settings == NULL)
		return -ENOMEM;

	l_settings_set_debug(settings, settings_debug, NULL, NULL);
	if (!l_settings_load_from_file(settings, config_file))
		return -EIO;

	foreach_source(settings, create_from_storage, NULL);

	return dbus_start(ready_cb, NULL);
}

void manager_stop(void)
{
	source_stop();
	dbus_stop();
	l_settings_free(settings);
}
