#
#	Makefile -- Top level Makefile for MPR
#
#	Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
#
#
#	Standard Make targets supported are:
#	
#		make 						# Does a "make compile"
#		make clean					# Removes generated objects
#		make compile				# Compiles the source
#		make depend					# Generates the make dependencies
#		make test 					# Runs unit tests
#		make leakTest 				# Runs memory leak tests
#		make loadTest 				# Runs load tests
#		make benchmark 				# Runs benchmarks
#

include		build/make/Makefile.top

dependExtra:
	@[ ! -L extensions ] && ln -s ../packages extensions ; true


diff import sync:
	@if [ ! -x $(BLD_TOOLS_DIR)/edep$(BLD_BUILD_EXE) -a "$(BUILDING_CROSS)" != 1 ] ; then \
		$(MAKE) -S --no-print-directory _RECURSIVE_=1 -C $(BLD_TOP)/build/src compile ; \
	fi
	@import.ksh --$@ --src ../tools.stable --dir . ../tools.stable/build/export/export.gen
	@import.ksh --$@ --src ../tools.stable --dir . ../tools.stable/build/export/export.configure

#
#	Convenient configure targets
#
debug config:
	./configure
	[ -f .makedep ] && make clean >/dev/null ; true

release:
	./configure --defaults=release
	[ -f .makedep ] && make clean >/dev/null ; true

cross64:
	./configure --host=x86_64-apple-darwin --without-ssl
	[ -f .makedep ] && make clean >/dev/null ; true

config64:
	./configure --host=x86_64-apple-darwin --build=x86_64-apple-darwin
	[ -f .makedep ] && make clean >/dev/null ; true

config32:
	./configure --host=i386-apple-darwin --build=i386-apple-darwin --without-ssl

wince:
	./configure --host=arm-ms-wince --shared --without-ssl

vx:
	unset WIND_HOME WIND_BASE ; \
	SEARCH_PATH=/tornado ./configure --host=i386-wrs-vxworks --enable-all --without-ssl
	[ -f .makedep ] && make clean >/dev/null ; true

#
#	Samples for VxWorks cross compilation
#	
vx5:
	unset WIND_HOME WIND_BASE ; \
	SEARCH_PATH=/tornado ./configure --host=i386-wrs-vxworks --without-ssl
	[ -f .makedep ] && make clean >/dev/null ; true

vx6:
	unset WIND_HOME WIND_BASE ; \
	./configure --host=pentium-wrs-vxworks --tune=size --enable-all --without-ssl
	[ -f .makedep ] && make clean >/dev/null ; true


#	NM=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/nm$${ARCH}.exe \
#	RANLIB=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/ranlib$${ARCH}.exe \
#	STRIP=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/strip$${ARCH}.exe \
#	LD=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/ld$${ARCH}.exe \
#	BUILD_CC=/usr/bin/cc \
#	BUILD_LD=/usr/bin/cc \

vx5env:
	ARCH=386 ; \
	WIND_HOME=c:/tornado ; \
	WIND_BASE=$$WIND_HOME ; \
	WIND_GNU_PATH=$$WIND_BASE/host ; \
	AR=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/ar$${ARCH}.exe \
	CC=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/cc$${ARCH}.exe \
	IFLAGS="-I$$WIND_BASE/target/h -I$$WIND_BASE/target/h/wrn/coreip" \
	SEARCH_PATH=/tornado ./configure --host=i386-wrs-vxworks \
	--enable-all --without-matrixssl --without-openssl
	[ -f .makedep ] && make clean >/dev/null ; true

vx6env:
	ARCH=pentium ; \
	WIND_HOME=c:/WindRiver ; \
	VXWORKS=vxworks-6.3 ; \
	WIND_BASE=$$WIND_HOME/$$VXWORKS ; \
	PLATFORM=i586-wrs-vxworks ; \
	WIND_GNU_PATH=$$WIND_HOME/gnu/3.4.4-vxworks-6.3 ; \
	AR=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/ar$${ARCH}.exe \
	CC=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/cc$${ARCH}.exe \
	LD=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/cc$${ARCH}.exe \
	NM=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/$${PLATFORM}/bin/nm.exe \
	RANLIB=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/$${PLATFORM}/bin/ranlib.exe \
	STRIP=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/$${PLATFORM}/bin/strip.exe \
	CFLAGS="-I$$WIND_BASE/target/h -I$$WIND_BASE/target/h/wrn/coreip" \
	BUILD_CC=/usr/bin/cc \
	BUILD_LD=/usr/bin/cc \
	./configure --host=i386-wrs-vxworks \
	--enable-all --without-matrixssl --without-openssl
	[ -f .makedep ] && make clean >/dev/null ; true

#
#	Using ubuntu packages: uclibc-toolchain, libuclibc-dev
#	Use dpkg -L package to see installed files. Installed under /usr/i386-uclibc-linux
#
uclibc:
	PREFIX=i386-uclibc-linux; \
	DIR=/usr/i386-uclibc-linux/bin ; \
	AR=$${DIR}/$${PREFIX}-ar \
	CC=$${DIR}/$${PREFIX}-gcc \
	LD=$${DIR}/$${PREFIX}-gcc \
	NM=$${DIR}/$${PREFIX}-nm \
	RANLIB=$${DIR}/$${PREFIX}-ranlib \
	STRIP=$${DIR}/$${PREFIX}-strip \
	CFLAGS="-fno-stack-protector" \
	CXXFLAGS="-fno-rtti -fno-exceptions" \
	BUILD_CC=/usr/bin/cc \
	BUILD_LD=/usr/bin/cc \
	./configure --host=i386-pc-linux 
	[ -f .makedep ] && make clean >/dev/null ; true

redo:
	hg pull
	hg update -C
	make config
	make clean
	make depend
	make
	make test
