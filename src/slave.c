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
#include "slave.h"

#define SLAVE_IFACE		"br.org.cesar.modbus.Slave1"

typedef void (*foreach_source_func) (const char *id, const char *ip, int port);

struct slave {
	int refs;
	uint8_t id;
	char *name;
	char *path;
};

static struct l_settings *settings;

static void slave_free(struct slave *slave)
{
	l_free(slave->name);
	l_free(slave->path);
	l_free(slave);
}

static struct slave *slave_ref(struct slave *slave)
{
	if (unlikely(!slave))
		return NULL;

	__sync_fetch_and_add(&slave->refs, 1);

	return slave;
}

static void slave_unref(struct slave *slave)
{
	if (unlikely(!slave))
		return;

	if (__sync_sub_and_fetch(&slave->refs, 1))
		return;

	slave_free(slave);
}


static void create_from_storage(const char *id, const char *ip, int port)
{
	struct source *source;

	if (!id || !ip || port < 0) {
		l_warn("storage: invalid entry");
		return;
	}

	l_info("Creating source from storage: %s %s %d", id, ip, port);
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

	l_info("Creating source: %s %s %d", id, ip, port);

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

static bool property_get_id(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct slave *slave = user_data;

	l_dbus_message_builder_append_basic(builder, 'y', &slave->id);

	return true;
}

static bool property_get_name(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct slave *slave = user_data;

	l_dbus_message_builder_append_basic(builder, 's', slave->name);

	return true;
}

static struct l_dbus_message *property_set_name(struct l_dbus *dbus,
					 struct l_dbus_message *msg,
					 struct l_dbus_message_iter *new_value,
					 l_dbus_property_complete_cb_t complete,
					 void *user_data)
{
	struct slave *slave = user_data;
	const char *name;

	if (!l_dbus_message_iter_get_variant(new_value, "s", &name))
		return dbus_error_invalid_args(msg);

	l_free(slave->name);
	slave->name = l_strdup(name);

	complete(dbus, msg, NULL);

	return NULL;
}

static void setup_interface(struct l_dbus_interface *interface)
{

	/* Add/Remove sources (a.k.a variables)  */
	l_dbus_interface_method(interface, "AddSource", 0,
				method_source_add, "", "a{sv}", "dict");

	l_dbus_interface_method(interface, "RemoveSource", 0,
				method_source_remove, "", "o", "path");

	if (!l_dbus_interface_property(interface, "Id", 0, "y",
				       property_get_id,
				       NULL))
		l_error("Can't add 'Id' property");

	/* Local name to identify slaves */
	if (!l_dbus_interface_property(interface, "Name", 0, "s",
				       property_get_name,
				       property_set_name))
		l_error("Can't add 'Name' property");
}

int slave_create(const char *address)
{
	struct slave *slave;
	char *dpath;

	/* "host:port or /dev/ttyACM0, /dev/ttyUSB0, ..."*/

	dpath = l_strdup(address);

	slave = l_new(struct slave, 1);
	slave->id = 0x01;
	slave->name = l_strdup("unknown");

	if (!l_dbus_register_object(dbus_get_bus(),
				    dpath,
				    slave_ref(slave),
				    (l_dbus_destroy_func_t) slave_unref,
				    SLAVE_IFACE, slave,
				    L_DBUS_INTERFACE_PROPERTIES, slave,
				    NULL)) {
		l_error("Can not register: %s", dpath);
		l_free(dpath);
		return -1;
	}

	slave->path = dpath;

	l_info("New slave: %s", dpath);

	/* FIXME: Identifier is a PTR_TO_INT. Missing hashmap */

	return L_PTR_TO_INT(slave);
}

void slave_destroy(int id)
{
	l_dbus_unregister_object(dbus_get_bus(), "/slave01");
}

int slave_start(const char *config_file)
{

	l_info("Starting slave ...");

	settings = l_settings_new();
	if (settings == NULL)
		return -ENOMEM;

	l_settings_set_debug(settings, settings_debug, NULL, NULL);
	if (!l_settings_load_from_file(settings, config_file)) {
		l_settings_free(settings);
		return -EIO;
	}

	if (!l_dbus_register_interface(dbus_get_bus(),
				       SLAVE_IFACE,
				       setup_interface,
				       NULL, false))
		l_error("dbus: unable to register %s", SLAVE_IFACE);

	/* FIXME: missing storage */
	slave_create("/slave01");

	source_start();

	foreach_source(settings, create_from_storage, NULL);

	return 0;
}

void slave_stop(void)
{
	source_stop();
	l_settings_free(settings);
}
