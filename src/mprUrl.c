/**
 *  mprUrl.c - Url manipulation routines
 *
 *  Miscellaneous routines to parse and enscape URLs.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

/************************************ Locals **********************************/
/*
 *  Character escape/descape matching codes. Generated by charGen.
 */
static uchar charMatch[256] = {
     0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 7, 5, 6, 4, 7, 6, 7, 7, 2, 0, 4, 0, 0, 4,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 6, 7, 4, 7, 6,
     4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6, 6, 6, 0,
     2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6, 6, 6, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 
};

/*
 *  Basic mime type support
 */
static char *mimeTypes[] = {
    "ai", "application/postscript",
    "asc", "text/plain",
    "au", "audio/basic",
    "avi", "video/x-msvideo",
    "bin", "application/octet-stream",
    "bmp", "image/bmp",
    "class", "application/octet-stream",
    "css", "text/css",
    "dll", "application/octet-stream",
    "doc", "application/msword",
    "ejs", "text/html",
    "eps", "application/postscript",
    "es", "application/x-javascript",
    "exe", "application/octet-stream",
    "gif", "image/gif",
    "gz", "application/x-gzip",
    "htm", "text/html",
    "html", "text/html",
    "ico", "image/x-icon",
    "jar", "application/octet-stream",
    "jpeg", "image/jpeg",
    "jpg", "image/jpeg",
    "js", "application/javascript",
    "mp3", "audio/mpeg",
    "pdf", "application/pdf",
    "png", "image/png",
    "ppt", "application/vnd.ms-powerpoint",
    "ps", "application/postscript",
    "ra", "audio/x-realaudio",
    "ram", "audio/x-pn-realaudio",
    "rmm", "audio/x-pn-realaudio",
    "rtf", "text/rtf",
    "rv", "video/vnd.rn-realvideo",
    "so", "application/octet-stream",
    "swf", "application/x-shockwave-flash",
    "tar", "application/x-tar",
    "tgz", "application/x-gzip",
    "tiff", "image/tiff",
    "txt", "text/plain",
    "wav", "audio/x-wav",
    "xls", "application/vnd.ms-excel",
    "zip", "application/zip",
    "php", "application/x-appweb-php",
    "pl", "application/x-appweb-perl",
    "py", "application/x-appweb-python",
    NULL, NULL,
};

/*
 *  Max size of the port specification in a URL
 */
#define MAX_PORT_LEN 8

/************************************ Code ************************************/
/*
 *  Parse a complete URI. This accepts full URIs with schemes (http:) and partial URLs
 */
MprUri *mprParseUri(MprCtx ctx, cchar *uri)
{
    MprUri  *up;
    char    *tok, *cp, *last_delim, *hostbuf, *urlTok;
    int     c, len, ulen, http;

    mprAssert(uri);

    up = mprAllocObj(ctx, MprUri);
    if (up == 0) {
        return 0;
    }

    /*
     *  Allocate a single buffer to hold all the cracked fields.
     */
    ulen = (int) strlen(uri);
    len = ulen * 2 + 3;

    up->originalUri = mprStrdup(up, uri);
    up->parsedUriBuf = (char*) mprAlloc(up, len * (int) sizeof(char));

    hostbuf = &up->parsedUriBuf[ulen+1];
    strcpy(up->parsedUriBuf, uri);
    urlTok = up->parsedUriBuf;

    /*
     *  Defaults for missing URL fields
     */
    up->url = "/";
    up->scheme = "http";
    up->host = "localhost";
    up->port = 80;
    up->query = 0;
    up->ext = 0;
    up->secure = 0;
    up->reference = 0;

    http = 0;
    tok = 0;

    if (strncmp(urlTok, "https://", 8) == 0) {
        up->secure = 1;
        up->port = 443;
        tok = &urlTok[8];
        http++;

    } else if (strncmp(urlTok, "http://", 7) == 0) {
        tok = &urlTok[7];
        http++;
    }

    if (http) {
        up->scheme = urlTok;
        up->host = tok;
        tok[-3] = '\0';
        for (cp = tok; *cp; cp++) {
            if (*cp == '/') {
                break;
            }
            if (*cp == ':') {
                *cp++ = '\0';
                up->port = atoi(cp);
                tok = cp;
            }
        }
        if ((cp = strchr(tok, '/')) != NULL) {
            c = *cp;
            *cp = '\0';
            mprStrcpy(hostbuf, ulen + 1, up->host);
            *cp = c;
            up->host = hostbuf;
            up->url = cp;
            tok = cp;
        }

    } else {
        up->url = urlTok;
        tok = urlTok;
    }

    if ((cp = strchr(tok, '#')) != NULL) {
        *cp++ = '\0';
        up->reference = cp;
        tok = cp;
    }
    if ((cp = strchr(tok, '?')) != NULL) {
        *cp++ = '\0';
        up->query = cp;
        tok = up->query;
    }

    if ((cp = strrchr(up->url, '.')) != NULL) {
        if ((last_delim = strrchr(up->url, '/')) != NULL) {
            if (last_delim <= cp) {
                up->ext = cp + 1;
#if UNUSED && BLD_WIN_LIKE
                mprStrLower(up->ext);
#endif
            }
        } else {
            up->ext = cp + 1;
#if UNUSED && BLD_WIN_LIKE
            mprStrLower(up->ext);
#endif
        }
    } else {
        len = (int) strlen(up->url);
    }

    return up;
}


