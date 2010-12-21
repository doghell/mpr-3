/*
 *  charGen.c - Generate the character lookup tables.
 *
 *  These are used in mprUrl.c  for escape / descape routines.
 *
 *  Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

/*********************************** Code *************************************/

int main(int argc, char *argv[])
{
    Mpr     *mpr;
    uchar    flags;
    uint     c;

    mpr = mprCreate(argc, argv, 0);

    mprPrintf(mpr, "static uchar charMatch[256] = {\n\t 0,");

    for (c = 1; c < 256; ++c) {
        flags = 0;
        if (c % 16 == 0)
            mprPrintf(mpr, "\n\t");
#if BLD_WIN_LIKE
        if (strchr("&;`'\"|*?~<>^()[]{}$\\\n\r%", c)) {
            flags |= MPR_HTTP_ESCAPE_SHELL;
        }
#else
        if (strchr("&;`\'\"|*?~<>^()[]{}$\\\n", c)) {
            flags |= MPR_HTTP_ESCAPE_SHELL;
        }
#endif
        /*
         *  RFC 1738 Reserved and unsafe chars in URLs are: 
         *      0x00-0x1F, 0x7F, 0x80-0xFF, space, <>"#%{}|\^~[]`
         *  Reserved chars in the scheme (protocol) portion of a URL are:
         *      ;/?: @=&
         *  For extra security, we also encode: 
         *      ' \t, \r, \n
         */
        if (c <= 0x1f || c >= 0x7f || isspace(c) || strchr(" !\"#$%\'&(),/:;<=>?@[\\]^{|}~", c) != 0) {
            flags |= MPR_HTTP_ESCAPE_URL;
        }
        if (strchr("<>&()#\"", c) != 0) {
            flags |= MPR_HTTP_ESCAPE_HTML;
        }
        mprPrintf(mpr, "%2u%c", flags, (c < 255) ? ',' : ' ');

    }
    mprPrintf(mpr, "\n};\n");

    return 0;
}



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
    vim: sw=8 ts=8 expandtab

    @end
 */
