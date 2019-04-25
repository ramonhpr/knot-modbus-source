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
#include <signal.h>
#include <getopt.h>

#include <ell/ell.h>

#include "manager.h"

static const char *opts_file;

static void signal_handler(uint32_t signo, void *user_data)
{
	switch (signo) {
	case SIGINT:
	case SIGTERM:
		l_info("Terminate");
		l_main_quit();
		break;
	}
}

static const struct option main_options[] = {
	{ "config",		required_argument,	NULL, 'c' },
	{ "help",		no_argument,		NULL, 'h' },
	{ }
};

static int parse_args(int argc, char *argv[])
{
	int opt;

	for (;;) {
		opt = getopt_long(argc, argv, "c:",
				  main_options, NULL);
		if (opt < 0)
			break;

		switch (opt) {
		case 'c':
			opts_file = optarg;
			break;
		default:
			return -EINVAL;
		}
	}

	if (argc - optind > 0) {
		fprintf(stderr, "Invalid command line parameters\n");
		return -EINVAL;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = parse_args(argc, argv);
	if (ret < 0)
		return ret;

	if (!l_main_init())
		return EXIT_FAILURE;

	l_log_set_stderr();

	if (manager_start(opts_file) < 0)
		goto main_exit;

	l_main_run_with_signal(signal_handler, NULL);

	manager_stop();
main_exit:
	l_main_exit();

	return 0;
}
