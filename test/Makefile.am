# Copyright by The HDF Group.
# All rights reserved.
#
# This file is part of AXE.  The full AXE copyright notice, including terms
# governing use, modification, and redistribution, is contained in the file
# COPYING at the root of the source code distribution tree.

AM_CFLAGS = -Wall

TESTS = check_max_nthreads test_all

test_all.log: check_max_nthreads.log

check_PROGRAMS = $(TESTS)

AM_LDFLAGS = -L../src
LDADD = -laxe
DEPENDENCIES = libaxe.la

test_all_SOURCES = test_all.c axe_test.h

check_max_nthreads_SOURCES = check_max_nthreads.c axe_test.h

AM_CPPFLAGS = -I$(top_srcdir)/src

