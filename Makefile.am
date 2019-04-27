AM_MAKEFLAGS = --no-print-directory

modules_sources =
modules_ldadd =
modules_cflags =

AM_CFLAGS = $(WARNING_CFLAGS) $(BUILD_CFLAGS)
AM_LDFLAGS = $(BUILD_LDFLAGS)

bin_PROGRAMS = src/modbusd
noinst_PROGRAMS =

src_modbusd_SOURCES = src/main.c \
			src/manager.h src/manager.c \
			src/slave.h src/slave.c \
			src/source.h src/source.c \
			src/dbus.h src/dbus.c \
			src/options.h src/driver.h \
			src/tcp.c src/rtu.c \
			src/storage.h src/storage.c

src_modbusd_LDADD = $(modules_ldadd) @ELL_LIBS@  @MODBUS_LIBS@ -lm
src_modbusd_LDFLAGS = $(AM_LDFLAGS)
src_modbusd_CFLAGS = $(AM_CFLAGS) $(modules_cflags) @ELL_CFLAGS@ @MODBUS_CFLAGS@

DISTCLEANFILES =
EXTRA_DIST = src/main.conf src/units.conf

if CONFIGFILES
confdir = $(sysconfdir)/modbus
conf_DATA = src/main.conf
conf_DATA += src/units.conf
endif

MAINTAINERCLEANFILES = Makefile.in \
	aclocal.m4 configure config.h.in config.sub config.guess \
	ltmain.sh depcomp compile missing install-sh

clean-local:
	$(RM) -r src/modbusd
