#
#	Makefile for the Michael's Portable Runtime (MPR) library
#
#	Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
#

include			.makedep

ifeq ($(BLD_REGEXP_BUILTIN),1)
	PRE_DIRS	+= regexp
endif
ifeq ($(BLD_FEATURE_SSL),1)
	PRE_DIRS	+= ssl
endif
POST_DIRS		+= utils
ifeq ($(BLD_FEATURE_TEST),1)
	POST_DIRS 	+= test
endif

TARGETS			+= $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
ifeq ($(BLD_FEATURE_SSL),1)
	TARGETS		+= $(BLD_LIB_DIR)/libmprssl$(BLD_LIB)
endif
ifeq ($(BLD_REGEXP),1)
ifeq ($(BLD_FEATURE_REGEXP_DIR),)
	TARGETS		+= $(BLD_LIB_DIR)/libpcre$(BLD_LIB)
endif
endif

REG_SOURCES		+= $(shell find regexp -name '*.c' | egrep -v '/save') 
REG_OBJECTS		+= $(patsubst %.c,$(BLD_OBJ_DIR)/%$(BLD_OBJ),$(notdir $(REG_SOURCES)))

SSL_SOURCES		+= ssl/mprSsl.c
ifeq ($(BLD_FEATURE_MATRIXSSL),1)
	SSL_SOURCES	+= ssl/mprMatrixssl.c
endif
ifeq ($(BLD_FEATURE_OPENSSL),1)
	SSL_SOURCES	+= ssl/mprOpenssl.c
endif
SSL_OBJECTS		+= $(patsubst %.c,$(BLD_OBJ_DIR)/%$(BLD_OBJ),$(notdir $(SSL_SOURCES)))

#
#	Build the targets in this directory first then the builtin packages (ssl and regexp)
#
compileExtra: $(TARGETS) 

#
#	Build the mpr library
#
$(BLD_LIB_DIR)/libmpr$(BLD_LIB): $(OBJECTS)
	bld --library $(BLD_LIB_DIR)/libmpr --search "$(BLD_MPR_LIBPATHS)" --libs "$(BLD_MPR_WITHLIBS)" $(OBJECTS)

$(BLD_LIB_DIR)/libmprssl$(BLD_LIB): $(SSL_OBJECTS) $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
	bld --library $(BLD_LIB_DIR)/libmprssl --libs "$(BLD_MPR_LIBS) $(BLD_SSL_WITHLIBS)" $(SSL_OBJECTS)

$(BLD_LIB_DIR)/libpcre$(BLD_LIB): $(REG_OBJECTS) $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
	bld --library $(BLD_LIB_DIR)/libpcre $(REG_OBJECTS)
