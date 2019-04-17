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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <ell/ell.h>

#include <modbus.h>
#include <string.h>

#include "dbus.h"
#include "storage.h"
#include "source.h"
#include "slave.h"

struct slave {
	int refs;
	char *key;	/* Local random id */
	uint8_t id;	/* modbus slave id */
	char *name;
	char *path;
	char *ipaddress;
	char *hostname;
	char *port; /* getaddrinfo service */
	modbus_t *tcp;
	struct l_io *io;
	struct l_queue *source_list;
	struct l_hashmap *to_list;
	int sources_fd;
};

struct bond {
	struct slave *slave;
	struct source *source;
};

static int slaves_fd;

static bool path_cmp(const void *a, const void *b)
{
	const struct source *source = a;
	const char *b1 = b;

	return (strcmp(source_get_path(source), b1) == 0 ? true : false);
}

static bool address_cmp(const void *a, const void *b)
{
	const struct source *source = a;
	uint16_t address = L_PTR_TO_INT(b);

	return (source_get_address(source) == address ? true : false);
}

static void timeout_destroy(void *data)
{
	struct l_timeout *timeout = data;

	l_timeout_remove(timeout);
}

static void slave_free(struct slave *slave)
{
	l_queue_destroy(slave->source_list,
			(l_queue_destroy_func_t) source_destroy);
	l_hashmap_destroy(slave->to_list, timeout_destroy);

	if (slave->io)
		l_io_destroy(slave->io);
	if (slave->tcp) {
		modbus_close(slave->tcp);
		modbus_free(slave->tcp);
	}
	storage_close(slave->sources_fd);
	l_free(slave->key);
	l_free(slave->ipaddress);
	l_free(slave->hostname);
	l_free(slave->port);
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

static void create_slave_from_storage(const char *key,
				int slave_id,
				const char *name,
				const char *address,
				void *user_data)
{
	struct l_queue *list = user_data;
	struct slave *slave;

	slave = slave_create(key, slave_id, name, address);
	if (!slave)
		return;

	l_queue_push_head(list, slave);
}

static void create_source_from_storage(const char *address,
				const char *name,
				const char *type,
				int interval,
				void *user_data)
{
	struct slave *slave = user_data;
	struct source *source;
	unsigned int uaddr;

	if (sscanf(address, "0x%04x", &uaddr) != 1)
		return;

	source = source_create(slave->path, name, type, uaddr, interval);
	if (!source)
		return;

	l_queue_push_head(slave->source_list, source);
}

static void tcp_disconnected_cb(struct l_io *io, void *user_data)
{
	struct slave *slave = user_data;

	l_info("slave %p disconnected", slave);

	l_hashmap_destroy(slave->to_list, timeout_destroy);
	slave->to_list = NULL;

	modbus_close(slave->tcp);
	modbus_free(slave->tcp);
	slave->tcp = NULL;

	l_io_destroy(slave->io);
	slave->io = NULL;

	l_dbus_property_changed(dbus_get_bus(), slave->path,
				SLAVE_IFACE, "Online");
}

static void polling_to_expired(struct l_timeout *timeout, void *user_data)
{
	struct bond *bond = user_data;
	struct source *source = bond->source;
	struct slave *slave = bond->slave;
	const char *sig = source_get_signature(source);
	uint16_t u16_addr = source_get_address(source);
	uint8_t val_u8 = 0;
	uint16_t val_u16 = 0;
	uint32_t val_u32 = 0;
	uint64_t val_u64 = 0;
	int ret = 0, err;

	l_info("modbus reading source %p addr:(0x%x)", source, u16_addr);

	switch (sig[0]) {
	case 'b':
		ret = modbus_read_input_bits(slave->tcp, u16_addr, 1, &val_u8);
		if (ret != -1)
			source_set_value_bool(source, val_u8 ? true : false);
		break;
	case 'y':
		ret = modbus_read_input_bits(slave->tcp, u16_addr, 8, &val_u8);
		if (ret != -1)
			source_set_value_byte(source, val_u8);

		break;
	case 'q':
		ret = modbus_read_registers(slave->tcp, u16_addr, 1, &val_u16);
		if (ret != -1)
			source_set_value_u16(source, val_u16);
		break;
	case 'u':
		/* Assuming network order */
		ret = modbus_read_registers(slave->tcp, u16_addr, 2,
					    (uint16_t *) &val_u32);
		if (ret != -1)
			source_set_value_u32(source, L_BE32_TO_CPU(val_u32));
		break;
	case 't':
		/* Assuming network order */
		ret = modbus_read_registers(slave->tcp, u16_addr, 4,
					    (uint16_t *) &val_u64);
		if (ret != -1)
			source_set_value_u64(source, L_BE64_TO_CPU(val_u64));
		break;
	default:
		break;
	}

	if (ret == -1) {
		err = errno;
		l_error("read(%x): %s(%d)", u16_addr, strerror(err), err);
	}

	l_timeout_modify_ms(timeout, source_get_interval(source));
}

static void polling_start(void *data, void *user_data)
{
	struct slave *slave = user_data;
	struct source *source = data;
	struct l_timeout *timeout;
	struct bond *bond;

	bond = l_new(struct bond, 1);
	bond->source = source;
	bond->slave = slave;

	timeout = l_timeout_create_ms(source_get_interval(source),
				      polling_to_expired, bond, l_free);

	l_hashmap_insert(slave->to_list, source_get_path(source), timeout);

	l_info("source(%p): %s interval: %d", source,
	       source_get_path(source),
	       source_get_interval(source));
}

static int enable_slave(struct slave *slave)
{
	int err;

	/* Already connected ? */
	if (slave->tcp)
		return -EALREADY;

	slave->tcp = modbus_new_tcp_pi(slave->hostname, slave->port);
	modbus_set_slave(slave->tcp, slave->id);

	err = modbus_connect(slave->tcp);
	l_info("connect() %s:%s (%d)", slave->hostname, slave->port, err);
	if (err != -1) {
		slave->io = l_io_new(modbus_get_socket(slave->tcp));
		if (slave->io == NULL)
			goto error;

		l_io_set_disconnect_handler(slave->io, tcp_disconnected_cb,
					    slave_ref(slave),
					    (l_io_destroy_cb_t) slave_unref);

		l_queue_foreach(slave->source_list,
				polling_start, slave);
		return 0;
	}

error:
	/* Releasing connection */
	err = errno;
	modbus_close(slave->tcp);
	modbus_free(slave->tcp);
	slave->tcp = NULL;

	return -err;
}

static int disable_slave(struct slave *slave)
{
	/* Already closed? */
	if (slave->tcp == NULL)
		return -EALREADY;

	/* Releasing connection */
	modbus_close(slave->tcp);
	modbus_free(slave->tcp);
	slave->tcp = NULL;

	return 0;
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
	char addrstr[7];
	char signature[2];
	const char *key = NULL;
	const char *name = NULL;
	const char *type = NULL;
	uint16_t address = 0;
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
		/* Memory address */
		else if (strcmp(key, "Address") == 0)
			ret = l_dbus_message_iter_get_variant(&value,
							      "q", &address);
		else if (strcmp(key, "PollingInterval") == 0)
			ret = l_dbus_message_iter_get_variant(&value,
							      "q", &interval);
		else
			return dbus_error_invalid_args(msg);

		if (!ret)
			return dbus_error_invalid_args(msg);
	}

	/* FIXME: validate type */
	if (!name || address == 0 || !type || strlen(type) != 1)
		return dbus_error_invalid_args(msg);

	/* Restricted to basic D-Bus types: bool, byte, u16, u32, u64 */
	memset(signature, 0, sizeof(signature));
	ret = sscanf(type, "%[byqut]1s", signature);
	if (ret != 1) {
		l_info("Limited to basic types only!");
		return dbus_error_invalid_args(msg);
	}

	source = l_queue_find(slave->source_list,
			      address_cmp, L_INT_TO_PTR(address));
	if (source) {
		l_error("source: address assigned already");
		return dbus_error_invalid_args(msg);
	}

	source = source_create(slave->path, name, type, address, interval);
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

	snprintf(addrstr, sizeof(addrstr), "0x%04x", address);
	storage_write_key_string(slave->sources_fd, addrstr,
				 "Name", name);
	storage_write_key_string(slave->sources_fd, addrstr,
				 "Type", type);
	storage_write_key_int(slave->sources_fd, addrstr,
			      "PollingInterval", interval);

	if (slave->io)
		polling_start(source, slave);

	return reply;
}

