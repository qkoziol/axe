TESTS = test_all

check_PROGRAMS = $(TESTS)

AM_LDFLAGS = -L../src
LDADD = -lae2
DEPENDENCIES = libae2.la

test_all_SOURCES = test_all.c ae2_test.h

AM_CPPFLAGS = -I$(top_srcdir)/src

