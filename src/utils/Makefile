#
#	Makefile for the MPR utilities
#
#	Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
#

include 	.makedep

ifeq ($(BUILD_NATIVE_OR_COMPLETE_CROSS),1)
	TARGETS	+= $(BLD_BIN_DIR)/charGen$(BLD_EXE) $(BLD_BIN_DIR)/makerom$(BLD_EXE)
endif

ifeq ($(BLD_FEATURE_HTTP_CLIENT),1)
	TARGETS	+= $(BLD_BIN_DIR)/http$(BLD_EXE)
endif

compileTargets: $(TARGETS)

$(BLD_BIN_DIR)/charGen$(BLD_EXE): $(BLD_OBJ_DIR)/charGen$(BLD_OBJ) $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
	@bld --exe $(BLD_BIN_DIR)/charGen$(BLD_EXE) --search "$(BLD_MPR_LIBPATHS)" --libs "$(BLD_MPR_LIBS)" charGen

$(BLD_BIN_DIR)/http$(BLD_EXE): $(BLD_OBJ_DIR)/http$(BLD_OBJ) $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
	@bld --exe $(BLD_BIN_DIR)/http$(BLD_EXE) --search "$(BLD_SSL_LIBPATHS)" --libs "$(BLD_MPR_LIBS) $(BLD_SSL_LIBS)" http

$(BLD_BIN_DIR)/makerom$(BLD_EXE): $(BLD_OBJ_DIR)/makerom$(BLD_OBJ) $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
	@bld --exe $(BLD_BIN_DIR)/makerom$(BLD_EXE) --search "$(BLD_MPR_LIBPATHS)" --libs "$(BLD_MPR_LIBS)" makerom

#
#   Local variables:
#   tab-width: 4
#   c-basic-offset: 4
#   End:
#   vim: sw=4 ts=4
#
