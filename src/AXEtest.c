/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "AXEengine.h"
#include "AXEschedule.h"


/*-------------------------------------------------------------------------
 * Function:    AXE_test_exclude_close_on
 *
 * Purpose:     Tells the specified engine to fail if it tries to close
 *              with tasks still open.  The engine closes either when
 *              AXEterminate_engine() is called with wait_all set to FALSE
 *              or after all tasks have completed with wait_all set to
 *              TRUE.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              March 22, 2013
 *
 *-------------------------------------------------------------------------
 */
void
AXE_test_exclude_close_on(AXE_engine_t engine)
{
    ((AXE_engine_int_t *)engine)->exclude_close = TRUE;

    return;
} /* end AXE_test_exclude_close_on() */


/*-------------------------------------------------------------------------
 * Function:    AXE_test_exclude_close_off
 *
 * Purpose:     Tells the specified engine to free open tasks when the
 *              engine is closed.  This is the default behavior.  The
 *              engine closes either when AXEterminate_engine() is called
 *              with wait_all set to FALSE or after all tasks have
 *              completed with wait_all set to TRUE.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              March 22, 2013
 *
 *-------------------------------------------------------------------------
 */
void
AXE_test_exclude_close_off(AXE_engine_t engine)
{
    ((AXE_engine_int_t *)engine)->exclude_close = FALSE;

    return;
} /* end AXE_test_exclude_close_off() */

