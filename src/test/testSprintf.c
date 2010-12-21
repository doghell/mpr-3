/**
 *  testSprintf.c - Unit tests for the Sprintf class
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/
/*
 *  We support some non-standard PRINTF args
 */
#define PRINTF_ATTRIBUTE(a,b)

#include    "mprTest.h"

/************************************ Code ************************************/

static void testBasicSprintf(MprTestGroup *gp)
{
    char    buf[256];
    char    *str;
    int     count;

    mprSprintf(buf, sizeof(buf), "%d", 12345678);
    assert(strlen(buf) == 8);
    assert(strcmp(buf, "12345678") == 0);

    mprSprintf(buf, sizeof(buf), "%d", -12345678);
    assert(strlen(buf) == 9);
    assert(strcmp(buf, "-12345678") == 0);

    str = mprAsprintf(gp, 4, "%d", 12345678);
    count = strlen(str);
    assert(count == 3);
    assert(strcmp(str, "123") == 0);
    mprFree(str);
}


static void testItoa(MprTestGroup *gp)
{
    char    buf[256];

    mprItoa(buf, sizeof(buf), 0, 10);
    assert(strcmp(buf, "0") == 0);

    mprItoa(buf, sizeof(buf), 1, 10);
    assert(strcmp(buf, "1") == 0);

    mprItoa(buf, sizeof(buf), -1, 10);
    assert(strcmp(buf, "-1") == 0);

    mprItoa(buf, sizeof(buf), 12345678, 10);
    assert(strcmp(buf, "12345678") == 0);

    mprItoa(buf, sizeof(buf), -12345678, 10);
    assert(strcmp(buf, "-12345678") == 0);

    mprItoa(buf, sizeof(buf), 0x1234, 16);
    assert(strcmp(buf, "1234") == 0);
}


/*
 *  We need to test quite a bit here. The general format of a sprintf spec is:
 *
 *      %[modifier][width][precision][bits][type]
 *
 *  The various character classes are:
 *      CLASS       Characters      Description
 *      NORMAL      [All other]     Normal characters
 *      PERCENT     [%]             Begin format
 *      MODIFIER    [-+ #,]         Modifiers
 *      ZERO        [0]             Special modifier
 *      STAR        [*]             Width supplied by arg
 *      DIGIT       [1-9]           Field widths
 *      DOT         [.]             Introduce precision
 *      BITS        [hlL]           Length bits
 *      TYPE        [cdfinopsSuxX]  Type specifiers
 */
static void testTypeOptions(MprTestGroup *gp)
{
    char    buf[256];
    int     count;

    mprSprintf(buf, sizeof(buf), "Hello %c World", 'X');
    assert(strcmp(buf, "Hello X World") == 0);

    mprSprintf(buf, sizeof(buf), "%d", 12345678);
    assert(strcmp(buf, "12345678") == 0);

#if BLD_FEATURE_FLOATING_POINT
    mprSprintf(buf, sizeof(buf), "%3.2f", 1.77);
    assert(strcmp(buf, "1.77") == 0);
#endif

    mprSprintf(buf, sizeof(buf), "%i", 12345678);
    assert(strcmp(buf, "12345678") == 0);

    mprSprintf(buf, sizeof(buf), "%s%n", "Hello World", &count);
    assert(count == 11);

    mprSprintf(buf, sizeof(buf), "%o", 077);
    assert(strcmp(buf, "77") == 0);

    mprSprintf(buf, sizeof(buf), "%p", (void*) 0xdeadbeef);
    assert(strcmp(buf, "0xdeadbeef") == 0);

    mprSprintf(buf, sizeof(buf), "%s", "Hello World");
    assert(strcmp(buf, "Hello World") == 0);

    mprSprintf(buf, sizeof(buf), "%S", "Hello World");
    assert(strcmp(buf, "Hello World") == 0);

    mprSprintf(buf, sizeof(buf), "%u", 0xffffffff);
    assert(strcmp(buf, "4294967295") == 0);

    mprSprintf(buf, sizeof(buf), "%x", 0xffffffff);
    assert(strcmp(buf, "ffffffff") == 0);

    mprSprintf(buf, sizeof(buf), "%X", 0xffffffff);
    assert(strcmp(buf, "FFFFFFFF") == 0);
}


