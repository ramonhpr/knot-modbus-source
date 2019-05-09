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

#include <stdint.h>
#include <string.h>

#include <tinycbor/cbor.h>
#include "smoke.h"

static int smoke_probe(void)
{
	return 0;
}

static void smoke_remove(void)
{

}

static int smoke_create(uint64_t id, CborValue *it)
{
	/* Return sock */
	return 0;
}

static int smoke_destroy(int sock, bool purge)
{
	return 0;
}

static int smoke_send(int sock, CborValue *it)
{
	return 0;
}

static CborValue *smoke_recv(int sock, int *err)
{
	return NULL;
}


struct smoke_driver fog = {
	.name = "KNoT",
	.probe = smoke_probe,
	.remove = smoke_remove,
	.create = smoke_create,
	.destroy = smoke_destroy,
	.send = smoke_send,
	.recv = smoke_recv,
};
