#
#	Makefile to build the regular expression support
#
#	Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
#

include 		.makedep

ifneq ($(BLD_FEATURE_REGEXP_DIR),)
	IMPORTS 	+= $(BLD_REGEXP_IMPORTS) $(BLD_MATRIXSSL_IMPORTS)
	IMPORTED	+= $(patsubst %,$(BLD_LIB_DIR)/%, $(notdir $(IMPORTS)))
endif

compileExtra:	$(IMPORTS)

$(IMPORTED): $(IMPORTS)
	getlib $<

