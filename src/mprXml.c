/**
 *  mprXml.c - A simple SAX style XML parser
 *
 *  This is a recursive descent parser for XML text files. It is a one-pass simple parser that invokes a user 
 *  supplied callback for key tokens in the XML file. The user supplies a read function so that XML files can 
 *  be parsed from disk or in-memory. 
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/****************************** Forward Declarations **************************/
#if BLD_FEATURE_XML

static int       parseNext(MprXml *xp, int state);
static MprXmlToken getToken(MprXml *xp, int state);
static int       getNextChar(MprXml *xp);
static int       scanFor(MprXml *xp, char *str);
static int       putLastChar(MprXml *xp, int c);
static void      xmlError(MprXml *xp, char *fmt, ...);
static void      trimToken(MprXml *xp);

/************************************ Code ************************************/

MprXml *mprXmlOpen(MprCtx ctx, int initialSize, int maxSize)
{
    MprXml  *xp;

    xp = mprAllocObjZeroed(ctx, MprXml);
    
    xp->inBuf = mprCreateBuf(xp, MPR_XML_BUFSIZE, MPR_XML_BUFSIZE);
    xp->tokBuf = mprCreateBuf(xp, initialSize, maxSize);

    return xp;
}


void mprXmlSetParserHandler(MprXml *xp, MprXmlHandler h)
{
    mprAssert(xp);

    xp->handler = h;
}


void mprXmlSetInputStream(MprXml *xp, MprXmlInputStream s, void *arg)
{
    mprAssert(xp);

    xp->readFn = s;
    xp->inputArg = arg;
}


/*
 *  Set the parse arg
 */ 
void mprXmlSetParseArg(MprXml *xp, void *parseArg)
{
    mprAssert(xp);

    xp->parseArg = parseArg;
}


/*
 *  Set the parse arg
 */ 
void *mprXmlGetParseArg(MprXml *xp)
{
    mprAssert(xp);

    return xp->parseArg;
}


/*
 *  Parse an XML file. Return 0 for success, -1 for error.
 */ 
int mprXmlParse(MprXml *xp)
{
    mprAssert(xp);

    return parseNext(xp, MPR_XML_BEGIN);
}


/*
 *  XML recursive descent parser. Return -1 for errors, 0 for EOF and 1 if there is still more data to parse.
 */
