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

#include <modbus.h>
#include <string.h>

#include "dbus.h"
#include "source.h"
#include "slave.h"

struct slave {
	int refs;
	uint8_t id;
	bool enable;
	char *name;
	char *path;
	char *hostname;
	int port;
	modbus_t *tcp;
	struct l_queue *source_list;
};

static struct l_settings *settings;

static bool path_cmp(const void *a, const void *b)
{
	const struct source *source = a;
	const char *b1 = b;

	return (strcmp(source_get_path(source), b1) == 0 ? true : false);
}

static void slave_free(struct slave *slave)
{
	l_queue_destroy(slave->source_list,
			(l_queue_destroy_func_t) source_destroy);

	modbus_close(slave->tcp);
	modbus_free(slave->tcp);
	l_free(slave->hostname);
	l_free(slave->name);
	l_free(slave->path);
	l_info("slave_free(%p)", slave);
	l_free(slave);
}

static struct slave *slave_ref(struct slave *slave)
{
	if (unlikely(!slave))
		return NULL;

	__sync_fetch_and_add(&slave->refs, 1);
	l_info("slave_ref(%p): %d", slave, slave->refs);

	return slave;
}

static void slave_unref(struct slave *slave)
{
	if (unlikely(!slave))
		return;

	l_info("slave_unref(%p): %d", slave, slave->refs - 1);
	if (__sync_sub_and_fetch(&slave->refs, 1))
		return;

	slave_free(slave);
}

static void settings_debug(const char *str, void *userdata)
{
        l_info("%s\n", str);
}

static struct l_dbus_message *method_source_add(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	struct slave *slave = user_data;
	struct source *source;
	struct l_dbus_message *reply;
	struct l_dbus_message_builder *builder;
	struct l_dbus_message_iter dict;
	struct l_dbus_message_iter value;
	const char *key = NULL;
	const char *name = NULL;
	const char *type = NULL;
	uint16_t address = 0;
	uint16_t size = 0;
	uint16_t interval = 1000; /* ms */
	bool ret;

	if (!l_dbus_message_get_arguments(msg, "a{sv}", &dict))
		return dbus_error_invalid_args(msg);

	while (l_dbus_message_iter_next_entry(&dict, &key, &value)) {
		if (strcmp(key, "Name") == 0)
			ret = l_dbus_message_iter_get_variant(&value,
							      "s", &name);
		else if (strcmp(key, "Type") == 0)
			ret = l_dbus_message_iter_get_variant(&value,
							      "s", &type);
		else if (strcmp(key, "Address") == 0)
			ret = l_dbus_message_iter_get_variant(&value,
							      "q", &address);
		else if (strcmp(key, "Size") == 0)
			ret = l_dbus_message_iter_get_variant(&value,
							      "q", &size);
		else if (strcmp(key, "PollingInterval") == 0)
			ret = l_dbus_message_iter_get_variant(&value,
							      "q", &interval);
		else
			return dbus_error_invalid_args(msg);

		if (!ret)
			return dbus_error_invalid_args(msg);
	}

	/* FIXME: validate type */
	if (!name || !type || address == 0 || size == 0)
		return dbus_error_invalid_args(msg);

	/* TODO: Add to storage and create source object */
	source = source_create(slave->path, name, type,
			       address, size, interval);
	if (!source)
		return dbus_error_invalid_args(msg);

	/* Add object path to reply message */
	reply = l_dbus_message_new_method_return(msg);
	builder = l_dbus_message_builder_new(reply);
	l_dbus_message_builder_append_basic(builder, 'o',
					    source_get_path(source));
	l_dbus_message_builder_finalize(builder);
	l_dbus_message_builder_destroy(builder);

	l_queue_push_head(slave->source_list, source);

	return reply;
}

static struct l_dbus_message *method_source_remove(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	struct slave *slave = user_data;
	struct source *source;
	const char *opath;

	if (!l_dbus_message_get_arguments(msg, "o", &opath))
		return dbus_error_invalid_args(msg);

	/* TODO: remove from storage and destroy source object */

	source = l_queue_remove_if(slave->source_list, path_cmp, opath);
	if (unlikely(!source))
		return dbus_error_invalid_args(msg);

	source_destroy(source);

	return l_dbus_message_new_method_return(msg);
}

static bool property_get_id(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct slave *slave = user_data;

	/* modbus specific id */
	l_dbus_message_builder_append_basic(builder, 'y', &slave->id);

	return true;
}

