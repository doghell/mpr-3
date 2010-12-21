#
#	Makefile for the all-in-one MPR distribution
#
#	Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
#

include			.makedep

COMPILE			=
TARGETS			+= mprLib.c mpr.h http.c makerom.c mprPcre.c mprSsl.c

compileExtra: 	$(TARGETS) dummy

#
#	Order of headers matters
#
HEADERS			+= $(patsubst %,../src/include/%, mprOs.h mprTune.h mpr.h mprTest.h)
MPR_SOURCES 	+= $(wildcard ../src/*.c) 
SSL_SOURCES 	+= ../src/include/mprSsl.h $(wildcard ../src/ssl/*.c) 

ifeq ($(BLD_REGEXP_DIR),)
	HEADERS		+= ../src/include/pcre.h
	PCRE_SOURCES+= $(patsubst %,../src/regexp/%, config.h pcre_internal.h ucp.h ucpinternal.h ucptable.h *.c)
endif

mpr.h: Makefile $(HEADERS)
	$(call log) "[Generate]" all/mpr.h
	all-in-one $(HEADERS) | egrep -v '#include.*mpr|#inc.*config.h|#inc.*ucp.*\.h|#inc.*pcre' >mpr.h

mprLib.c: Makefile $(HEADERS) $(MPR_SOURCES)
	$(call log) "[Generate]" all/mprLib.c
	echo '#include "mpr.h"' >mprLib.c
	all-in-one $(MPR_SOURCES) | egrep -v '#inc.*mpr' >>mprLib.c

http.c: Makefile ../src/utils/http.c
	$(call log) "[Generate]" all/http.c
	cp ../src/utils/http.c http.c

makerom.c: Makefile ../src/utils/makerom.c
	cp ../src/utils/makerom.c makerom.c

mprPcre.c: Makefile $(HEADERS) $(PCRE_SOURCES)
	$(call log) "[Generate]" all/mprPcre.c
	echo "#define BLD_ALL_IN_ONE 1" >mprPcre.c
	all-in-one $(HEADERS) $(PCRE_SOURCES) | egrep -v '#inc.*mpr|#inc.*config.h|#inc.*ucp.*\.h|#inc.*pcre' >>mprPcre.c

mprSsl.c: Makefile ../src/include/mprSsl.h $(HEADERS) $(SSL_SOURCES)
	$(call log) "[Generate]" all/mprSsl.c
	all-in-one $(HEADERS) $(SSL_SOURCES) | egrep -v '#inc.*mpr' >mprSsl.c
	cp ../src/include/mprSsl.h mprSsl.h

#
#	Import libraries
#
$(OPENSSL_IMPORTS): $(patsubst %,$(BLD_OPENSSL_IMP_LIBPATH)/%, $(BLD_OPENSSL_IMP_LIBS))
	getlib $(BLD_OPENSSL_IMP_LIBPATH)/$(notdir $@)

$(MATRIXSSL_IMPORTS): $(patsubst %,$(BLD_MATRIXSSL_IMP_LIBPATH)/%, $(BLD_MATRIXSSL_IMP_LIBS))
	getlib $(BLD_MATRIXSSL_IMP_LIBPATH)/$(notdir $@)

cleanExtra:
	rm -f *.c *.h

#
#   Local variables:
#   tab-width: 4
#   c-basic-offset: 4
#   End:
#   vim: sw=4 ts=4 noexpandtab
#
