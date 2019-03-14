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

#define SOURCE_IFACE	KNOT_MODBUS_SERVICE ".Source1"

struct source {
	int refs;
	char *path;
	char *name;
	char *type;
	uint16_t address;
	uint16_t size;
};

static void source_free(struct source *source)
{
	l_free(source->name);
	l_free(source->type);
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

static bool property_get_name(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 's', source->name);

	return true;
}

static struct l_dbus_message *property_set_name(struct l_dbus *dbus,
					 struct l_dbus_message *msg,
					 struct l_dbus_message_iter *new_value,
					 l_dbus_property_complete_cb_t complete,
					 void *user_data)
{
	struct source *source = user_data;
	const char *name;

	if (!l_dbus_message_iter_get_variant(new_value, "s", &name))
		return dbus_error_invalid_args(msg);

	l_free(source->name);
	source->name = l_strdup(name);

	/* TODO: re-connect? */

	complete(dbus, msg, NULL);

	return NULL;
}

static bool property_get_type(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 's', source->type);

	return true;
}

static bool property_get_address(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 'q', &source->address);

	return true;
}

static bool property_get_size(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 'q', &source->size);

	return true;
}

static void setup_interface(struct l_dbus_interface *interface)
{
	/* Variable alias */
	if (!l_dbus_interface_property(interface, "Name", 0, "s",
				       property_get_name,
				       property_set_name))
		l_error("Can't add 'Name' property");

	/* Variable type: coil/register/... */
	if (!l_dbus_interface_property(interface, "Type", 0, "s",
				       property_get_type,
				       NULL))
		l_error("Can't add 'Type' property");

	/* Variable address */
	if (!l_dbus_interface_property(interface, "Address", 0, "q",
				       property_get_address,
				       NULL))
		l_error("Can't add 'Address' property");

	/* Variable size  */
	if (!l_dbus_interface_property(interface, "Size", 0, "q",
				       property_get_size,
				       NULL))
		l_error("Can't add 'Size' property");
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

const char *source_create(const char *prefix, const char *name,
			  const char *type, uint16_t address, uint16_t size)
{
	struct source *source;
	char *dpath;

	/* TODO: Already exists? */

	dpath = l_strdup_printf("%s/source_%04x", prefix, address);

	source = l_new(struct source, 1);
	source->name = l_strdup(name);
	source->type = l_strdup(type);
	source->address = address;
	source->size = size;
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

	return dpath;
}

void source_destroy(const char *opath)
{
	l_dbus_unregister_object(dbus_get_bus(), opath);
}
