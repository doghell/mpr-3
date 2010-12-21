#
#	Makefile for the MPR unit test source code
#
#	Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
#

include 	.makedep

TARGETS		= $(BLD_BIN_DIR)/benchMpr$(BLD_EXE) $(BLD_BIN_DIR)/testMpr$(BLD_EXE) $(BLD_BIN_DIR)/runProgram$(BLD_EXE)
OBJECTS		= $(patsubst %.c,$(BLD_OBJ_DIR)/%$(BLD_OBJ),$(wildcard test*.c))

compileTargets:	$(TARGETS)

$(BLD_BIN_DIR)/testMpr$(BLD_EXE): $(OBJECTS) $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
	bld --exe $(BLD_BIN_DIR)/testMpr$(BLD_EXE) --search "$(BLD_SSL_LIBPATHS)" --libs "mpr $(BLD_SSL_LIBS)" $(OBJECTS)

$(BLD_BIN_DIR)/runProgram$(BLD_EXE): runProgram.c $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
	bld --exe $(BLD_BIN_DIR)/runProgram$(BLD_EXE) --libs "$(BLD_MPR_LIBS)" runProgram

$(BLD_BIN_DIR)/benchMpr$(BLD_EXE): $(OBJECTS) $(BLD_LIB_DIR)/libmpr$(BLD_LIB)
	bld --exe $(BLD_BIN_DIR)/benchMpr$(BLD_EXE) --search "$(BLD_MPR_LIBPATHS)" --libs "$(BLD_MPR_LIBS)" benchMpr

cleanExtra:
	rm -f benchMpr$(BLD_EXE) trace*.txt *.txt.old testMpr$(BLD_LIB)

#
#   Local variables:
#   tab-width: 4
#   c-basic-offset: 4
#   End:
#   vim: sw=4 ts=4 noexpandtab
#
