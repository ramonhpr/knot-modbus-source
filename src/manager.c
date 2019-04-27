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
#include "options.h"
#include "slave.h"
#include "storage.h"
#include "manager.h"

struct main_options main_opts;
struct serial_options serial_opts;

static struct l_queue *slave_list;

static void entry_destroy(void *data)
{
	struct slave *slave = data;

	/* false: don't remove from storage */
	slave_destroy(slave, false);
}

static bool path_cmp(const void *a, const void *b)
{
	const struct slave *slave = a;
	const char *b1 = b;

	return (strcmp(slave_get_path(slave), b1) == 0 ? true : false);
}

static int options_load(const char *filename)
{
	char *parity;
	int strg;

	/* TODO: missing D-Bus settings */
	main_opts.tcp = false;
	main_opts.polling_interval = 1000; /* 1000ms */

	serial_opts.baud = 115200;
	serial_opts.parity = 'N';
	serial_opts.data_bit = 8;
	serial_opts.stop_bit = 1;

	if (!filename)
		return 0;

	strg = storage_open(filename);
	if (strg < 0)
		return strg;

	storage_read_key_int(strg, "Serial", "Baud", &serial_opts.baud);
	storage_read_key_int(strg, "Serial", "DataBit", &serial_opts.data_bit);
	storage_read_key_int(strg, "Serial", "StopBit", &serial_opts.stop_bit);

	parity = storage_read_key_string(strg, "Serial", "Parity");
	if (parity) {
		serial_opts.parity = parity[0];
		l_free(parity);
	}

	storage_close(strg);

	return 0;
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
	uint8_t slave_id = 0xff;
	char randomkeystr[17];
	uint64_t randomkey;

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
		else if (strcmp(key, "URL") == 0)
			l_dbus_message_iter_get_variant(&value, "s", &address);
		else if (strcmp(key, "Id") == 0)
			l_dbus_message_iter_get_variant(&value, "y", &slave_id);
		else
			return dbus_error_invalid_args(msg);
	}

	l_info("Creating new slave(%d, %s) ...", slave_id, address);

	if (!address) {
		l_error("URL missing!");
		return dbus_error_invalid_args(msg);
	}

	if (slave_id  > 247) {
		l_error("Slave id out of range (0 - 247)!");
		return dbus_error_invalid_args(msg);
	}

	if (l_getrandom(&randomkey, sizeof(randomkey)) == false) {
		l_error("l_getrandom(): not supported");
		return dbus_error_errno(msg, "Internal", ENOSYS);
	}

	snprintf(randomkeystr, sizeof(randomkeystr), "%016lx", randomkey);

	slave = slave_create(randomkeystr, slave_id, name ? : address, address);
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
	if (!slave) {
		l_error("Slave does not exist!");
		return dbus_error_invalid_args(msg);
	}

	/* true: remove storage */
	slave_destroy(slave, true);

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
				       MANAGER_IFACE,
				       setup_interface,
				       NULL, false))
		l_error("dbus: unable to register %s", MANAGER_IFACE);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 "/",
					 MANAGER_IFACE,
					 NULL))
		l_error("dbus: unable to add %s to '/'", MANAGER_IFACE);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 "/",
					 L_DBUS_INTERFACE_PROPERTIES,
					 NULL))
		l_error("dbus: unable to add %s to '/'",
			L_DBUS_INTERFACE_PROPERTIES);

	/* Returns list of created slaves (from storage) */
	slave_list = slave_start(user_data);
}

int manager_start(const char *opts_filename, const char *units_filename)
{
	l_info("Starting manager ...");

	options_load(opts_filename);

	return dbus_start(ready_cb, (void *) units_filename);
}

void manager_stop(void)
{
	l_info("Stopping manager ...");
	l_queue_destroy(slave_list, entry_destroy);
	slave_stop();
	dbus_stop();
}
