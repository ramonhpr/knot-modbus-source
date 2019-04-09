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

struct source;

int source_start(void);
void source_stop(void);

struct source;
struct source *source_create(const char *prefix, const char *name,
			  const char *sig, uint16_t address, uint16_t interval);
void source_destroy(struct source *source);
const char *source_get_path(const struct source *source);
const char *source_get_signature(const struct source *source);
uint16_t source_get_address(const struct source *source);
uint16_t source_get_interval(const struct source *source);

bool source_set_value_bool(struct source *source, bool value);
bool source_set_value_byte(struct source *source, uint8_t value);
bool source_set_value_u16(struct source *source, uint16_t value);
bool source_set_value_u32(struct source *source, uint32_t value);
