/*
 *  mprOs.h -- Include O/S headers and smooth out per-O/S differences
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/******************************* Documentation ********************************/

/*
 *  This header is part of the Multithreaded Portable Runtime (MPR) and aims to include
 *  all necessary O/S headers and to unify the constants and declarations 
 *  required by Embedthis products. It can be included by C or C++ programs.
 */

/******************************************************************************/

#ifndef _h_MPR_OS_HDRS
#define _h_MPR_OS_HDRS 1

#include    "buildConfig.h"

/********************************* CPU Families *******************************/
/*
 *  Porters, add your CPU families here and update configure code. 
 */
#define MPR_CPU_UNKNOWN     0
#define MPR_CPU_IX86        1
#define MPR_CPU_PPC         2
#define MPR_CPU_SPARC       3
#define MPR_CPU_XSCALE      4
#define MPR_CPU_ARM         5
#define MPR_CPU_MIPS        6
#define MPR_CPU_68K         7
#define MPR_CPU_SIMNT       8           /* VxWorks NT simulator */
#define MPR_CPU_SIMSPARC    9           /* VxWorks sparc simulator */
#define MPR_CPU_IX64        10          /* AMD64 or EMT64 */
#define MPR_CPU_UNIVERSAL   11          /* MAC OS X universal binaries */
#define MPR_CPU_SH4         12

/********************************* O/S Includes *******************************/

#if BLD_UNIX_LIKE && !VXWORKS && !MACOSX && !FREEBSD
    #include    <sys/types.h>
    #include    <time.h>
    #include    <arpa/inet.h>
    #include    <ctype.h>
    #include    <dirent.h>
    #include    <dlfcn.h>
    #include    <fcntl.h>
    #include    <grp.h> 
    #include    <errno.h>
    #include    <libgen.h>
    #include    <limits.h>
    #include    <netdb.h>
    #include    <net/if.h>
    #include    <netinet/in.h>
    #include    <netinet/tcp.h>
    #include    <netinet/ip.h>
    #include    <pthread.h> 
    #include    <pwd.h> 
    #include    <sys/poll.h>
    #include    <setjmp.h>
    #include    <signal.h>
    #include    <stdarg.h>
    #include    <stdio.h>
    #include    <stdlib.h>
    #include    <string.h>
    #include    <syslog.h>
    #include    <sys/ioctl.h>
    #include    <sys/mman.h>
    #include    <sys/stat.h>
    #include    <sys/param.h>
    #if !CYGWIN && !SOLARIS
        #include    <sys/prctl.h>
    #endif
    #include    <sys/resource.h>
    #include    <sys/sem.h>
    #include    <sys/shm.h>
    #include    <sys/socket.h>
    #include    <sys/select.h>
    #include    <sys/time.h>
    #include    <sys/times.h>
    #include    <sys/utsname.h>
    #include    <sys/uio.h>
    #include    <sys/un.h>
    #include    <sys/wait.h>
    #include    <unistd.h>
#if LINUX && !__UCLIBC__
    #include    <sys/sendfile.h>
#endif
#if CYGWIN || LINUX
    #include    <stdint.h>
#else
    #include    <netinet/in_systm.h>
#endif
#if BLD_FEATURE_FLOATING_POINT
    #define __USE_ISOC99 1
    #include    <math.h>
#if !CYGWIN
    #include    <resolv.h>
    #include    <values.h>
#endif
#endif
#endif /* BLD_UNIX_LIKE */


#if VXWORKS
    #include    <vxWorks.h>
    #include    <envLib.h>
    #include    <sys/types.h>
    #include    <time.h>
    #include    <arpa/inet.h>
    #include    <ctype.h>
    #include    <dirent.h>
    #include    <fcntl.h>
    #include    <errno.h>
    #include    <iosLib.h>
    #include    <limits.h>
    #include    <loadLib.h>
    #include    <netdb.h>
    #include    <net/if.h>
    #include    <netinet/tcp.h>
    #include    <netinet/in.h>
    #include    <netinet/ip.h>
    #include    <selectLib.h>
    #include    <setjmp.h>
    #include    <signal.h>
    #include    <stdarg.h>
    #include    <stdio.h>
    #include    <stdlib.h>
    #include    <string.h>
    #include    <symSyncLib.h>
    #include    <sysSymTbl.h>
    #include    <sys/fcntlcom.h>
    #include    <sys/ioctl.h>
    #include    <sys/stat.h>
    #include    <sys/socket.h>
    #include    <sys/times.h>
    #include    <unistd.h>
    #include    <unldLib.h>
    #if BLD_FEATURE_FLOATING_POINT
    #include    <float.h>
    #define __USE_ISOC99 1
    #include    <math.h>
    #endif
    #include    <sockLib.h>
    #include    <inetLib.h>
    #include    <ioLib.h>
    #include    <pipeDrv.h>
    #include    <hostLib.h>
    #include    <netdb.h>
    #include    <tickLib.h>
    #include    <taskHookLib.h>
