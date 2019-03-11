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

#include "dbus.h"
#include "source.h"

#define SOURCE_IFACE	KNOT_MODBUS_SERVICE ".Source1"

struct source {
	int refs;
	char *path;
	char *id;
	char *ip;
	int port;
	modbus_t *tcp;
};

static void source_free(struct source *source)
{
	modbus_free(source->tcp);
	l_free(source->id);
	l_free(source->ip);
	l_free(source->path);
	l_free(source);
}

static struct source *source_ref(struct source *source)
{
	if (unlikely(!source))
		return NULL;

	__sync_fetch_and_add(&source->refs, 1);

	return source;
}

static void source_unref(struct source *source)
{
	if (unlikely(!source))
		return;

	if (__sync_sub_and_fetch(&source->refs, 1))
		return;

	source_free(source);
}

static bool property_get_id(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 's', source->id);

	return true;
}

static bool property_get_ip(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 's', source->ip);

	return true;
}

static struct l_dbus_message *property_set_ip(struct l_dbus *dbus,
					 struct l_dbus_message *msg,
					 struct l_dbus_message_iter *new_value,
					 l_dbus_property_complete_cb_t complete,
					 void *user_data)
{
	struct source *source = user_data;
	const char *ip;

	if (!l_dbus_message_iter_get_variant(new_value, "s", &ip))
		return dbus_error_invalid_args(msg);

	l_free(source->ip);
	source->ip = l_strdup(ip);

	/* TODO: re-connect? */

	complete(dbus, msg, NULL);

	return NULL;
}

static bool property_get_port(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 'i', &source->port);

	return true;
}

static struct l_dbus_message *property_set_port(struct l_dbus *dbus,
					 struct l_dbus_message *msg,
					 struct l_dbus_message_iter *new_value,
					 l_dbus_property_complete_cb_t complete,
					 void *user_data)
{
	struct source *source = user_data;
	int port;

	if (!l_dbus_message_iter_get_variant(new_value, "i", &port))
		return dbus_error_invalid_args(msg);

	source->port = port;

	/* TODO: re-connect? */

	complete(dbus, msg, NULL);

	return NULL;
}

static void setup_interface(struct l_dbus_interface *interface)
{
	if (!l_dbus_interface_property(interface, "Id", 0, "s",
				       property_get_id,
				       NULL))
		l_error("Can't add 'Id' property");

	if (!l_dbus_interface_property(interface, "Ip", 0, "s",
				       property_get_ip,
				       property_set_ip))
		l_error("Can't add 'Ip' property");

	if (!l_dbus_interface_property(interface, "Port", 0, "i",
				       property_get_port,
				       property_set_port))
		l_error("Can't add 'Port' property");
}

int source_start(void)
{
	l_info("Starting source ...");

	if (!l_dbus_register_interface(dbus_get_bus(),
				       SOURCE_IFACE,
				       setup_interface,
				       NULL, false)) {
		l_error("dbus: unable to register %s", SOURCE_IFACE);
		return -EINVAL;
	}

	return 0;
}

void source_stop(void)
{
	l_dbus_unregister_interface(dbus_get_bus(),
				    SOURCE_IFACE);
}

struct source *source_create(const char *id, const char *ip, int port)
{
	struct source *source;
	char *dpath;

	/* TODO: Already exists? */

	dpath = l_strdup("/slave01/source01");

	source = l_new(struct source, 1);
	source->id = l_strdup(id);
	source->ip = l_strdup(ip);
	source->port = port;
	source->tcp = modbus_new_tcp(ip, port);
	source->path = NULL;

	/* TODO: Connect to peer */

	if (!l_dbus_register_object(dbus_get_bus(),
				    dpath,
				    source_ref(source),
				    (l_dbus_destroy_func_t) source_unref,
				    SOURCE_IFACE, source,
				    L_DBUS_INTERFACE_PROPERTIES, source,
				    NULL)) {
		l_error("Can not register: %s", dpath);
		l_free(dpath);
		return NULL;
	}

	l_info("New source: %s", dpath);

	source->path = dpath;

	return source_ref(source);
}

void source_destroy(struct source *source)
{
	l_dbus_unregister_object(dbus_get_bus(), source->path);
	source_unref(source);
}