static int parseNext(MprXml *xp, int state)
{
    MprXmlHandler   handler;
    MprXmlToken     token;
    MprBuf          *tokBuf;
    char            *tname, *aname;
    int             rc;

    mprAssert(state >= 0);

    tokBuf = xp->tokBuf;
    handler = xp->handler;
    tname = aname = 0;
    rc = 0;
    
    /*
     *  In this parse loop, the state is never assigned EOF or ERR. In such cases we always return EOF or ERR.
     */
    while (1) {

        token = getToken(xp, state);

        if (token == MPR_XMLTOK_TOO_BIG) {
            xmlError(xp, "XML token is too big");
            goto err;
        }

        switch (state) {
        case MPR_XML_BEGIN:     /* ------------------------------------------ */
            /*
             *  Expect to get an element, comment or processing instruction 
             */
            switch (token) {
            case MPR_XMLTOK_EOF:
                goto exit;

            case MPR_XMLTOK_LS:
                /*
                 *  Recurse to handle the new element, comment etc.
                 */
                rc = parseNext(xp, MPR_XML_AFTER_LS);
                if (rc < 0) {
                    goto exit;
                }
                break;

            default:
                xmlError(xp, "Syntax error");
                goto err;
            }
            break;

        case MPR_XML_AFTER_LS: /* ------------------------------------------ */
            switch (token) {
            case MPR_XMLTOK_COMMENT:
                state = MPR_XML_COMMENT;
                rc = (*handler)(xp, state, "!--", 0, mprGetBufStart(tokBuf));
                if (rc < 0) {
                    goto err;
                }
                rc = 1;
                goto exit;

            case MPR_XMLTOK_CDATA:
                state = MPR_XML_CDATA;
                rc = (*handler)(xp, state, "!--", 0, mprGetBufStart(tokBuf));
                if (rc < 0) {
                    goto err;
                }
                rc = 1;
                goto exit;

            case MPR_XMLTOK_INSTRUCTIONS:
                /* Just ignore processing instructions */
                rc = 1;
                goto exit;

            case MPR_XMLTOK_TEXT:
                state = MPR_XML_NEW_ELT;
                tname = mprStrdup(xp, mprGetBufStart(tokBuf));
                if (tname == 0) {
                    rc = MPR_ERR_NO_MEMORY;
                    goto exit;
                }
                rc = (*handler)(xp, state, tname, 0, 0);
                if (rc < 0) {
                    goto err;
                }
                break;

            default:
                xmlError(xp, "Syntax error");
                goto err;
            }
            break;

        case MPR_XML_NEW_ELT:   /* ------------------------------------------ */
            /*
             *  We have seen the opening "<element" for a new element and have not yet seen the terminating 
             *  ">" of the opening element.
             */
            switch (token) {
            case MPR_XMLTOK_TEXT:
                /*
                 *  Must be an attribute name
                 */
                aname = mprStrdup(xp, mprGetBufStart(tokBuf));
                token = getToken(xp, state);
                if (token != MPR_XMLTOK_EQ) {
                    xmlError(xp, "Missing assignment for attribute \"%s\"", aname);
                    goto err;
                }

                token = getToken(xp, state);
                if (token != MPR_XMLTOK_TEXT) {
                    xmlError(xp, "Missing value for attribute \"%s\"", aname);
                    goto err;
                }
                state = MPR_XML_NEW_ATT;
                rc = (*handler)(xp, state, tname, aname, mprGetBufStart(tokBuf));
                if (rc < 0) {
                    goto err;
                }
                state = MPR_XML_NEW_ELT;
                break;

            case MPR_XMLTOK_GR:
                /*
                 *  This is ">" the termination of the opening element
                 */
                if (*tname == '\0') {
                    xmlError(xp, "Missing element name");
                    goto err;
                }

                /*
                 *  Tell the user that the opening element is now complete
                 */
                state = MPR_XML_ELT_DEFINED;
                rc = (*handler)(xp, state, tname, 0, 0);
                if (rc < 0) {
                    goto err;
                }
                state = MPR_XML_ELT_DATA;
                break;

            case MPR_XMLTOK_SLASH_GR:
                /*
                 *  If we see a "/>" then this is a solo element
                 */
                if (*tname == '\0') {
                    xmlError(xp, "Missing element name");
                    goto err;
                }
                state = MPR_XML_SOLO_ELT_DEFINED;
                rc = (*handler)(xp, state, tname, 0, 0);
                if (rc < 0) {
                    goto err;
                }
                rc = 1;
                goto exit;
    
            default:
                xmlError(xp, "Syntax error");
                goto err;
            }
            break;

        case MPR_XML_ELT_DATA:      /* -------------------------------------- */
            /*
             *  We have seen the full opening element "<name ...>" and now await data or another element.
             */
            if (token == MPR_XMLTOK_LS) {
                /*
                 *  Recurse to handle the new element, comment etc.
                 */
                rc = parseNext(xp, MPR_XML_AFTER_LS);
                if (rc < 0) {
                    goto exit;
                }
                break;

            } else if (token == MPR_XMLTOK_LS_SLASH) {
                state = MPR_XML_END_ELT;
                break;

            } else if (token != MPR_XMLTOK_TEXT) {
                goto err;
            }
            if (mprGetBufLength(tokBuf) > 0) {
                /*
                 *  Pass the data between the element to the user
                 */
                rc = (*handler)(xp, state, tname, 0, mprGetBufStart(tokBuf));
                if (rc < 0) {
                    goto err;
                }
            }
            break;

        case MPR_XML_END_ELT:           /* -------------------------------------- */
            if (token != MPR_XMLTOK_TEXT) {
                xmlError(xp, "Missing closing element name for \"%s\"", tname);
                goto err;
            }
            /*
             *  The closing element name must match the opening element name 
             */
            if (strcmp(tname, mprGetBufStart(tokBuf)) != 0) {
                xmlError(xp, "Closing element name \"%s\" does not match on line %d. Opening name \"%s\"",
                    mprGetBufStart(tokBuf), xp->lineNumber, tname);
                goto err;
            }
            rc = (*handler)(xp, state, tname, 0, 0);
            if (rc < 0) {
                goto err;
            }
            if (getToken(xp, state) != MPR_XMLTOK_GR) {
                xmlError(xp, "Syntax error");
                goto err;
            }
            return 1;

        case MPR_XML_EOF:       /* ---------------------------------------------- */
            goto exit;

        case MPR_XML_ERR:   /* ---------------------------------------------- */
        default:
            goto err;
        }
    }
    mprAssert(0);

err:
    rc = -1;

exit:
    mprFree(tname);
    mprFree(aname);

    return rc;
}


/*
 *  Lexical analyser for XML. Return the next token reading input as required. It uses a one token look ahead and 
 *  push back mechanism (LAR1 parser). Text token identifiers are left in the tokBuf parser buffer on exit. This Lex 
 *  has special cases for the states MPR_XML_ELT_DATA where we have an optimized read of element data, and 
 *  MPR_XML_AFTER_LS where we distinguish between element names, processing instructions and comments. 
 */