#if _WRS_VXWORKS_MAJOR >= 6
    #include    <wait.h>
#endif
#endif /* VXWORKS */


#if MACOSX
    #include    <time.h>
    #include    <arpa/inet.h>
    #include    <ctype.h>
    #include    <dirent.h>
    #include    <dlfcn.h>
    #include    <fcntl.h>
    #include    <grp.h> 
    #include    <errno.h>
    #include    <libgen.h>
    #include    <limits.h>
    #include    <mach-o/dyld.h>
    #include    <netdb.h>
    #include    <net/if.h>
    #include    <netinet/in.h>
    #include    <netinet/tcp.h>
    #include    <sys/poll.h>
    #include    <pthread.h> 
    #include    <pwd.h> 
    #include    <resolv.h>
    #include    <setjmp.h>
    #include    <signal.h>
    #include    <stdarg.h>
    #include    <stdio.h>
    #include    <stdlib.h>
    #include    <stdint.h>
    #include    <string.h>
    #include    <syslog.h>
    #include    <sys/ioctl.h>
    #include    <sys/mman.h>
    #include    <sys/types.h>
    #include    <sys/stat.h>
    #include    <sys/param.h>
    #include    <sys/resource.h>
    #include    <sys/sem.h>
    #include    <sys/shm.h>
    #include    <sys/socket.h>
    #include    <sys/select.h>
    #include    <sys/sysctl.h>
    #include    <sys/time.h>
    #include    <sys/times.h>
    #include    <sys/types.h>
    #include    <sys/uio.h>
    #include    <sys/un.h>
    #include    <sys/utsname.h>
    #include    <sys/wait.h>
    #include    <unistd.h>
    #include    <libkern/OSAtomic.h>
    #if BLD_FEATURE_FLOATING_POINT
    #include    <float.h>
    #define __USE_ISOC99 1
    #include    <math.h>
    #endif
#endif /* MACOSX */


#if FREEBSD
    #include    <time.h>
    #include    <arpa/inet.h>
    #include    <ctype.h>
    #include    <dirent.h>
    #include    <dlfcn.h>
    #include    <fcntl.h>
    #include    <grp.h> 
    #include    <errno.h>
    #include    <libgen.h>
    #include    <limits.h>
    #include    <netdb.h>
    #include    <sys/socket.h>
    #include    <net/if.h>
    #include    <netinet/in_systm.h>
    #include    <netinet/in.h>
    #include    <netinet/tcp.h>
    #include    <netinet/ip.h>
    #include    <pthread.h> 
    #include    <pwd.h> 
    #include    <resolv.h>
    #include    <signal.h>
    #include    <stdarg.h>
    #include    <stdio.h>
    #include    <stdlib.h>
    #include    <stdint.h>
    #include    <string.h>
    #include    <syslog.h>
    #include    <sys/ioctl.h>
    #include    <sys/types.h>
    #include    <sys/stat.h>
    #include    <sys/param.h>
    #include    <sys/resource.h>
    #include    <sys/sem.h>
    #include    <sys/shm.h>
    #include    <sys/select.h>
    #include    <sys/time.h>
    #include    <sys/times.h>
    #include    <sys/types.h>
    #include    <sys/utsname.h>
    #include    <sys/wait.h>
    #include    <sys/mman.h>
    #include    <sys/sysctl.h>
    #include    <sys/un.h>
    #include    <sys/uio.h>
    #include    <unistd.h>
    #include    <poll.h>
#if BLD_FEATURE_FLOATING_POINT
    #include    <float.h>
    #define __USE_ISOC99 1
    #include    <math.h>
#endif
#endif /* FREEBSD */

