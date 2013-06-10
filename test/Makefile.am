# Copyright by The HDF Group.
# All rights reserved.
#
# This file is part of AXE.  The full AXE copyright notice, including terms
# governing use, modification, and redistribution, is contained in the file
# COPYING at the root of the source code distribution tree.

AM_CFLAGS = -Wall

TESTS = check_max_nthreads test_axe test_id

test_axe.log: check_max_nthreads.log
test_id.log: check_max_nthreads.log

check_PROGRAMS = $(TESTS)

AM_LDFLAGS = -L../src
LDADD = -laxe
DEPENDENCIES = libaxe.la

check_max_nthreads_SOURCES = check_max_nthreads.c axe_test.h

test_axe_SOURCES = test_axe.c axe_test.h

test_id_SOURCES = test_id.c axe_test.h

AM_CPPFLAGS = -I$(top_srcdir)/src

