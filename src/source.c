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
#include "storage.h"
#include "source.h"

struct source {
	int refs;
	char *path;		/* D-Bus Object path */
	char *name;		/* Local name */
	char *sig;		/* D-Bus like signature */
	uint16_t address;	/* PLC memory address */
	uint16_t interval;	/* Polling interval in ms */
	int storage;		/* Storage identification */
	union {
		bool vbool;
		uint8_t vu8;
		uint16_t vu16;
		uint32_t vu32;
		uint64_t vu64;
	} value;
};

static void source_free(struct source *source)
{
	l_free(source->name);
	l_free(source->sig);
	l_free(source->path);
	l_info("source_free(%p)", source);
	l_free(source);
}

static struct source *source_ref(struct source *source)
{
	if (unlikely(!source))
		return NULL;

	__sync_fetch_and_add(&source->refs, 1);
	l_info("source_ref(%p): %d", source, source->refs);

	return source;
}

static void source_unref(struct source *source)
{
	if (unlikely(!source))
		return;

	l_info("source_unref(%p): %d", source, source->refs - 1);
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

static bool property_get_signature(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 's', source->sig);

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

static bool property_get_value(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_enter_variant(builder, source->sig);
	l_dbus_message_builder_append_basic(builder,
					    source->sig[0], &source->value);
	l_dbus_message_builder_leave_variant(builder);

	return true;
}

static bool property_get_interval(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct source *source = user_data;

	l_dbus_message_builder_append_basic(builder, 'q', &source->interval);

	return true;
}

static void setup_interface(struct l_dbus_interface *interface)
{
	/* Variable alias */
	if (!l_dbus_interface_property(interface, "Name", 0, "s",
				       property_get_name,
				       property_set_name))
		l_error("Can't add 'Name' property");

	/* Variable Signature: Applying D-Bus types to iiot */
	if (!l_dbus_interface_property(interface, "Signature", 0, "s",
				       property_get_signature,
				       NULL))
		l_error("Can't add 'Type' property");

	/* Variable address */
	if (!l_dbus_interface_property(interface, "Address", 0, "q",
				       property_get_address,
				       NULL))
		l_error("Can't add 'Address' property");

	/* Variable RAW Value */
	if (!l_dbus_interface_property(interface, "Value", 0, "v",
				       property_get_value,
				       NULL))
		l_error("Can't add 'Value' property");

	/* Polling interval */
	if (!l_dbus_interface_property(interface, "PollingInterval", 0, "q",
				       property_get_interval,
				       NULL))
		l_error("Can't add 'PollingInterval' property");

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

struct source *source_create(const char *prefix, const char *name,
			     const char *sig, uint16_t address,
			     uint16_t interval, int storage_id, bool store)
{
	char addrstr[7];
	struct source *source;
	char *dpath;

	dpath = l_strdup_printf("%s/source_%04x", prefix, address);

	source = l_new(struct source, 1);
	source->refs = 0;
	source->name = l_strdup(name);
	source->sig= l_strdup(sig);
	source->address = address;
	source->path = NULL;
	source->interval = interval;
	source->storage = storage_id;
	memset(&source->value, 0, sizeof(source->value));

	if (!l_dbus_register_object(dbus_get_bus(),
				    dpath,
				    source_ref(source),
				    (l_dbus_destroy_func_t) source_unref,
				    SOURCE_IFACE, source,
				    L_DBUS_INTERFACE_PROPERTIES,
				    source,
				    NULL)) {
		l_error("Can not register: %s", dpath);
		l_free(dpath);
		return NULL;
	}

	l_info("New source: %s", dpath);

	source->path = dpath;

	/*
	 * store 'false' means that source is being created from persistent
	 * storage, 'true' means that a new source object has been created.
	 */
	if (store) {
		snprintf(addrstr, sizeof(addrstr), "0x%04x", address);
		storage_write_key_string(storage_id, addrstr, "Name", name);
		storage_write_key_string(storage_id, addrstr, "Type", sig);
		storage_write_key_int(storage_id, addrstr,
				      "PollingInterval", interval);
	}

	return source_ref(source);
}

void source_destroy(struct source *source)
{
	l_info("source_destroy(%p)", source);

	if (unlikely(!source))
		return;

	l_dbus_unregister_object(dbus_get_bus(), source->path);
	source_unref(source);
}

const char *source_get_path(const struct source *source)
{
	if (unlikely(!source))
		return NULL;

	return source->path;
}

const char *source_get_signature(const struct source *source)
{
	if (unlikely(!source))
		return NULL;

	return source->sig;
}

uint16_t source_get_address(const struct source *source)
{
	if (unlikely(!source))
		return 0xffff;

	return source->address;
}

uint16_t source_get_interval(const struct source *source)
{
	if (unlikely(!source))
		return 0xffff;

	return source->interval;
}

bool source_set_value_bool(struct source *source, bool value)
{
	if (unlikely(!source))
		return false;

	if (source->value.vbool == value)
		return true;

	source->value.vbool = value;

	l_dbus_property_changed(dbus_get_bus(), source->path,
				SOURCE_IFACE, "Value");

	return true;
}

bool source_set_value_byte(struct source *source, uint8_t value)
{
	if (unlikely(!source))
		return false;

	if (source->value.vu8 == value)
		return true;

	source->value.vu8 = value;

	l_dbus_property_changed(dbus_get_bus(), source->path,
				SOURCE_IFACE, "Value");

	return true;
}

bool source_set_value_u16(struct source *source, uint16_t value)
{
	if (unlikely(!source))
		return false;

	if (source->value.vu16 == value)
		return true;

	source->value.vu16 = value;

	l_dbus_property_changed(dbus_get_bus(), source->path,
				SOURCE_IFACE, "Value");

	return true;
}

bool source_set_value_u32(struct source *source, uint32_t value)
{
	if (unlikely(!source))
		return false;

	if (source->value.vu32 == value)
		return true;

	source->value.vu32 = value;

	l_dbus_property_changed(dbus_get_bus(), source->path,
				SOURCE_IFACE, "Value");

	return true;
}

bool source_set_value_u64(struct source *source, uint64_t value)
{
	if (unlikely(!source))
		return false;

	if (source->value.vu64 == value)
		return true;

	source->value.vu64 = value;

	l_dbus_property_changed(dbus_get_bus(), source->path,
				SOURCE_IFACE, "Value");

	return true;
}