/*
 *  Format a fully qualified URI
 */
char *mprFormatUri(MprCtx ctx, cchar *scheme, cchar *host, int port, cchar *path, cchar *query)
{
    char    portBuf[16], *uri;
    cchar   *portDelim, *pathDelim, *queryDelim;
    int     defaultPort, len;

    len = 0;

    if (scheme == 0 || *scheme == '\0') {
        scheme = "http";
    }
    len += (int) strlen(scheme) + 3;                            /* Add 3 for "://" */

    defaultPort = (strcmp(scheme, "http") == 0) ? 80 : 443;

    if (host == 0 || *host == '\0') {
        host = "localhost";
    }

    /*
     *  Hosts with integral port specifiers override
     */
    if (strchr(host, ':')) {
        portDelim = 0;
    } else {
        if (port != defaultPort) {
            mprItoa(portBuf, sizeof(portBuf), port, 10);
            portDelim = ":";
        } else {
            portBuf[0] = '\0';
            portDelim = "";
        }
        len += (int) strlen(portBuf) + (int) strlen(portDelim);
    }
    len += (int) strlen(host);

    if (path) {
        pathDelim = (*path == '/') ? "" :  "/";
    } else {
        pathDelim = "/";
        path = "";
    }
    len += (int) strlen(path) + (int) strlen(pathDelim);

    if (query && *query) {
        queryDelim = "?";
    } else {
        queryDelim = query = "";
    }
    len += (int) strlen(query) + (int) strlen(queryDelim);
    len += 1;                                               /* Add one for the null */

    uri = mprAlloc(ctx, len);
    if (uri == 0) {
        return 0;
    }

    if (portDelim) {
        uri = mprAsprintf(ctx, len, "%s://%s%s%s%s%s%s%s", scheme, host, portDelim, portBuf, pathDelim, 
            path, queryDelim, query);
    } else {
        uri = mprAsprintf(ctx, len, "%s://%s%s%s%s%s", scheme, host, pathDelim, path, queryDelim, query);
    }
    if (uri == 0) {
        return 0;
    }
    return uri;
}


/*
 *  Url encode by encoding special characters with hex equivalents.
 */
char *mprUrlEncode(MprCtx ctx, cchar *inbuf)
{
    static cchar    hexTable[] = "0123456789abcdef";
    uchar           c;
    cchar           *ip;
    char            *result, *op;
    int             len;

    mprAssert(inbuf);
    mprAssert(inbuf);

    for (len = 1, ip = inbuf; *ip; ip++, len++) {
        if (charMatch[(int) *ip] & MPR_HTTP_ESCAPE_URL) {
            len += 2;
        }
    }

    if ((result = mprAlloc(ctx, len)) == 0) {
        return 0;
    }

    ip = inbuf;
    op = result;

    while ((c = (uchar) (*inbuf++)) != 0) {
        if (c == ' ') {
            *op++ = '+';
        } else if (charMatch[c] & MPR_HTTP_ESCAPE_URL) {
            *op++ = '%';
            *op++ = hexTable[c >> 4];
            *op++ = hexTable[c & 0xf];
        } else {
            *op++ = c;
        }
    }
    mprAssert(op < &result[len]);
    *op = '\0';
    return result;
}


