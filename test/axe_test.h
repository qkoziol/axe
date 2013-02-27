/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include <AXEprivate.h>


/*
 * Macros
 */
#define AT()            printf ("        at %s:%d in %s()...\n", __FILE__, __LINE__, __FUNCTION__)
#define TESTING(WHAT) \
do { \
        printf("Testing %-62s", WHAT); \
    fflush(stdout); \
} while(0)
#define PASSED()        do {puts(" PASSED");fflush(stdout);} while(0)
#define FAILED()        do {puts("*FAILED*");fflush(stdout);} while(0)
#define WARNING()       do {puts("*WARNING*");fflush(stdout);} while(0)
#define SKIPPED()       do {puts(" -SKIP-");fflush(stdout);} while(0)
#define TEST_ERROR      do {FAILED(); AT(); goto error;} while(0)

