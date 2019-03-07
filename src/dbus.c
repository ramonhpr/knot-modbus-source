/*
 * This file is part of the KNOT Project
 *
 * Copyright (c) 2019, CESAR. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdio.h>
#include <ell/ell.h>

#include "dbus.h"

static struct l_dbus *g_dbus = NULL;

struct setup {
	dbus_setup_completed_func_t complete;
	void *user_data;
};

struct l_dbus_message *dbus_error_invalid_args( struct l_dbus_message *msg)
{
	return l_dbus_message_new_error(msg, KNOT_MODBUS_SERVICE ".InvalidArgs",
					"Argument type is wrong");
}

static void dbus_disconnect_callback(void *user_data)
{
}

static void dbus_request_name_callback(struct l_dbus *dbus, bool success,
					bool queued, void *user_data)
{
	struct setup *setup = user_data;

	if (!success)
		return;

	if (!l_dbus_object_manager_enable(g_dbus)) {
		l_error("Unable to register the ObjectManager");
		return;
	}

	setup->complete(setup->user_data);
}

static void ready_callback(void *user_data)
{
	l_dbus_name_acquire(g_dbus, KNOT_MODBUS_SERVICE, false, false, false,
			    dbus_request_name_callback, user_data);
}

struct l_dbus *dbus_get_bus(void)
{
	return g_dbus;
}

int dbus_start(dbus_setup_completed_func_t setup_cb, void *user_data)
{
	struct setup *setup;

	l_info("Starting dbus ...");

	g_dbus = l_dbus_new_default(L_DBUS_SYSTEM_BUS);

	setup = l_new(struct setup, 1);
	setup->complete = setup_cb;
	setup->user_data = user_data;

	l_dbus_set_ready_handler(g_dbus, ready_callback, setup, l_free);

	l_dbus_set_disconnect_handler(g_dbus,
				      dbus_disconnect_callback,
				      NULL, NULL);

	return 0;
}


void dbus_stop(void)
{
	l_dbus_destroy(g_dbus);
}
