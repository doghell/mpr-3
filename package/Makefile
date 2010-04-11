#
#	Makefile for the packages
#
#	Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
#

include 		.makedep 

# PRE_DIRS		= $(BLD_HOST_OS)
BASE_NAME		:= "$(BLD_PRODUCT)-$(BLD_VERSION)-$(BLD_NUMBER)-$(BLD_HOST_DIST)-$(BLD_HOST_OS)-$(BLD_HOST_CPU)"
LAUNCH_BROWSER	:=  1
WIN_INS_DIR		:= c:/tmp/testInsAppweb
PKG_DIR			:= `pwd`/.tmp

ifeq	($(BLD_OS_LINUX),1)
	PRIV		:= fakepriv
else
	PRIV		:= priv
endif

packageExtra: 
	@echo "Installing and Packaging not required or supported"

install-package: 
	@echo "Installing and Packaging not required or supported"

uninstall-package:
	@echo "Installing and Packaging not required or supported"

install install-%:
	@echo "Installing and Packaging not required or supported"

uninstall uninstall-%: prep
	@echo "Installing and Packaging not required or supported"
	
checkInstalled:
	@echo "Installing and Packaging not required or supported"

dependExtra:
	[ "$(shell echo *.sh)" != "*.sh" ] && chmod +x *.sh ; true
