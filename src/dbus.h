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

#define KNOT_MODBUS_SERVICE		"br.org.cesar.modbus"
#define SLAVE_IFACE			KNOT_MODBUS_SERVICE".Slave1"
#define SOURCE_IFACE			KNOT_MODBUS_SERVICE".Source1"

typedef void (*dbus_setup_completed_func_t) (void *user_data);

int dbus_start(dbus_setup_completed_func_t setup_cb, void *user_data);
void dbus_stop(void);


struct l_dbus_message *dbus_error_invalid_args( struct l_dbus_message *msg);
struct l_dbus *dbus_get_bus(void);
