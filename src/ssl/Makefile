#
#	Makefile to build the SSL support
#
#	Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
#

MAKE_IFLAGS		= $(BLD_OPENSSL_IFLAGS) $(BLD_MATRIXSSL_IFLAGS)

include 		.makedep

ifeq ($(BLD_FEATURE_SSL),1)
	IMPORTS 	+= $(BLD_OPENSSL_IMPORTS) $(BLD_MATRIXSSL_IMPORTS)
	IMPORTED	+= $(patsubst %,$(BLD_LIB_DIR)/%, $(notdir $(IMPORTS)))

compileExtra:	$(IMPORTED)

$(IMPORTED): $(IMPORTS)
	getlib $^
endif

#
#	This should be done for OpenSSL for each new application to generate new DH parameters
#
opensslParams:
	openssl dhparam -out dh512.pem -C -2 512 >dh.c
	openssl dhparam -out dh1024.pem -C -2 1024 >>dh.c
