/**
 *  mprSsl.c -- Load and manage the SSL providers.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"
#include    "mprSsl.h"

#if BLD_FEATURE_SSL
/************************************ Code ************************************/
/*
 *  Load the ssl provider
 */
static MprModule *loadSsl(MprCtx ctx, bool lazy)
{
    Mpr         *mpr;
    MprModule   *mp;

    mpr = mprGetMpr(ctx);

    if (mpr->flags & MPR_SSL_PROVIDER_LOADED) {
        return mprLookupModule(ctx, "sslModule");
    }

    mprLog(ctx, MPR_CONFIG, "Activating the SSL provider");
#if BLD_FEATURE_OPENSSL
    /*
     *  NOTE: preference given to open ssl if both are enabled
     */
    mprLog(ctx, 2, "Loading OpenSSL module");
    if (mprCreateOpenSslModule(ctx, lazy) < 0) {
        return 0;
    }

#elif BLD_FEATURE_MATRIXSSL
    mprLog(ctx, 2, "Loading MatrixSSL module");
    if (mprCreateMatrixSslModule(ctx, lazy) < 0) {
        return 0;
    }
#endif
    if ((mp = mprCreateModule(ctx, "sslModule", BLD_VERSION, (void*) NULL, NULL, NULL)) == 0) {
        return 0;
    }
    mpr->flags |= MPR_SSL_PROVIDER_LOADED;
    return mp;
}


MprModule *mprLoadSsl(MprCtx ctx, bool lazy)
{
    return loadSsl(ctx, lazy);
}


/*
 *  Loadable module interface. 
 */
MprModule *mprSslInit(MprCtx ctx, cchar *path)
{
    return loadSsl(ctx, 1);
}


/*
 *  Create a new Ssl context object
 */
MprSsl *mprCreateSsl(MprCtx ctx)
{
    MprSsl      *ssl;

    /*
     *  Create with a null destructor so the provider can install one if required
     */
    ssl =  mprAllocObjWithDestructorZeroed(ctx, MprSsl, NULL);
    if (ssl == 0) {
        return 0;
    }
    ssl->ciphers = mprStrdup(ssl, MPR_DEFAULT_CIPHER_SUITE);
    ssl->protocols = MPR_HTTP_PROTO_SSLV3 | MPR_HTTP_PROTO_TLSV1;
    ssl->verifyDepth = 6;
    return ssl;
}


void mprConfigureSsl(MprSsl *ssl)
{
    MprSocketProvider   *provider;

    provider = mprGetMpr(ssl)->socketService->secureProvider;
    if (provider) {
        provider->configureSsl(ssl);
    }
}


void mprSetSslCiphers(MprSsl *ssl, cchar *ciphers)
{
    mprAssert(ssl);
    
    mprFree(ssl->ciphers);
    ssl->ciphers = mprStrdup(ssl, ciphers);
}


void mprSetSslKeyFile(MprSsl *ssl, cchar *keyFile)
{
    mprAssert(ssl);
    
    mprFree(ssl->keyFile);
    ssl->keyFile = mprStrdup(ssl, keyFile);
}


void mprSetSslCertFile(MprSsl *ssl, cchar *certFile)
{
    mprAssert(ssl);
    
    mprFree(ssl->certFile);
    ssl->certFile = mprStrdup(ssl, certFile);
}


void mprSetSslCaFile(MprSsl *ssl, cchar *caFile)
{
    mprAssert(ssl);
    
    mprFree(ssl->caFile);
    ssl->caFile = mprStrdup(ssl, caFile);
}


void mprSetSslCaPath(MprSsl *ssl, cchar *caPath)
{
    mprAssert(ssl);
    
    mprFree(ssl->caPath);
    ssl->caPath = mprStrdup(ssl, caPath);
}


void mprSetSslProtocols(MprSsl *ssl, int protocols)
{
    ssl->protocols = protocols;
}


void mprSetSocketSslConfig(MprSocket *sp, MprSsl *ssl)
{
    if (sp->sslSocket) {
        sp->sslSocket->ssl = ssl;
    }
}


void mprVerifySslClients(MprSsl *ssl, bool on)
{
    ssl->verifyClient = on;
}


#else /* SSL */

/*
 *  Stubs
 */
MprModule *mprLoadSsl(MprCtx ctx, bool lazy)
{
    return 0;
}

MprModule *mprSslInit(MprCtx ctx, cchar *path)
{
    return 0;
}


MprSsl *mprCreateSsl(MprCtx ctx)
{
    return 0;
}


void mprConfigureSsl(MprSsl *ssl)
{
}


void mprSetSslCiphers(MprSsl *ssl, cchar *ciphers)
{
}


void mprSetSslKeyFile(MprSsl *ssl, cchar *keyFile)
{
}


void mprSetSslCertFile(MprSsl *ssl, cchar *certFile)
{
}


void mprSetSslCaFile(MprSsl *ssl, cchar *caFile)
{
}


void mprSetSslCaPath(MprSsl *ssl, cchar *caPath)
{
}


void mprSetSslProtocols(MprSsl *ssl, int protocols)
{
}


void mprSetSocketSslConfig(MprSocket *sp, MprSsl *ssl)
{
}


void mprVerifySslClients(MprSsl *ssl, bool on)
{
}


#endif /* SSL */


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
