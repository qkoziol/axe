#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([ae2], [0.1a], [nfortne2@hdfgroup.org])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/AE2.c])
AC_CONFIG_HEADER([src/config.h])

# Init automake
AM_INIT_AUTOMAKE
AM_SILENT_RULES

# Set the prefix to a ae2 directory in the source root.
AC_PREFIX_DEFAULT([`pwd`/ae2])

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Initialize libtool
LT_INIT

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
    AC_MSG_ERROR([opa library required to build AE2])
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
# Check for presence of pthread_yield.  If not present, check for sched_yield.
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

