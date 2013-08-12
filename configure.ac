#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#
# Copyright by The HDF Group.
# All rights reserved.
#
# This file is part of AXE.  The full AXE copyright notice, including terms
# governing use, modification, and redistribution, is contained in the file
# COPYING at the root of the source code distribution tree.

AC_PREREQ(2.59)
AC_INIT([axe], [0.3b], [nfortne2@hdfgroup.org])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/AXE.c])
AC_CONFIG_HEADER([src/config.h])

# Init automake
AM_INIT_AUTOMAKE
AM_SILENT_RULES

# Set the prefix to an axe directory in the source root.
AC_PREFIX_DEFAULT([`pwd`/axe])

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Default to building static library, because opa by default builds itself
# statically without -fPIC, preventing it from working with a shared library.
AC_DISABLE_SHARED

# Initialize libtool.
# Bug in libtool adds -O2 and -g by default
save_cflags=$CFLAGS
LT_INIT
CFLAGS=$save_cflags

# ----------------------------------------------------------------------
# Check headers
AC_CHECK_HEADERS([pthread.h], [], [
    echo "Required headers missing!"
    exit 1
    ])

# ----------------------------------------------------------------------
# Check for pthread
AC_CHECK_LIB(pthread, pthread_create, [], [
    echo "pthread not found!"
    exit 1
    ])

# ----------------------------------------------------------------------
# Check for opa
AC_ARG_WITH([opa],
            [AC_HELP_STRING([--with-opa=DIR],
                            [Use opa library [default=yes]])],,
            [withval=yes])

case "$withval" in
  yes)
    HAVE_OPA="yes"
    AC_CHECK_HEADERS([opa_primitives.h opa_queue.h],, [unset HAVE_OPA])
    AC_CHECK_LIB([opa], [OPA_Interprocess_lock_init],, [unset HAVE_OPA])

    if test -z "$HAVE_OPA"; then
      AC_MSG_ERROR([could not find opa library])
    fi
    ;;
  no)
    AC_MSG_ERROR([opa library required to build AXE])
    ;;
  *)
    HAVE_OPA="yes"
    case "$withval" in
      *,*)
        opa_inc="`echo $withval | cut -f1 -d,`"
        opa_lib="`echo $withval | cut -f2 -d, -s`"
        ;;
      *)
        if test -n "$withval"; then
          opa_inc="$withval/include"
          opa_lib="$withval/lib"
        fi
        ;;
    esac

    ## Trying to include -I/usr/include and -L/usr/lib is redundant and
    ## can mess some compilers up.
    if test "X$opa_inc" = "X/usr/include"; then
      opa_inc=""
    fi
    if test "X$opa_lib" = "X/usr/lib"; then
      opa_lib=""
    fi

    if test -n "$opa_inc"; then
      CPPFLAGS="$CPPFLAGS -I$opa_inc"
    fi

    AC_CHECK_HEADERS([opa_primitives.h opa_queue.h],, [unset HAVE_OPA])

    if test -n "$opa_lib"; then
      LDFLAGS="$LDFLAGS -L$opa_lib"
    fi

    AC_CHECK_LIB([opa], [OPA_Interprocess_lock_init],, [unset HAVE_OPA])

    if test -z "$HAVE_OPA"; then
      AC_MSG_ERROR([could not find opa library])
    fi
    ;;
esac

# ----------------------------------------------------------------------
# Check for presence of sched_yield.  If not present, check for pthread_yield.
AC_CHECK_HEADER(sched.h,
    [AC_CHECK_FUNCS(sched_yield,
        ,
        [AC_CHECK_FUNCS(pthread_yield)]
    )],
    [AC_CHECK_FUNCS(pthread_yield)]
)

AC_CONFIG_FILES([
    Makefile 
    src/Makefile 
    test/Makefile]) 
AC_OUTPUT