/*
 *  Decode a string using URL encoding.
 */
char *mprUrlDecode(MprCtx ctx, cchar *inbuf)
{
    cchar   *ip;
    char    *result, *op;
    int     num, i, c;

    mprAssert(inbuf);

    if ((result = mprStrdup(ctx, inbuf)) == 0) {
        return 0;
    }

    for (op = result, ip = inbuf; *ip; ip++, op++) {
        if (*ip == '+') {
            *op = ' ';

        } else if (*ip == '%' && isxdigit((int) ip[1]) && isxdigit((int) ip[2])) {
            ip++;
            num = 0;
            for (i = 0; i < 2; i++, ip++) {
                c = tolower((int) *ip);
                if (c >= 'a' && c <= 'f') {
                    num = (num * 16) + 10 + c - 'a';
                } else if (c >= '0' && c <= '9') {
                    num = (num * 16) + c - '0';
                } else {
                    /* Bad chars in URL */
                    return 0;
                }
            }
            *op = (char) num;
            ip--;

        } else {
            *op = *ip;
        }
    }
    *op = '\0';
    return result;
}


/*
 *  Escape a shell command
 */
char *mprEscapeCmd(MprCtx ctx, cchar *cmd, int escChar)
{
    uchar   c;
    cchar   *ip;
    char    *result, *op;
    int     len;

    mprAssert(cmd);

    for (len = 1, ip = cmd; *ip; ip++, len++) {
        if (charMatch[(int) *ip] & MPR_HTTP_ESCAPE_SHELL) {
            len++;
        }
    }
    if ((result = mprAlloc(ctx, len)) == 0) {
        return 0;
    }

    if (escChar == 0) {
        escChar = '\\';
    }
    op = result;
    while ((c = (uchar) *cmd++) != 0) {
#if BLD_WIN_LIKE
        if ((c == '\r' || c == '\n') && *cmd != '\0') {
            c = ' ';
            continue;
        }
#endif
        if (charMatch[c] & MPR_HTTP_ESCAPE_SHELL) {
            *op++ = escChar;
        }
        *op++ = c;
    }
    mprAssert(op < &result[len]);
    *op = '\0';
    return result;
}


/*
 *  Escape HTML to escape defined characters (prevent cross-site scripting)
 */
char *mprEscapeHtml(MprCtx ctx, cchar *html)
{
    cchar   *ip;
    char    *result, *op;
    int     len;

    for (len = 1, ip = html; *ip; ip++, len++) {
        if (charMatch[(int) *ip] & MPR_HTTP_ESCAPE_HTML) {
            len += 5;
        }
    }
    if ((result = mprAlloc(ctx, len)) == 0) {
        return 0;
    }

    /*
     *  Leave room for the biggest expansion
     */
    op = result;
    while (*html != '\0') {
        if (charMatch[(uchar) *html] & MPR_HTTP_ESCAPE_HTML) {
            if (*html == '&') {
                strcpy(op, "&amp;");
                op += 5;
            } else if (*html == '<') {
                strcpy(op, "&lt;");
                op += 4;
            } else if (*html == '>') {
                strcpy(op, "&gt;");
                op += 4;
            } else if (*html == '#') {
                strcpy(op, "&#35;");
                op += 5;
            } else if (*html == '(') {
                strcpy(op, "&#40;");
                op += 5;
            } else if (*html == ')') {
                strcpy(op, "&#41;");
                op += 5;
            } else if (*html == '"') {
                strcpy(op, "&quot;");
                op += 6;
            } else {
                mprAssert(0);
            }
            html++;
        } else {
            *op++ = *html++;
        }
    }
    mprAssert(op < &result[len]);
    *op = '\0';
    return result;
}


