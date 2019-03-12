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
#include "slave.h"
#include "manager.h"

#define MANAGER_INTERFACE		"br.org.cesar.modbus.Manager1"

static struct l_settings *settings;

static void settings_debug(const char *str, void *userdata)
{
        l_info("%s\n", str);
}

static struct l_dbus_message *method_slave_add(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	struct l_dbus_message_iter dict;
	struct l_dbus_message_iter value;
	const char *key = NULL;
	const char *name = NULL;
	const char *address = NULL;
	uint8_t id = 0;

	if (!l_dbus_message_get_arguments(msg, "a{sv}", &dict))
		return dbus_error_invalid_args(msg);

	/*
	 * "Id": modbus slave id (1 - 247)
	 * "Name": Friendly/local name
	 * "Address: host:port or /dev/ttyACM0, /dev/ttyUSB0, ...
	 */
	while (l_dbus_message_iter_next_entry(&dict, &key, &value)) {
		if (strcmp(key, "Name") == 0)
			l_dbus_message_iter_get_variant(&value, "s", &name);
		else if (strcmp(key, "Address") == 0)
			l_dbus_message_iter_get_variant(&value, "s", &address);
		else if (strcmp(key, "Id") == 0)
			l_dbus_message_iter_get_variant(&value, "y", &id);
		else
			return dbus_error_invalid_args(msg);
	}

	l_info("Creating new slave(%d, %s) ...", id, address);

	if (!address || id == 0)
		return dbus_error_invalid_args(msg);

	/* TODO: Add to storage and create slave object */
	if (slave_create(id, name ? : address, address) < 0)
		return dbus_error_invalid_args(msg);

	return l_dbus_message_new_method_return(msg);
}

static struct l_dbus_message *method_slave_remove(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	const char *path;

	if (!l_dbus_message_get_arguments(msg, "o", &path))
		return dbus_error_invalid_args(msg);

	/* TODO: remove from storage and destroy slave object */

	return l_dbus_message_new_method_return(msg);
}

static void setup_interface(struct l_dbus_interface *interface)
{

	/* Add/Remove slaves (a.k.a variables)  */
	l_dbus_interface_method(interface, "AddSlave", 0,
				method_slave_add, "", "a{sv}", "dict");

	l_dbus_interface_method(interface, "RemoveSlave", 0,
				method_slave_remove, "", "o", "path");
}

static void ready_cb(void *user_data)
{
	if (!l_dbus_register_interface(dbus_get_bus(),
				       MANAGER_INTERFACE,
				       setup_interface,
				       NULL, false))
		l_error("dbus: unable to register %s", MANAGER_INTERFACE);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 "/",
					 MANAGER_INTERFACE,
					 NULL))
		l_error("dbus: unable to add %s to '/'", MANAGER_INTERFACE);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 "/",
					 L_DBUS_INTERFACE_PROPERTIES,
					 NULL))
		l_error("dbus: unable to add %s to '/'",
			L_DBUS_INTERFACE_PROPERTIES);

	slave_start(user_data);
}

int manager_start(const char *config_file)
{
	l_info("Starting manager ...");

	/* Slave settings file */
	settings = l_settings_new();
	if (settings == NULL)
		return -ENOMEM;

	l_settings_set_debug(settings, settings_debug, NULL, NULL);
	if (!l_settings_load_from_file(settings, config_file))
		return -EIO;

	return dbus_start(ready_cb, (void *) config_file);
}

void manager_stop(void)
{
	slave_stop();
	dbus_stop();
}