static bool property_get_name(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct slave *slave = user_data;

	/* Local name */
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

	/* Local name */
	if (!l_dbus_message_iter_get_variant(new_value, "s", &name))
		return dbus_error_invalid_args(msg);

	l_free(slave->name);
	slave->name = l_strdup(name);

	complete(dbus, msg, NULL);

	return NULL;
}

static bool property_get_enable(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct slave *slave = user_data;
	bool enable;

	enable = (slave->tcp ? true : false);

	l_dbus_message_builder_append_basic(builder, 'b', &enable);

	return true;
}

static struct l_dbus_message *property_set_enable(struct l_dbus *dbus,
					 struct l_dbus_message *msg,
					 struct l_dbus_message_iter *new_value,
					 l_dbus_property_complete_cb_t complete,
					 void *user_data)
{
	struct slave *slave = user_data;
	bool enable;
	int err;

	if (!l_dbus_message_iter_get_variant(new_value, "b", &enable))
		return dbus_error_invalid_args(msg);


	/* Shutdown modbus tcp */
	if (enable == false) {

		/* Already closed? */
		if (slave->tcp == NULL)
			goto done;

		/* Releasing connection */
		modbus_close(slave->tcp);
		modbus_free(slave->tcp);
		slave->tcp = NULL;
	} else {
		/* Enabling modbus tcp */

		/* Already connected ? */
		if (slave->tcp)
			goto done;

		slave->tcp = modbus_new_tcp(slave->hostname, slave->port);
		if (modbus_connect(slave->tcp)  == 0)
			goto done;

		/* Releasing connection */
		err = errno;
		modbus_close(slave->tcp);
		modbus_free(slave->tcp);
		slave->tcp = NULL;

		return dbus_error_errno(msg, "Connect", err);
	}
done:
	complete(dbus, msg, NULL);
	return NULL;
}

static void setup_interface(struct l_dbus_interface *interface)
{

	/* Add/Remove sources (a.k.a variables)  */
	l_dbus_interface_method(interface, "AddSource", 0,
				method_source_add,
				"o", "a{sv}", "path", "dict");

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

	/* Enable/Disable slave polling */
	if (!l_dbus_interface_property(interface, "Enable", 0, "b",
				       property_get_enable,
				       property_set_enable))
		l_error("Can't add 'Enable' property");

}

struct slave *slave_create(uint8_t id, const char *name, const char *address)
{
	struct slave *slave;
	char *dpath;
	char hostname[128];
	int port = -1;

	/* "host:port or /dev/ttyACM0, /dev/ttyUSB0, ..."*/

	memset(hostname, 0, sizeof(hostname));
	if (sscanf(address, "%[^:]:%d", hostname, &port) != 2) {
		l_error("Address (%s) not supported: Invalid format", address);
		return NULL;
	}

	/* FIXME: id is not unique across PLCs */

	dpath = l_strdup_printf("/slave_%04x", id);

	slave = l_new(struct slave, 1);
	slave->refs = 0;
	slave->id = id;
	slave->enable = false;
	slave->name = l_strdup(name);
	slave->hostname = l_strdup(hostname);
	slave->port = port;
	slave->tcp = NULL;
	slave->source_list = l_queue_new();

	if (!l_dbus_register_object(dbus_get_bus(),
				    dpath,
				    slave_ref(slave),
				    (l_dbus_destroy_func_t) slave_unref,
				    SLAVE_IFACE, slave,
				    L_DBUS_INTERFACE_PROPERTIES,
				    slave,
				    NULL)) {
		l_error("Can not register: %s", dpath);
		l_free(dpath);
		return NULL;
	}

	slave->path = dpath;

	l_info("Slave(%p): (%s) hostname: (%s) port: (%d)",
					slave, dpath, hostname, port);

	/* FIXME: Identifier is a PTR_TO_INT. Missing hashmap */

	return slave_ref(slave);
}

void slave_destroy(struct slave *slave)
{
	l_info("slave_destroy(%p)", slave);

	if (unlikely(!slave))
		return;

	l_dbus_unregister_object(dbus_get_bus(), slave->path);
	slave_unref(slave);
}

const char *slave_get_path(const struct slave *slave)
{
	if (unlikely(!slave))
		return NULL;

	return slave->path;
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

	source_start();

	return 0;
}

void slave_stop(void)
{
	source_stop();
	l_dbus_unregister_interface(dbus_get_bus(),
				    SLAVE_IFACE);
	l_settings_free(settings);
}