/*
 *  Validate a Url
 *
 *  WARNING: this code will not fully validate against certain Windows 95/98/Me bugs. Don't use this code in these
 *  operating systems without modifying this code to remove "con", "nul", "aux", "clock$" and "config$" in either
 *  case from the URI. The MprFileSystem::stat() will perform these checks to determine if a file is a device file.
 */
char *mprValidateUrl(MprCtx ctx, char *url)
{
    char    *sp, *dp, *xp, *dot;

    if ((url = mprStrdup(ctx, url)) == 0) {
        return 0;
    }

    /*
     *  Remove multiple path separators and map '\\' to '/' for windows
     */
    sp = dp = url;
    while (*sp) {
#if BLD_WIN_LIKE
        if (*sp == '\\') {
            *sp = '/';
        }
#endif
        if (sp[0] == '/' && sp[1] == '/') {
            sp++;
        } else {
            *dp++ = *sp++;
        }
    }
    *dp = '\0';

    dot = strchr(url, '.');
    if (dot == 0) {
        return url;
    }

    /*
     *  Per RFC 1808, remove "./" segments
     */
    dp = dot;
    for (sp = dot; *sp; ) {
        if (*sp == '.' && sp[1] == '/' && (sp == url || sp[-1] == '/')) {
            sp += 2;
        } else {
            *dp++ = *sp++;
        }
    }
    *dp = '\0';

    /*
     *  Remove trailing "."
     */
    if ((dp == &url[1] && url[0] == '.') ||
        (dp > &url[1] && dp[-1] == '.' && dp[-2] == '/')) {
        *--dp = '\0';
    }

    /*
     *  Remove "../"
     */
    for (sp = dot; *sp; ) {
        if (*sp == '.' && sp[1] == '.' && sp[2] == '/' && (sp == url || sp[-1] == '/')) {
            xp = sp + 3;
            sp -= 2;
            if (sp < url) {
                sp = url;
            } else {
                while (sp >= url && *sp != '/') {
                    sp--;
                }
                sp++;
            }
            dp = sp;
            while ((*dp++ = *xp) != 0) {
                xp++;
            }
        } else {
            sp++;
        }
    }
    *dp = '\0';

    /*
     *  Remove trailing "/.."
     */
    if (sp == &url[2] && *url == '.' && url[1] == '.') {
        *url = '\0';
    } else {
        if (sp > &url[2] && sp[-1] == '.' && sp[-2] == '.' && sp[-3] == '/') {
            sp -= 4;
            if (sp < url) {
                sp = url;
            } else {
                while (sp >= url && *sp != '/') {
                    sp--;
                }
                sp++;
            }
            *sp = '\0';
        }
    }
#if BLD_WIN_LIKE
    if (*url != '\0') {
        char    *cp;
        /*
         *  There was some extra URI past the matching alias prefix portion.  Windows will ignore trailing "."
         *  and " ". We must reject here as the URL probably won't match due to the trailing character and the
         *  copyHandler will return the unprocessed content to the user. Bad.
         */
        cp = &url[strlen(url) - 1];
        while (cp >= url) {
            if (*cp == '.' || *cp == ' ') {
                *cp-- = '\0';
            } else {
                break;
            }
        }
    }
#endif
    return url;
}


cchar *mprLookupMimeType(MprCtx ctx, cchar *ext)
{
    Mpr     *mpr;
    char    **cp;
    cchar   *ep, *mtype;

    mprAssert(ext);

    mpr = mprGetMpr(ctx);
    if (mpr->mimeTypes == 0) {
        mpr->mimeTypes = mprCreateHash(mpr, 67);
        for (cp = mimeTypes; cp[0]; cp += 2) {
            mprAddHash(mpr->mimeTypes, cp[0], cp[1]);
        }
    }
    if ((ep = strrchr(ext, '.')) != 0) {
        ext = &ep[1];
    }
    mtype = (cchar*) mprLookupHash(mpr->mimeTypes, ext);
    if (mtype == 0) {
        return "application/octet-stream";
    }
    return mtype;
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
    vim: sw=4 ts=4 expandtab

    @end
 */
