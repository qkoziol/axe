AXE version 0.1b currently under development
------------------------------------------------------------------------------
An asynchronous concurrent execution engine for a graph of tasks.

For now, the primary documentation is in the comments in AXE.c and axe.h.  All
public API functions are contained in AXE.c, and AXE.c contains no non-public
functions.

All API functions except AXEterminate_engine are designed to be threadsafe, and
may also be called from within a task executed by the engine.
AXEterminate_engine may be called concurrently with other threads as long as
either no other threads are operating on that engine, or wait_all is set to true
and other tasks are still executing in the engine.  In the second case, other
threads must finish with the engine before all tasks complete.

This library reserves no threads for internal use - all scheduling is done by
the application thread during API calls and by task worker threads.

Compiling this library requires OpenPA, available at:
http://trac.mcs.anl.gov/projects/openpa

Primary contact for AXE is Neil Fortner <nfortne2@hdfgroup.org>