static MprXmlToken getToken(MprXml *xp, int state)
{
    MprBuf      *tokBuf, *inBuf;
    char        *cp;
    int         c, rc;

    tokBuf = xp->tokBuf;
    inBuf = xp->inBuf;

    mprAssert(state >= 0);

    if ((c = getNextChar(xp)) < 0) {
        return MPR_XMLTOK_EOF;
    }
    mprFlushBuf(tokBuf);

    /*
     *  Special case parsing for names and for element data. We do this for performance so we can return to the caller 
     *  the largest token possible.
     */
    if (state == MPR_XML_ELT_DATA) {
        /*
         *  Read all the data up to the start of the closing element "<" or the start of a sub-element.
         */
        if (c == '<') {
            if ((c = getNextChar(xp)) < 0) {
                return MPR_XMLTOK_EOF;
            }
            if (c == '/') {
                return MPR_XMLTOK_LS_SLASH;
            }
            putLastChar(xp, c);
            return MPR_XMLTOK_LS;
        }
        do {
            if (mprPutCharToBuf(tokBuf, c) < 0) {
                return MPR_XMLTOK_TOO_BIG;
            }
            if ((c = getNextChar(xp)) < 0) {
                return MPR_XMLTOK_EOF;
            }
        } while (c != '<');

        /*
         *  Put back the last look-ahead character
         */
        putLastChar(xp, c);

        /*
         *  If all white space, then zero the token buffer
         */
        for (cp = tokBuf->start; *cp; cp++) {
            if (!isspace((int) *cp)) {
                return MPR_XMLTOK_TEXT;
            }
        }
        mprFlushBuf(tokBuf);
        return MPR_XMLTOK_TEXT;
    }

    while (1) {
        switch (c) {
        case ' ':
        case '\n':
        case '\t':
        case '\r':
            break;

        case '<':
            if ((c = getNextChar(xp)) < 0) {
                return MPR_XMLTOK_EOF;
            }
            if (c == '/') {
                return MPR_XMLTOK_LS_SLASH;
            }
            putLastChar(xp, c);
            return MPR_XMLTOK_LS;
    
        case '=':
            return MPR_XMLTOK_EQ;

        case '>':
            return MPR_XMLTOK_GR;

        case '/':
            if ((c = getNextChar(xp)) < 0) {
                return MPR_XMLTOK_EOF;
            }
            if (c == '>') {
                return MPR_XMLTOK_SLASH_GR;
            }
            return MPR_XMLTOK_ERR;
        
        case '\"':
        case '\'':
            xp->quoteChar = c;
            /* Fall through */

        default:
            /*
             *  We handle element names, attribute names and attribute values 
             *  here. We do NOT handle data between elements here. Read the 
             *  token.  Stop on white space or a closing element ">"
             */
            if (xp->quoteChar) {
                if ((c = getNextChar(xp)) < 0) {
                    return MPR_XMLTOK_EOF;
                }
                while (c != xp->quoteChar) {
                    if (mprPutCharToBuf(tokBuf, c) < 0) {
                        return MPR_XMLTOK_TOO_BIG;
                    }
                    if ((c = getNextChar(xp)) < 0) {
                        return MPR_XMLTOK_EOF;
                    }
                }
                xp->quoteChar = 0;

            } else {
                while (!isspace(c) && c != '>' && c != '/' && c != '=') {
                    if (mprPutCharToBuf(tokBuf, c) < 0) {
                        return MPR_XMLTOK_TOO_BIG;
                    }
                    if ((c = getNextChar(xp)) < 0) {
                        return MPR_XMLTOK_EOF;
                    }
                }
                putLastChar(xp, c);
            }
            if (mprGetBufLength(tokBuf) <= 0) {
                return MPR_XMLTOK_ERR;
            }
            mprAddNullToBuf(tokBuf);

            if (state == MPR_XML_AFTER_LS) {
                /*
                 *  If we are just inside an element "<", then analyze what we have to see if we have an element name, 
                 *  instruction or comment. Tokbuf will hold "?" for instructions or "!--" for comments.
                 */
                if (mprLookAtNextCharInBuf(tokBuf) == '?') {
                    /*  Just ignore processing instructions */
                    rc = scanFor(xp, "?>");
                    if (rc < 0) {
                        return MPR_XMLTOK_TOO_BIG;
                    } else if (rc == 0) {
                        return MPR_XMLTOK_ERR;
                    }
                    return MPR_XMLTOK_INSTRUCTIONS;

                } else if (mprLookAtNextCharInBuf(tokBuf) == '!') {
                    if (strncmp((char*) tokBuf->start, "![CDATA[", 8) == 0) {
                        mprAdjustBufStart(tokBuf, 8);
                        rc = scanFor(xp, "]]>");
                        if (rc < 0) {
                            return MPR_XMLTOK_TOO_BIG;
                        } else if (rc == 0) {
                            return MPR_XMLTOK_ERR;
                        }
                        return MPR_XMLTOK_CDATA;

                    } else {
                        mprFlushBuf(tokBuf);
                        rc = scanFor(xp, "-->");
                        if (rc < 0) {
                            return MPR_XMLTOK_TOO_BIG;
                        } else if (rc == 0) {
                            return MPR_XMLTOK_ERR;
                        }
                        return MPR_XMLTOK_COMMENT;
                    }
                }
            }
            trimToken(xp);
            return MPR_XMLTOK_TEXT;
        }
        if ((c = getNextChar(xp)) < 0) {
            return MPR_XMLTOK_EOF;
        }
    }

    /* Should never get here */
    mprAssert(0);
    return MPR_XMLTOK_ERR;
}


