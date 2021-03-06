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

typedef void (*foreach_source_func) (const char *id, const char *address, const char *name);

static struct l_settings *settings;
static struct l_queue *slave_list;

static void settings_debug(const char *str, void *userdata)
{
        l_info("%s\n", str);
}

static bool path_cmp(const void *a, const void *b)
{
	const char *a1 = a;
	const char *b1 = b;

	return (strcmp(a1, b1) == 0 ? true : false);
}

static void create_from_storage(const char *id,
				const char *address, const char *name)
{
	struct slave *slave;
	int slave_id;

	if (sscanf(id, "%x", &slave_id) != 1)
		return;

	slave = slave_create(slave_id, name, address);
	if (!slave)
		return;

	l_queue_push_head(slave_list, slave);
}

static void foreach_slave_register(const struct l_settings *settings,
				   foreach_source_func func, void *user_data)
{
	char **groups;
	char *name;
	char *address;
	int index;

	groups = l_settings_get_groups(settings);
	if (!groups)
		return;

	for (index = 0; groups[index] != NULL; index++) {

		name = l_settings_get_string(settings,
					     groups[index], "Name");
		if (!name)
			continue;

		address = l_settings_get_string(settings,
						groups[index], "Address");
		if (!address) {
			l_free(name);
			continue;
		}

		func(groups[index], address, name);

		l_free(address);
		l_free(name);
	}

	l_strfreev(groups);
}

static struct l_dbus_message *method_slave_add(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	struct slave *slave;
	struct l_dbus_message *reply;
	struct l_dbus_message_builder *builder;
	struct l_dbus_message_iter dict;
	struct l_dbus_message_iter value;
	const char *key = NULL;
	const char *name = NULL;
	const char *address = NULL;
	uint8_t slave_id = 0;

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
			l_dbus_message_iter_get_variant(&value, "y", &slave_id);
		else
			return dbus_error_invalid_args(msg);
	}

	l_info("Creating new slave(%d, %s) ...", slave_id, address);

	if (!address || slave_id == 0)
		return dbus_error_invalid_args(msg);

	/* TODO: Add to storage and create slave object */
	slave = slave_create(slave_id, name ? : address, address);
	if (!slave)
		return dbus_error_invalid_args(msg);

	l_queue_push_head(slave_list, slave);

	/* Add object path to reply message */
	reply = l_dbus_message_new_method_return(msg);
	builder = l_dbus_message_builder_new(reply);
	l_dbus_message_builder_append_basic(builder,
					    'o', slave_get_path(slave));
	l_dbus_message_builder_finalize(builder);
	l_dbus_message_builder_destroy(builder);

	return reply;
}

static struct l_dbus_message *method_slave_remove(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	struct slave *slave;
	const char *opath;

	if (!l_dbus_message_get_arguments(msg, "o", &opath))
		return dbus_error_invalid_args(msg);

	/* Belongs to list? */
	slave = l_queue_remove_if(slave_list, path_cmp, opath);
	if (!slave)
		return dbus_error_invalid_args(msg);

	slave_destroy(slave);

	/* TODO: remove from storage and destroy slave object */

	return l_dbus_message_new_method_return(msg);
}

static void setup_interface(struct l_dbus_interface *interface)
{
	/* Add/Remove slaves (a.k.a variables)  */
	l_dbus_interface_method(interface, "AddSlave", 0,
				method_slave_add, "o",
				"a{sv}", "path", "dict");

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

	/* Registering all slaves */
	foreach_slave_register(settings, create_from_storage, NULL);
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

	slave_list = l_queue_new();

	return dbus_start(ready_cb, (void *) config_file);
}

void manager_stop(void)
{
	l_info("Stopping manager ...");
	l_queue_destroy(slave_list, (l_queue_destroy_func_t) slave_destroy);
	slave_stop();
	dbus_stop();
}