#if WIN
    /*
     *  We replace insecure functions with Embedthis replacements
     */
    #define     _CRT_SECURE_NO_DEPRECATE 1

    /*
     *  Need this to get the the latest winsock APIs
     */
    #ifndef     _WIN32_WINNT
    #define     _WIN32_WINNT 0x501
    #endif

    #include    <winsock2.h>
    #include    <ws2tcpip.h>
    #include    <ctype.h>
    #include    <conio.h>
    #include    <direct.h>
    #include    <errno.h>
    #include    <fcntl.h>
    #include    <io.h>
    #include    <limits.h>
    #include    <malloc.h>
    #include    <process.h>
    #include    <sys/stat.h>
    #include    <sys/types.h>
    #include    <setjmp.h>
    #include    <stddef.h>
    #include    <stdio.h>
    #include    <stdlib.h>
    #include    <string.h>
    #include    <stdarg.h>
    #include    <time.h>
    #include    <windows.h>
    #if BLD_FEATURE_FLOATING_POINT
    #include    <math.h>
    #include    <float.h>
    #endif
    #include    <shlobj.h>
    #include    <shellapi.h>
    #include    <wincrypt.h>
    #if BLD_DEBUG
    #include    <crtdbg.h>
    #endif
    #undef     _WIN32_WINNT
#endif /* WIN */


#if WINCE
    #include    <ctype.h>
    #include    <malloc.h>
    #include    <stddef.h>
    #include    <stdio.h>
    #include    <stdlib.h>
    #include    <string.h>
    #include    <stdarg.h>
    #include    <time.h>
    #include    <winsock2.h>
    #include    <windows.h>
    #include    <winbase.h>
    #include    <winuser.h>
    #if BLD_FEATURE_FLOATING_POINT
    #include    <float.h>
    #endif
    #include    <shlobj.h>
    #include    <shellapi.h>
    #include    <wincrypt.h>
#endif /* WINCE */


#if BREW
    #if BLD_FEATURE_FLOATING_POINT
    #warning "Floating point is not supported on Brew"
    #endif
    #if BLD_FEATURE_MULTITHREAD
    #warning "Multithreading is not supported on Brew"
    #endif
    #include    <AEEModGen.h>
    #include    <AEEAppGen.h>
    #include    <BREWVersion.h>

    #if BREW_MAJ_VER == 2
        #ifdef __GNUC__
        #define __inline extern __inline__
        #endif
        #include    <AEENet.h>
        #undef __inline
    #endif

    #include    <AEE.h>
    #include    <AEEBitmap.h>
    #include    <AEEDisp.h>
    #include    <AEEFile.h>
    #include    <AEEHeap.h>
    #include    <AEEImageCtl.h>
    #include    <AEEMedia.h>
    #include    <AEEMediaUtil.h>
    #include    <AEEMimeTypes.h>
    #include    <AEEStdLib.h>
    #include    <AEEShell.h>
    #include    <AEESoundPlayer.h>
    #include    <AEEText.h>
    #include    <AEETransform.h>
    #include    <AEEWeb.h>
    #if BREW_MAJ_VER >= 3
    #include    <AEESMS.h>
    #endif
    #include    <AEETAPI.h>
#endif /* BREW */

/***************************** Cross Platform Defines *************************/
/*
 *  Word size and conversions between integer and pointer.
 */
#if __WORDSIZE == 64 || __amd64 || __x86_64 || __x86_64__
    #define MPR_64_BIT 1
    #define ITOP(i)         ((void*) ((int64) i))
    #define PTOI(i)         ((int) ((int64) i))
    #define LTOP(i)         ((void*) ((int64) i))
    #define PTOL(i)         ((int64) i)
#else
    #define MPR_64_BIT 0
    #define ITOP(i)         ((void*) ((int) i))
    #define PTOI(i)         ((int) i)
    #define LTOP(i)         ((void*) ((int) i))
    #define PTOL(i)         ((int64) (int) i)
#endif

/*
 *  Standard const types used by the MPR
 */
typedef unsigned char uchar;
typedef signed char schar;
typedef const char cchar;
typedef const unsigned char cuchar;
typedef const unsigned short cushort;
typedef const void cvoid;
typedef short uni;
typedef const uni cuni;

