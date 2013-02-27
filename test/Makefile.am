TESTS = test_all

check_PROGRAMS = $(TESTS)

AM_LDFLAGS = -L../src
LDADD = -laxe
DEPENDENCIES = libaxe.la

test_all_SOURCES = test_all.c axe_test.h

AM_CPPFLAGS = -I$(top_srcdir)/src

