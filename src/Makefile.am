# Copyright by The HDF Group.
# All rights reserved.
#
# This file is part of AXE.  The full AXE copyright notice, including terms
# governing use, modification, and redistribution, is contained in the file
# COPYING at the root of the source code distribution tree.

AM_CFLAGS = -Wall

# Set the soversion number for the shared library
libaxe_la_LDFLAGS = -version-info 1:0:0

include_HEADERS = \
	AXE.h

lib_LTLIBRARIES = libaxe.la
libaxe_la_SOURCES = \
	AXE.h \
	AXE.c \
	AXEengine.h \
	AXEengine.c \
	AXEprivate.h \
	AXEschedule.h \
	AXEschedule.c \
	AXEtask.h \
	AXEtask.c \
	AXEthreadpool.h \
	AXEthreadpool.c