#ifdef __cplusplus
extern "C" {
#else
    #if !MACOSX
        typedef int bool;
    #endif
#endif

#if BLD_UNIX_LIKE
    typedef pthread_t   MprOsThread;
#elif BLD_CPU_ARCH == MPR_CPU_IX64
    typedef int64       MprOsThread;
#else
    typedef int         MprOsThread;
#endif

#ifndef BITSPERBYTE
#define BITSPERBYTE     (8 * sizeof(char))
#endif

#if !SOLARIS
#define BITS(type)      (BITSPERBYTE * (int) sizeof(type))
#endif

#ifndef MAXINT
#if INT_MAX
    #define MAXINT      INT_MAX
#else
    #define MAXINT      0x7fffffff
#endif
#endif
#ifndef MAXINT64
    #define MAXINT64    INT64(0x7fffffffffffffff)
#endif

/*
 *  Byte orderings
 */
#define MPR_LITTLE_ENDIAN   1
#define MPR_BIG_ENDIAN      2
/*
 *  Current endian ordering
 */
#define MPR_ENDIAN          BLD_ENDIAN

#ifndef max
    #define max(a,b)  (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
    #define min(a,b)  (((a) < (b)) ? (a) : (b))
#endif

#ifndef PRINTF_ATTRIBUTE
    #if (__GNUC__ >= 3) && !DOXYGEN && BLD_DEBUG
        /** 
         *  Use gcc attribute to check printf fns.  a1 is the 1-based index of the parameter containing the format, 
         *  and a2 the index of the first argument. Note that some gcc 2.x versions don't handle this properly 
         */     
        #define PRINTF_ATTRIBUTE(a1, a2) __attribute__ ((format (__printf__, a1, a2)))
    #else
        #define PRINTF_ATTRIBUTE(a1, a2)
    #endif
#endif

#define MPR_INLINE inline

/*
 *  Optimize expression evaluation code depending if the value is likely or not
 */
#undef likely
#undef unlikely
#if (__GNUC__ >= 3)
    #define likely(x)   __builtin_expect(!!(x), 1)
    #define unlikely(x) __builtin_expect(!!(x), 0)
#else
    #define likely(x)   (x)
    #define unlikely(x) (x)
#endif

#if !__UCLIBC__ && !CYGWIN && __USE_XOPEN2K
/*
    Disable because Linux spin locks run slower 
    #define BLD_HAS_SPINLOCK    1
 */
#define BLD_HAS_SPINLOCK    0
#endif

#if BLD_CC_DOUBLE_BRACES
    #define  VA_NULL    {{0}}
#else
    #define  VA_NULL    {0}
#endif

/******************************** Linux Defines *******************************/

#if CYGWIN || LINUX
    __extension__ typedef long long int int64;
    __extension__ typedef unsigned long long int uint64;

#if CYGWIN
    typedef unsigned long ulong;
#endif

    typedef off_t MprOffset;
    typedef intptr_t pint;

    #define INT64(x) (x##LL)
    #define UINT64(x) (x##ULL)

    #define closesocket(x)  close(x)
    #define MPR_BINARY      ""
    #define MPR_TEXT        ""
    #define SOCKET_ERROR    -1
    #define SET_SOCKOPT_CAST void*

#if BLD_FEATURE_FLOATING_POINT
    #define MAX_FLOAT       MAXFLOAT
    #define isNan(f) (f == FP_NAN)
#endif

#if CYGWIN
    #ifndef PTHREAD_MUTEX_RECURSIVE_NP
        #define PTHREAD_MUTEX_RECURSIVE_NP PTHREAD_MUTEX_RECURSIVE
    #endif
    #define __WALL          0
#else
    #define O_BINARY        0
    #define O_TEXT          0

    /*
     *  For some reason it is removed from fedora 6 pthreads.h and only comes in for UNIX96
     */
    extern int pthread_mutexattr_gettype (__const pthread_mutexattr_t *__restrict
        __attr, int *__restrict __kind) __THROW;
    /* 
     *  Set the mutex kind attribute in *ATTR to KIND (either PTHREAD_MUTEX_NORMAL,
     *  PTHREAD_MUTEX_RECURSIVE, PTHREAD_MUTEX_ERRORCHECK, or PTHREAD_MUTEX_DEFAULT).  
     */
    extern int pthread_mutexattr_settype (pthread_mutexattr_t *__attr, int __kind) __THROW;
    extern char **environ;
#endif

    #define LD_LIBRARY_PATH "LD_LIBRARY_PATH"

#endif  /* CYGWIN || LINUX  */

/******************************* VxWorks Defines ******************************/

#if VXWORKS
    typedef off_t MprOffset;
    typedef unsigned int uint;
    typedef unsigned long ulong;
    typedef long long int int64;
    typedef unsigned long long int uint64;
    typedef int pint;

    #define HAVE_SOCKLEN_T
    #define INT64(x) (x##LL)
    #define UINT64(x) (x##ULL)

    #define MPR_BINARY      ""
    #define MPR_TEXT        ""
    #define O_BINARY        0
    #define O_TEXT          0
    #define SOCKET_ERROR    -1
    #define MSG_NOSIGNAL    0
    #define __WALL          0

    #define SET_SOCKOPT_CAST char*
    #define closesocket(x) close(x)

#if BLD_FEATURE_FLOATING_POINT
    #define MAX_FLOAT       FLT_MAX
#endif

    #undef R_OK
    #define R_OK    4
    #undef W_OK
    #define W_OK    2
    #undef X_OK
    #define X_OK    1
    #undef F_OK
    #define F_OK    0

    extern int sysClkRateGet();

    #ifndef SHUT_RDWR
        #define SHUT_RDWR 2
    #endif

    #define getpid mprGetpid
    extern uint mprGetpid();

#if _WRS_VXWORKS_MAJOR < 6
    #define NI_MAXHOST      128
    extern STATUS access(cchar *path, int mode);
    typedef int     socklen_t;
    struct sockaddr_storage {
        char        pad[1024];
    };
#else
    /*
     *  This may or may not be necessary - let us know dev@embedthis.com if your system needs this (and why).
     */
    #if _DIAB_TOOL
        #if BLD_HOST_CPU_ARCH == MPR_CPU_PPC
            // #define __va_copy(dest, src) *(dest) = *(src)
            #define __va_copy(dest, src) memcpy((dest), (src), sizeof(va_list))
        #endif
    #endif
    #define HAVE_SOCKLEN_T
#endif
    #if _DIAB_TOOL
    #define inline __inline__
    #endif

extern int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif  /* VXWORKS */

/******************************** MacOsx Defines ******************************/

#if MACOSX
    typedef off_t MprOffset;
    typedef unsigned long ulong;

    __extension__ typedef long long int int64;
    __extension__ typedef unsigned long long int uint64;
    typedef intptr_t pint;

    #define INT64(x) (x##LL)
    #define UINT64(x) (x##ULL)

    #define closesocket(x)  close(x)
    #define MPR_BINARY      ""
    #define MPR_TEXT        ""
    #define O_BINARY        0
    #define O_TEXT          0
    #define SOCKET_ERROR    -1
    #define MSG_NOSIGNAL    0
    #define __WALL          0           /* 0x40000000 */
    #define SET_SOCKOPT_CAST void*
    #define PTHREAD_MUTEX_RECURSIVE_NP  PTHREAD_MUTEX_RECURSIVE

#if BLD_FEATURE_FLOATING_POINT
    #define MAX_FLOAT       MAXFLOAT
#endif
    
    /*
     *  Fix for MAC OS X - getenv
     */
    #if !HAVE_DECL_ENVIRON
    #ifdef __APPLE__
        #include <crt_externs.h>
        #define environ (*_NSGetEnviron())
    #else
        extern char **environ;
    #endif
    #endif
    #define LD_LIBRARY_PATH "DYLD_LIBRARY_PATH"
#endif /* MACOSX */

/*********************************** FREEBSD **************************************/

#if FREEBSD
    typedef off_t MprOffset;
    typedef unsigned long ulong;
    typedef intptr_t pint;

    __extension__ typedef long long int int64;
    __extension__ typedef unsigned long long int uint64;
    #define INT64(x) (x##LL)

    #define closesocket(x)  close(x)
    #define MPR_BINARY      ""
    #define MPR_TEXT        ""
    #define O_BINARY        0
    #define O_TEXT          0
    #define SOCKET_ERROR    -1
    #define MPR_DLL_EXT     ".dylib"
    #define __WALL          0
    #define PTHREAD_MUTEX_RECURSIVE_NP  PTHREAD_MUTEX_RECURSIVE

#if BLD_FEATURE_FLOATING_POINT
    #define MAX_FLOAT       MAXFLOAT
#endif

    #define CLD_EXITED 1
    #define CLD_KILLED 2
    #define LD_LIBRARY_PATH "LD_LIBRARY_PATH"

#endif /* FREEBSD */

/******************************* Windows Defines ******************************/
/*
 *  All windows like systems. Includes WINCE.
 */
#if BLD_WIN_LIKE
    typedef unsigned int uint;
    typedef unsigned long ulong;
    typedef unsigned short ushort;
    typedef __int64 int64;
    typedef unsigned __int64 uint64;
    typedef int64   MprOffset;
    typedef int     uid_t;
    typedef void    *handle;
    typedef char    *caddr_t;
    typedef long    pid_t;
    typedef int     gid_t;
    typedef ushort  mode_t;
    typedef void    *siginfo_t;
    typedef int     socklen_t;
    #if WINCE
        typedef int pint;
    #else
        typedef intptr_t pint;
    #endif

    struct timezone {
      int  tz_minuteswest;      /* minutes W of Greenwich */
      int  tz_dsttime;          /* type of dst correction */
    };

    #define HAVE_SOCKLEN_T
    #define INT64(x) (x##i64)
    #define UINT64(x) (x##Ui64)
    #define MSG_NOSIGNAL    0
    #define MPR_BINARY      "b"
    #define MPR_TEXT        "t"

    #if !WINCE
    #define access      _access
    #define chdir       _chdir
    #define close       _close
    #define fileno      _fileno
    #define fstat       _fstat
    #define getcwd      _getcwd
    #define getpid      _getpid
    #define gettimezone _gettimezone
    #define lseek       _lseek
    #define mkdir(a,b)  _mkdir(a)
    #define open        _open
    #define putenv      _putenv
    #define read        _read
    #define rmdir(a)    _rmdir(a)
    #define stat        _stat
    #define strdup      _strdup
    #define umask       _umask
    #define unlink      _unlink
    #define write       _write
    #endif
    
    #define MPR_TEXT    "t"

    #ifndef R_OK
    #define R_OK    4
    #endif
    #ifndef W_OK
    #define W_OK    2
    #endif
    #ifndef X_OK
    #define X_OK    4
    #endif
    #ifndef F_OK
    #define F_OK    0
    #endif

    #define EPERM           1
    #define ENOENT          2
    #define ESRCH           3
    #define EINTR           4
    #define EIO             5
    #define ENXIO           6
    #define E2BIG           7
    #define ENOEXEC         8
    #define EBADF           9
    #define ECHILD          10
    #define EAGAIN          11

    /*
     *  VS 2011 defines these
     */
    #ifndef EWOULDBLOCK
    #define EWOULDBLOCK     EAGAIN
    #define EINPROGRESS     36
    #define EALREADY        37
    #define ENETDOWN        43
    #define ECONNRESET      44
    #define ECONNREFUSED    45
    #define EADDRNOTAVAIL   49
    #define EISCONN         56
    #define EADDRINUSE      46
    #define ENETUNREACH     51
    #define ECONNABORTED    53
    #endif

    #define ENOMEM          12
    #define EACCES          13
    #define EFAULT          14
    #define EOSERR          15
    #define EBUSY           16
    #define EEXIST          17
    #define EXDEV           18
    #define ENODEV          19
    #define ENOTDIR         20
    #define EISDIR          21
    #define EINVAL          22
    #define ENFILE          23
    #define EMFILE          24
    #define ENOTTY          25
    #define EFBIG           27
    #define ENOSPC          28
    #define ESPIPE          29
    #define EROFS           30
    #define EMLINK          31
    #define EPIPE           32
    #define EDOM            33
    #define ERANGE          34

    #undef SHUT_RDWR
    #define SHUT_RDWR           2
    
#if BLD_FEATURE_FLOATING_POINT
    #define MAX_FLOAT       DBL_MAX
#endif
#ifndef FILE_FLAG_FIRST_PIPE_INSTANCE
    #define FILE_FLAG_FIRST_PIPE_INSTANCE   0x00080000
#endif

    #define SET_SOCKOPT_CAST cchar*
    #define inline __inline
    #define chmod _chmod
    #define isNan(f) (_isnan(f))

    /*
     *  PHP can't handle these
     */
    #if !BUILDING_PHP
        #define popen _popen
        #define pclose _pclose
    #endif

    /*
     *  When time began
     */
    #define TIME_GENESIS UINT64(11644473600000000)

    extern void     srand48(long);
    extern long     lrand48(void);
    extern long     ulimit(int, ...);
    extern long     nap(long);
    extern int      getuid(void);
    extern int      geteuid(void);

    extern int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif /* WIN_LIKE */

/******************************** Wince Defines *******************************/

#if WINCE

    typedef void FILE;
    typedef int off_t;

    struct stat {
        int     st_dev;
        int     st_ino;
        ushort  st_mode;
        short   st_nlink;
        short   st_uid;
        short   st_gid;
        int     st_rdev;
        long    st_size;
        time_t  st_atime;
        time_t  st_mtime;
        time_t  st_ctime;
    };

    #ifndef EOF
        #define EOF         -1
    #endif

    #define O_RDONLY        0
    #define O_WRONLY        1
    #define O_RDWR          2
    #define O_NDELAY        0x4
    #define O_NONBLOCK      0x4
    #define O_APPEND        0x8
    #define O_CREAT         0x100
    #define O_TRUNC         0x200
    #define O_TEXT          0x400
    #define O_EXCL          0x800
    #define O_BINARY        0x1000

    /*
     *  stat flags
     */
    #define S_IFMT          0170000 
    #define S_IFDIR         0040000
    #define S_IFCHR         0020000         /* character special */
    #define S_IFIFO         0010000
    #define S_IFREG         0100000
    #define S_IREAD         0000400
    #define S_IWRITE        0000200
    #define S_IEXEC         0000100

    #ifndef S_ISDIR
        #define S_ISDIR(X) (((X) & S_IFMT) == S_IFDIR)
    #endif
    #ifndef S_ISREG
        #define S_ISREG(X) (((X) & S_IFMT) == S_IFREG)
    #endif

    #define STARTF_USESHOWWINDOW 0
    #define STARTF_USESTDHANDLES 0

    /*
     *  Tunable parameters
     */
    #define     BUFSIZ      MPR_BUFSIZE
    #define     PATHSIZE    MPR_MAX_PATH
    #define gethostbyname2(a,b) gethostbyname(a)

    extern int access(cchar *filename, int flags);
    extern int chdir(cchar * dirname);
    extern int chmod(cchar *path, int mode);
    extern int close(int handle);
    extern void exit(int status);
    extern long _get_osfhandle(int handle);
    extern char *getcwd(char* buffer, int maxlen);
    extern char *getenv(cchar *charstuff);
    extern uint getpid();
    extern long lseek(int handle, long offset, int origin);
    extern int mkdir(cchar *dir, int mode);
    extern time_t mktime(struct tm *pt);
    extern int _open_osfhandle(int *handle, int flags);
    extern uint open(cchar *file, int mode,...);
    extern int read(int handle, void *buffer, uint count);
    extern int rename(cchar *from, cchar *to);
    extern int rmdir(cchar * dir);
    extern uint sleep(uint secs);
    extern int stat(cchar *path, struct stat *stat);
    extern char *strdup(char *s);
    extern int write(int handle, cvoid *buffer, uint count);
    extern int umask(int mode);
    extern int unlink(cchar *path);

    extern int errno;

    #undef CreateFile
    #define CreateFile CreateFileA
    WINBASEAPI HANDLE WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
        LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
        HANDLE hTemplateFile);

    #undef CreateProcess
    #define CreateProcess CreateProcessA

    #undef FindFirstFile
    #define FindFirstFile FindFirstFileA
    WINBASEAPI HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);

    #undef FindNextFile
    #define FindNextFile FindNextFileA
    WINBASEAPI BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);

    #undef GetModuleFileName
    #define GetModuleFileName GetModuleFileNameA
    WINBASEAPI DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);

    #undef GetModuleHandle
    #define GetModuleHandle GetModuleHandleA
    WINBASEAPI HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName);

    #undef GetProcAddress
    #define GetProcAddress GetProcAddressA
    // extern FARPROC GetProcAddressA(HMODULE module, LPCSTR name);

    #undef GetFileAttributes
    #define GetFileAttributes GetFileAttributesA
    extern DWORD GetFileAttributesA(cchar *path);

    extern void GetSystemTimeAsFileTime(FILETIME *ft);

    #undef LoadLibrary
    #define LoadLibrary LoadLibraryA
    HINSTANCE WINAPI LoadLibraryA(LPCSTR lpLibFileName);

    #define WSAGetLastError GetLastError

    #define _get_timezone getTimezone
    extern int getTimezone(int *secs);

    extern struct tm *localtime_r(const time_t *when, struct tm *tp);
    extern struct tm *gmtime_r(const time_t *t, struct tm *tp);

