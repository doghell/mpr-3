#
#  Makefile -- Libraries Makefile
#
# Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
#

include 	.makedep

dependExtra:
	mkdir -p $(BLD_LIB_DIR)

cleanExtra:
	rm -f *.a *.lib *.dll *.dylib 
	rm -f */*.a */*.lib */*.dylib 
	rm -f $(BLD_LIB_DIR)/*.a $(BLD_LIB_DIR)/*.so $(BLD_LIB_DIR)/*.lib $(BLD_LIB_DIR)/*.dylib

clobberExtra: cleanExtra
	[ "$(BUILD_CROSS)" = 1 ] && rm -fr "./$(BLD_HOST_SYSTEM)"

#
#   Local variables:
#   tab-width: 4
#   c-basic-offset: 4
#   End:
#   vim: sw=4 ts=4 noexpandtab
#