static void testModifierOptions(MprTestGroup *gp)
{
    char    buf[256];

    mprSprintf(buf, sizeof(buf), "%-4d", 23);
    assert(strcmp(buf, "23  ") == 0);
    mprSprintf(buf, sizeof(buf), "%-4d", -23);
    assert(strcmp(buf, "-23 ") == 0);

    mprSprintf(buf, sizeof(buf), "%+4d", 23);
    assert(strcmp(buf, " +23") == 0);
    mprSprintf(buf, sizeof(buf), "%+4d", -23);
    assert(strcmp(buf, " -23") == 0);

    mprSprintf(buf, sizeof(buf), "% 4d", 23);
    assert(strcmp(buf, "  23") == 0);
    mprSprintf(buf, sizeof(buf), "% 4d", -23);
    assert(strcmp(buf, " -23") == 0);

    mprSprintf(buf, sizeof(buf), "%-+4d", 23);
    assert(strcmp(buf, "+23 ") == 0);
    mprSprintf(buf, sizeof(buf), "%-+4d", -23);
    assert(strcmp(buf, "-23 ") == 0);
    mprSprintf(buf, sizeof(buf), "%- 4d", 23);
    assert(strcmp(buf, " 23 ") == 0);

    mprSprintf(buf, sizeof(buf), "%#6x", 0x23);
    assert(strcmp(buf, "  0x23") == 0);

    mprSprintf(buf, sizeof(buf), "%,d", 12345678);
    assert(strcmp(buf, "12,345,678") == 0);
}


static void testWidthOptions(MprTestGroup *gp)
{
    char    buf[256];

    mprSprintf(buf, sizeof(buf), "%2d", 1234);
    assert(strcmp(buf, "1234") == 0);

    mprSprintf(buf, sizeof(buf), "%8d", 1234);
    assert(strcmp(buf, "    1234") == 0);

    mprSprintf(buf, sizeof(buf), "%-8d", 1234);
    assert(strcmp(buf, "1234    ") == 0);

    mprSprintf(buf, sizeof(buf), "%*d", 8, 1234);
    assert(strcmp(buf, "    1234") == 0);

    mprSprintf(buf, sizeof(buf), "%*d", -8, 1234);
    assert(strcmp(buf, "1234    ") == 0);
}


static void testPrecisionOptions(MprTestGroup *gp)
{
    char    buf[256];

    mprSprintf(buf, sizeof(buf), "%.2d", 1234);
    assert(strcmp(buf, "1234") == 0);

    mprSprintf(buf, sizeof(buf), "%.8d", 1234);
    assert(strcmp(buf, "00001234") == 0);

    mprSprintf(buf, sizeof(buf), "%8.6d", 1234);
    assert(strcmp(buf, "  001234") == 0);

    mprSprintf(buf, sizeof(buf), "%6.3d", 12345);
    assert(strcmp(buf, " 12345") == 0);

    mprSprintf(buf, sizeof(buf), "%6.3s", "ABCDEFGHIJ");
    assert(strcmp(buf, "   ABC") == 0);

#if BLD_FEATURE_FLOATING_POINT
    mprSprintf(buf, sizeof(buf), "%6.2f", 12.789);
    assert(strcmp(buf, " 12.79") == 0);

    mprSprintf(buf, sizeof(buf), "%8.2f", 1234.789);
    assert(strcmp(buf, " 1234.79") == 0);
#endif
}


static void testBitOptions(MprTestGroup *gp)
{
    char    buf[256];

    mprSprintf(buf, sizeof(buf), "%hd %hd", (short) 23, (short) 78);
    assert(strcmp(buf, "23 78") == 0);

    mprSprintf(buf, sizeof(buf), "%ld %ld", (long) 12, (long) 89);
    assert(strcmp(buf, "12 89") == 0);

    mprSprintf(buf, sizeof(buf), "%Ld %Ld", (int64) 66, (int64) 41);
    assert(strcmp(buf, "66 41") == 0);

    mprSprintf(buf, sizeof(buf), "%hd %Ld %hd %Ld", 
        (short) 123, (int64) 789, (short) 441, (int64) 558);
    assert(strcmp(buf, "123 789 441 558") == 0);
}


static void testSprintf64(MprTestGroup *gp)
{
    char    buf[256];

    mprSprintf(buf, sizeof(buf), "%Ld", INT64(9012345678));
    assert(strlen(buf) == 10);
    assert(strcmp(buf, "9012345678") == 0);

    mprSprintf(buf, sizeof(buf), "%Ld", INT64(-9012345678));
    assert(strlen(buf) == 11);
    assert(strcmp(buf, "-9012345678") == 0);
}


#if BLD_FEATURE_FLOATING_POINT
static void testFloatingSprintf(MprTestGroup *gp)
{
}
#endif


MprTestDef testSprintf = {
    "sprintf", 0, 0, 0,
    {
        MPR_TEST(0, testBasicSprintf),
        MPR_TEST(0, testItoa),
        MPR_TEST(0, testTypeOptions),
        MPR_TEST(0, testModifierOptions),
        MPR_TEST(0, testWidthOptions),
        MPR_TEST(0, testPrecisionOptions),
        MPR_TEST(0, testBitOptions),
        MPR_TEST(0, testSprintf64),
#if BLD_FEATURE_FLOATING_POINT
        MPR_TEST(0, testFloatingSprintf),
#endif
        MPR_TEST(0, 0),
    },
};

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