#endif /* WINCE */

/****************************** Solaris Defines *******************************/

#if SOLARIS
    typedef off_t MprOffset;
    typedef long long int int64;
    typedef unsigned long long int uint64;

    #define INT64(x) (x##LL)
    #define UINT64(x) (x##ULL)

    #define closesocket(x)  close(x)
    #define MPR_BINARY      ""
    #define MPR_TEXT        ""
    #define O_BINARY        0
    #define O_TEXT          0
    #define SOCKET_ERROR    -1
    #define MSG_NOSIGNAL    0
    #define INADDR_NONE     ((in_addr_t) 0xffffffff)
    #define __WALL  0
    #define PTHREAD_MUTEX_RECURSIVE_NP  PTHREAD_MUTEX_RECURSIVE
    #define LD_LIBRARY_PATH "LD_LIBRARY_PATH"
#if BLD_FEATURE_FLOATING_POINT
    #define MAX_FLOAT       MAXFLOAT
#endif

#endif /* SOLARIS */


/********************************* BREW Defines *******************************/

#if BREW
    typedef off_t MprOffset;
    typedef unsigned int uint;
    typedef unsigned long ulong;
    typedef unsigned short ushort;
    typedef uint    off_t;
    typedef long    pid_t;

    #ifndef _TIME_T_DEFINED
        typedef int64 time_t;
    #endif

    #define O_RDONLY        0
    #define O_WRONLY        1
    #define O_RDWR          2
    #define O_CREAT         0x200
    #define O_TRUNC         0x400
    #define O_BINARY        0
    #define O_TEXT          0x20000
    #define O_EXCL          0x40000
    #define O_APPEND        0x80000

    #define R_OK    4
    #define W_OK    2
    #define X_OK    1
    #define F_OK    0

    #define SEEK_SET    0
    #define SEEK_CUR    1
    #define SEEK_END    2

    #undef  printf
    #define printf DBGPRINTF

    extern int  getpid();
    extern int  isalnum(int c);
    extern int  isalpha(int c);
    extern int  isdigit(int c);
    extern int  islower(int c);
    extern int  isupper(int c);
    extern int  isspace(int c);
    extern int  isxdigit(int c);

    extern uint strlen(cchar *str);
    extern char *strstr(cchar *string, cchar *strSet);
    extern void *memset(cvoid *dest, int c, uint count);
    extern void exit(int status);
    extern char *strpbrk(cchar *str, cchar *set);
    extern uint strspn(cchar *str, cchar *set);
    extern int  tolower(int c);
    extern int  toupper(int c);
    extern void *memcpy(void *dest, cvoid *src, uint count);
    extern void *memmove(void *dest, cvoid *src, uint count);

    extern int  atoi(cchar *str);
    extern void free(void *ptr);
    extern void *malloc(uint size);
    extern void *realloc(void *ptr, uint size);
    extern char *strcat(char *dest, cchar *src);
    extern char *strchr(cchar *str, int c);
    extern int  strcmp(cchar *s1, cchar *s2);
    extern int  strncmp(cchar *s1, cchar *s2, uint count);
    extern char *strcpy(char *dest, cchar *src);
    extern char *strncpy(char *dest, cchar *src, uint count);
    extern char *strrchr(cchar *str, int c);

    #if BREWSIM && BLD_DEBUG
        extern _CRTIMP int __cdecl _CrtCheckMemory(void);
        extern _CRTIMP int __cdecl _CrtSetReportHook();
    #endif

