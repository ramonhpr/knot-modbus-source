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

struct main_options {
	bool		tcp;			/* D-Bus TCP - default false */
	uint16_t	polling_interval;	/* Source reading interval */
};

/*
 * Supported baud rates:
 *	110, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 38400,
 *	57600, 115200, 230400, 250000, 460800, 500000, 921600, 1000000
 */
struct serial_options {
	int		baud;  /* See above */
	char		parity; /* N, E, O */
	int		data_bit; /* 5, 6, 7, 8 */
	int		stop_bit; /* 1 or 2 */
};

extern struct main_options main_opts;
extern struct serial_options serial_opts;
