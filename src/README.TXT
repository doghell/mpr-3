README.TXT for the Multithreaded Portable Runtime.
--------------------------------------------------------------------------------

The Multithreaded Portable Runtime provides a cross-platform abstraction to 
hide per operating/system dependencies. Implemented in "C", it can be used
in both C and C++ programs. It implements an interface that is sufficient to 
support AppWeb and it does not try to be a complete cross-platform API.

This document describes the important files in this directory.

Directory       Purpose
-----------------------
samples         - Sample source code for the MPR.
test            - MPR unit tests.
UNIX            - UNIX specific code.
WIN             - Windows specific code.
package         - Directory containing packge file lists.


File            Purpose
-----------------------
buf.cpp         - The buffer class. Used for expandable ring queues.
embedded.cpp    - Safe embedded routines including safe string handling.
file.cpp        - Virtual file system class.
files           - List of files to put into the mpr library.
hash.cpp        - The Hash class. Used for general hash indexing.
libmpr.dsp      - Visual Studio project for building the MPR.
libmprTest.dsp  - Visual Studio project for building the MPR unit tests.
list.cpp        - List base class. Used extensively by factory objects. 
log.cpp         - MPR logging and debug trace class.
make.dep        - Dynamic makefile dependencies.
Makefile        - MPR Makefile.
malloc.cpp      - Fast malloc. Not required, but good at reducing fragmentation.
miniMpr.c       - Mini MPR for stand-alone use without the full MPR>
miniMpr.h       - Mini MPR header.
mpr.cpp         - MPR startup logic.
mpr.h           - Primary MPR header.
mprOs.h         - MPR operating system abstraction header.
mpr.rc          - Windows resource file
select.cpp      - Posix Select class.
socket.cpp      - Socket class.
sourceFiles     - Source file list to include in the Visual Studio projects.
task.cpp        - Task class. Used to run and manage multithread tasks.
test.cpp        - Unit test framework. Based on JUnit.
test.h          - Test header.
timer.cpp       - Timer class. Used to schedule routines to run.
var.cpp         - Universal variable and object type.

WIN/UNIX/MACOSX
daemon.cpp      - Code to run MPR as a system service / daemon
os.cpp          - O/S dependent code
thread.cpp      - Thread handling code

--------------------------------------------------------------------------------

Copyright (c) 2003-2011 Embedthis Software, LLC. All Rights Reserved.
Embedthis and AppWeb are trademarks of Embedthis Software, LLC. Other 
brands and their products are trademarks of their respective holders.

See LICENSE.TXT for software license details.