static struct l_dbus_message *method_source_remove(struct l_dbus *dbus,
						struct l_dbus_message *msg,
						void *user_data)
{
	struct slave *slave = user_data;
	struct source *source;
	const char *opath;
	char addrstr[7];

	if (!l_dbus_message_get_arguments(msg, "o", &opath))
		return dbus_error_invalid_args(msg);

	source = l_queue_remove_if(slave->source_list, path_cmp, opath);
	if (unlikely(!source))
		return dbus_error_invalid_args(msg);

	snprintf(addrstr, sizeof(addrstr), "0x%04x",
		 source_get_address(source));

	if (storage_remove_group(slave->sources_fd, addrstr) < 0)
		l_info("storage(): Can't delete source!");

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

	storage_write_key_string(slaves_fd, slave->key, "Name", name);

	return NULL;
}

static bool property_get_ipaddress(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct slave *slave = user_data;

	/* PLC/Peer IP address */
	l_dbus_message_builder_append_basic(builder, 's', slave->ipaddress);

	return true;
}

static bool property_get_online(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct slave *slave = user_data;
	bool online;

	online = (slave->tcp ? true : false);

	l_dbus_message_builder_append_basic(builder, 'b', &online);

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
	int ret;

	if (!l_dbus_message_iter_get_variant(new_value, "b", &enable))
		return dbus_error_invalid_args(msg);

	if (enable == false)
		/* Shutdown modbus tcp */
		ret = disable_slave(slave);
	else
		/* Connect & enable polling */
		ret = enable_slave(slave);

	if (ret < 0 && ret != -EALREADY)
		return dbus_error_errno(msg, "Connect", -ret);

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

	/* Per/PLC IP address including port. Format: 'hostname:port' */
	if (!l_dbus_interface_property(interface, "IpAddress", 0, "s",
				       property_get_ipaddress,
				       NULL))
		l_error("Can't add 'IpAddress' property");

	/* Online: connected to slave */
	if (!l_dbus_interface_property(interface, "Online", 0, "b",
				       property_get_online,
				       property_set_enable))
		l_error("Can't add 'Online' property");

}

