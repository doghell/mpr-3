#
#	Makefile -- MPR API Documentation
#
#	Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
#

include		.makedep

doc:
	rm ../../../all/*.c
	make -C ../../../all
	rm -fr xml html
	mkdir -p xml html
	doxygen mpr.spec
	ejs $(BLD_TOOLS_DIR)/gendoc.es --bare --title "Mpr - Portable Runtime Native API" --tags ../mpr.tags \
		xml/mpr_8h.xml xml/group* xml/struct_*.xml >../mprBare.html
	ejs $(BLD_TOOLS_DIR)/gendoc.es --title "Mpr - Portable Runtime Native API" --tags ../mpr.tags \
		xml/mpr_8h.xml xml/group* xml/struct_*.xml >../mpr.html

x:
	ejs $(BLD_TOOLS_DIR)/gendoc.es --title "Mpr - Portable Runtime Native API" --tags ../mpr.tags \
		xml/mpr_8h.xml xml/group* xml/struct_*.xml >../mpr.html

cleanExtra:
	rm -rf xml html ../mpr.tags

clobberExtra:
	rm -f ../mpr.html ../mprBare.html
