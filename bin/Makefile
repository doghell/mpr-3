#
#  Makefile -- Binaries Makefile
#
# Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
#

include 	.makedep

dependExtra:
	mkdir -p $(BLD_BIN_DIR)

cleanExtra: 
	rm -f *.def *.out *.exe *.lib *.dll *.dylib *.a *.so *.mod
	rm -f */*.def */*.out */*.exe */*.lib */*.dylib */*.a */*.so */*.mod
	rm -f $(BLD_BIN_DIR)/*.def
	rm -f $(BLD_BIN_DIR)/*.out
	rm -f $(BLD_BIN_DIR)/*.exe
	rm -f $(BLD_BIN_DIR)/*.lib
	rm -f $(BLD_BIN_DIR)/*.dll
	rm -f $(BLD_BIN_DIR)/*.dylib
	rm -f $(BLD_BIN_DIR)/*.a
	rm -f $(BLD_BIN_DIR)/*.so
	rm -f $(BLD_BIN_DIR)/*.mod
	rm -f benchMpr* charGen* http* makerom* runProgram* testMpr*

clobberExtra: cleanExtra
	[ "$(BUILD_CROSS)" = 1 ] && rm -fr "./$(BLD_HOST_SYSTEM)"

#
#   Local variables:
#   tab-width: 4
#   c-basic-offset: 4
#   End:
#   vim: sw=4 ts=4
#