struct slave *slave_create(const char *key, uint8_t id,
			   const char *name, const char *address)
{
	struct slave *slave;
	struct stat st;
	char *dpath;
	char *filename;
	char hostname[128];
	char port[8];
	int st_ret;

	/* "host:port or /dev/ttyACM0, /dev/ttyUSB0, ..."*/

	memset(hostname, 0, sizeof(hostname));
	memset(port, 0, sizeof(port));
	if (sscanf(address, "%127[^:]:%7s", hostname, port) != 2) {
		l_error("Address (%s) not supported: Invalid format", address);
		return NULL;
	}

	dpath = l_strdup_printf("/slave_%s", key);

	slave = l_new(struct slave, 1);
	slave->refs = 0;
	slave->key = l_strdup(key);
	slave->id = id;
	slave->name = l_strdup(name);
	slave->ipaddress = l_strdup(address);
	slave->hostname = l_strdup(hostname);
	slave->port = l_strdup(port);
	slave->tcp = NULL;
	slave->io = NULL;
	slave->source_list = l_queue_new();
	slave->to_list = l_hashmap_string_new();

	filename = l_strdup_printf("%s/%s/sources.conf",
				   STORAGEDIR, slave->key);

	memset(&st, 0, sizeof(st));
	st_ret = stat(filename, &st);

	slave->sources_fd = storage_open(filename);
	l_free(filename);

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

	l_info("Slave(%p): (%s) hostname: (%s) port: (%s)",
					slave, dpath, hostname, port);

	if (st_ret == 0) {
		/* Slave created from storage */
		storage_foreach_source(slave->sources_fd,
				       create_source_from_storage, slave);
	} else {
		/* New slave */
		storage_write_key_int(slaves_fd, key, "Id", id);
		storage_write_key_string(slaves_fd, key,
					 "Name", name ? : address);
		storage_write_key_string(slaves_fd, key,
					 "IpAddress", address);
	}

	return slave_ref(slave);
}

void slave_destroy(struct slave *slave, bool rm)
{
	char *filename;
	int err;

	l_info("slave_destroy(%p)", slave);

	if (unlikely(!slave))
		return;

	if (slave->io)
		l_io_set_disconnect_handler(slave->io, NULL, NULL, NULL);

	l_dbus_unregister_object(dbus_get_bus(), slave->path);

	if (!rm)
		goto done;

	/* Remove stored data: sources.conf */
	filename = l_strdup_printf("%s/%s/sources.conf",
				   STORAGEDIR, slave->key);
	if (unlink(filename) == -1) {
		err = errno;
		l_error("unlink(%s): %s(%d)", filename, strerror(err), err);
	}

	l_free(filename);

	/* Remove stored data: directory */
	filename = l_strdup_printf("%s/%s", STORAGEDIR, slave->key);
	if (rmdir(filename) == -1) {
		err = errno;
		l_error("unlink(%s): %s(%d)", filename, strerror(err), err);
	}

	l_free(filename);

	/* Remove group from slaves.conf */
	if (storage_remove_group(slaves_fd, slave->key) < 0)
		l_info("storage(): Can't delete slave!");

done:
	slave_unref(slave);
}

const char *slave_get_path(const struct slave *slave)
{
	if (unlikely(!slave))
		return NULL;

	return slave->path;
}

struct l_queue *slave_start(void)
{
	const char *filename = STORAGEDIR "/slaves.conf";
	struct l_queue *list;

	l_info("Starting slave ...");

	/* Slave settings file */
	slaves_fd = storage_open(filename);
	if (slaves_fd < 0) {
		l_error("Can not open/create slave files!");
		return NULL;
	}

	if (!l_dbus_register_interface(dbus_get_bus(),
				       SLAVE_IFACE,
				       setup_interface,
				       NULL, false))
		l_error("dbus: unable to register %s", SLAVE_IFACE);

	source_start();

	list = l_queue_new();
	storage_foreach_slave(slaves_fd, create_slave_from_storage, list);

	return list;
}

void slave_stop(void)
{

	storage_close(slaves_fd);

	source_stop();

	l_dbus_unregister_interface(dbus_get_bus(),
				    SLAVE_IFACE);
}
