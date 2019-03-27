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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ell/ell.h>

#include "storage.h"

static struct l_hashmap *storage_list = NULL;

static int save_settings(int fd, struct l_settings *settings)
{
	char *res;
	size_t res_len;
	int err = 0;

	res = l_settings_to_data(settings, &res_len);

	if (ftruncate(fd, 0) == -1) {
		err = -errno;
		goto failure;
	}

	if (pwrite(fd, res, res_len, 0) < 0)
		err = -errno;

failure:
	l_free(res);

	return err;
}

int storage_open(const char *pathname)
{
	struct l_settings *settings;
	int fd;

	fd = open(pathname, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0)
		return -errno;

	settings = l_settings_new();
	/* Ignore error if file doesn't exists */
	l_settings_load_from_file(settings, pathname);

	if (!storage_list)
		storage_list = l_hashmap_new();

	l_hashmap_insert(storage_list, L_INT_TO_PTR(fd), settings);

	return fd;
}

int storage_close(int fd)
{
	struct l_settings *settings;

	settings = l_hashmap_remove(storage_list, L_INT_TO_PTR(fd));
	if(!settings)
		return -ENOENT;

	l_settings_free(settings);

	return close(fd);
}

void storage_foreach_slave(int fd, storage_foreach_slave_t func,
						void *user_data)
{
	struct l_settings *settings;
	char **groups;
	char *name;
	char *address;
	int i;

	settings = l_hashmap_lookup(storage_list, L_INT_TO_PTR(fd));
	if (!settings)
		return;

	groups = l_settings_get_groups(settings);

	for (i = 0; groups[i] != NULL; i++) {
		name = l_settings_get_string(settings, groups[i], "Name");
		if (!name)
			continue;

		address = l_settings_get_string(settings, groups[i], "Address");
		if (address)
			func(groups[i], name, address, user_data);

		l_free(address);
		l_free(name);
		l_free(groups[i]);
	}

	l_free(groups);
}

int storage_write_key_string(int fd, const char *group,
			     const char *key, const char *value)
{
	struct l_settings *settings;

	settings = l_hashmap_lookup(storage_list, L_INT_TO_PTR(fd));
	if (!settings)
		return -EIO;

	if (l_settings_set_string(settings, group, key, value) == false)
		return -EINVAL;

	return save_settings(fd, settings);
}