/*
 *  Scan for a pattern. Trim the pattern from the token. Return 1 if the pattern was found, return 0 if not found. 
 *  Return < 0 on errors.
 */
static int scanFor(MprXml *xp, char *pattern)
{
    MprBuf  *tokBuf;
    char    *start, *p, *cp;
    int     c;

    mprAssert(pattern);

    tokBuf = xp->tokBuf;
    mprAssert(tokBuf);

    start = mprGetBufStart(tokBuf);
    while (1) {
        cp = start;
        for (p = pattern; *p; p++) {
            if (cp >= (char*) tokBuf->end) {
                if ((c = getNextChar(xp)) < 0) {
                    return 0;
                }
                if (mprPutCharToBuf(tokBuf, c) < 0) {
                    return -1;
                }
            }
            if (*cp++ != *p) {
                break;
            }
        }
        if (*p == '\0') {
            /*
             *  Remove the pattern from the tokBuf
             */
            mprAdjustBufEnd(tokBuf, -(int) strlen(pattern));
            trimToken(xp);
            return 1;
        }
        start++;
    }
}


/*
 *  Get another character. We read and buffer blocks of data if we need more data to parse.
 */
static int getNextChar(MprXml *xp)
{
    MprBuf  *inBuf;
    int     c, l;

    inBuf = xp->inBuf;
    if (mprGetBufLength(inBuf) <= 0) {
        /*
         *  Flush to reset the servp/endp pointers to the start of the buffer so we can do a maximal read 
         */
        mprFlushBuf(inBuf);
        l = (xp->readFn)(xp, xp->inputArg, mprGetBufStart(inBuf), mprGetBufSpace(inBuf));
        if (l <= 0) {
            return -1;
        }
        mprAdjustBufEnd(inBuf, l);
    }
    c = mprGetCharFromBuf(inBuf);

    if (c == '\n') {
        xp->lineNumber++;
    }
    return c;
}


/*
 *  Put back a character in the input buffer
 */
static int putLastChar(MprXml *xp, int c)
{
    if (mprInsertCharToBuf(xp->inBuf, (char) c) < 0) {
        mprAssert(0);
        return MPR_ERR_BAD_STATE;
    }
    if (c == '\n') {
        xp->lineNumber--;
    }
    return 0;
}


/*
 *  Output a parse message
 */ 
static void xmlError(MprXml *xp, char *fmt, ...)
{
    va_list     args;
    char        *buf;

    mprAssert(fmt);

    va_start(args, fmt);
    buf = mprVasprintf(xp, MPR_MAX_STRING, fmt, args);
    va_end(args);

    mprFree(xp->errMsg);
    xp->errMsg = mprAsprintf(xp, MPR_MAX_STRING, "XML error: %s\nAt line %d\n", buf, xp->lineNumber);
    mprFree(buf);
}


/*
 *  Remove trailing whitespace in a token and ensure it is terminated with a NULL for easy parsing
 */
static void trimToken(MprXml *xp)
{
    while (isspace(mprLookAtLastCharInBuf(xp->tokBuf))) {
        mprAdjustBufEnd(xp->tokBuf, -1);
    }
    mprAddNullToBuf(xp->tokBuf);
}


cchar *mprXmlGetErrorMsg(MprXml *xp)
{
    if (xp->errMsg == 0) {
        return "";
    }
    return xp->errMsg;
}


int mprXmlGetLineNumber(MprXml *xp)
{
    return xp->lineNumber;
}


#else
void __dummyMprXml() {} 
#endif /* BLD_FEATURE_XML */

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
