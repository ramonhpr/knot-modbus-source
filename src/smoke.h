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

/*
 * Driver to allow publish & subscribe data from clouds. Initially targetted
 * to KNoT fog based on meshblu. Which driver must keep cloud specific data
 * internally without push dependency to 'slave' core APIs. Variable/Offset
 * value shoud be used to map to sensor_ids, units are based on IEEE 260.1
 * When creating CborValue is an array of sensor ID and unit. When pushing
 * data, it is an array of sensor ID and value.
 * 'send' and 'recv' callbacks handle CborValue array using the same format
 * each array entry contains { sensor_id, basic_value }. 'schema' array entry
 * contains { sensor_id, unit}.
 */

struct smoke_driver {
	const char *name;
	int (*probe) (void);
	void (*remove) (void);
	int (*create) (uint64_t id, CborValue *schema);
	int (*destroy) (int sock, bool purge);
	int (*send) (int sock, CborValue *value);
	CborValue *(*recv) (int sock, int *err);
};
