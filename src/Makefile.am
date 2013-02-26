AM_CFLAGS = -Wall -g -fno-strict-aliasing

# Set the soversion number for the shared library
libae2_la_LDFLAGS = -version-info 1:0:0

include_HEADERS = \
	AE2.h

lib_LTLIBRARIES = libae2.la
libae2_la_SOURCES = \
	AE2.h \
	AE2.c \
	AE2engine.h \
	AE2engine.c \
	AE2private.h \
	AE2schedule.h \
	AE2schedule.c \
	AE2task.h \
	AE2task.c \
	AE2threadpool.h \
	AE2threadpool.c