#endif /* BREW */

#ifdef __cplusplus
}
#endif

#endif /* _h_MPR_OS_HDRS */

/*
 *  @copy   default
 *  
 *  Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
 *  Copyright (c) Michael O'Brien, 1993-2011. All Rights Reserved.
 *  
 *  This software is distributed under commercial and open source licenses.
 *  You may use the GPL open source license described below or you may acquire 
 *  a commercial license from Embedthis Software. You agree to be fully bound 
 *  by the terms of either license. Consult the LICENSE.TXT distributed with 
 *  this software for full details.
 *  
 *  This software is open source; you can redistribute it and/or modify it 
 *  under the terms of the GNU General Public License as published by the 
 *  Free Software Foundation; either version 2 of the License, or (at your 
 *  option) any later version. See the GNU General Public License for more 
 *  details at: http://www.embedthis.com/downloads/gplLicense.html
 *  
 *  This program is distributed WITHOUT ANY WARRANTY; without even the 
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 *  
 *  This GPL license does NOT permit incorporating this software into 
 *  proprietary programs. If you are unable to comply with the GPL, you must
 *  acquire a commercial license to use this software. Commercial licenses 
 *  for this software and support services are available from Embedthis 
 *  Software at http://www.embedthis.com 
 *  
 *  Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */
