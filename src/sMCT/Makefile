
#
# Makefile for Linux GNU compiler
#
# Usage:
#   make            - Build all tools. They will be copied into 'bin' directory.
#   make clean      - Clean.
#   make sCSG      - Build sCSG.
#   make sBSP      - Build sBSP.
#   make sVIS      - Build sVIS.
#   make sRAD      - Build sRAD.
#
# Before running the tools, please make sure the default maximum stack size on your computer
#   is more than 4MB.
#
# When compiling with g++:
#   Flag '-fno-strict-aliasing' is a must.
#   All macro definitions should not be changed, because all code were written and checked under this assumption.
#   The following warnings should be ignored:
#     warning: no newline at end of file
#     warning: '???' may be used uninitialized in this function
#     warning: suggest parentheses around assignment used as truth value
#     warning: passing ‘float’ for argument 1 to ‘void seconds_to_hhmm(unsigned int, unsigned int&, unsigned int&, unsigned int&, unsigned int&)’
#     warning: ignoring #pragma warning
#     warning: comparison between signed and unsigned integer expressions
#     warning: enumeration value ‘???’ not handled in switch
#     warning: unused variable ‘???’
#     warning: converting to ‘int’ from ‘vec_t’
#








#
# Common .cpp and .h files for all projects
#

COMMON_CPPFILES = \
			common/blockmem.cpp \
			common/bspfile.cpp \
			common/cmdlib.cpp \
			common/cmdlinecfg.cpp \
			common/filelib.cpp \
			common/log.cpp \
			common/mathlib.cpp \
			common/messages.cpp \
			common/scriplib.cpp \
			common/threads.cpp \
			common/winding.cpp \
			common/arguments.cpp \

COMMON_INCLUDEDIRS = \
			template \
			common \

COMMON_INCLUDEFILES = \
			template/basictypes.h \
			common/blockmem.h \
			common/boundingbox.h \
			common/bspfile.h \
			common/cmdlib.h \
			common/cmdlinecfg.h \
			common/filelib.h \
			common/hlassert.h \
			common/log.h \
			common/mathlib.h \
			common/mathtypes.h \
			common/messages.h \
			common/scriplib.h \
			common/threads.h \
			common/win32fix.h \
			common/winding.h \
			common/arguments.h \

COMMON_DEFINITIONS = \
			VERSION_LINUX \
			SYSTEM_POSIX \
			NDEBUG \
			STDC_HEADERS \
			HAVE_FCNTL_H \
			HAVE_PTHREAD_H \
			HAVE_SYS_RESOURCE_H \
			HAVE_SYS_STAT_H \
			HAVE_SYS_TIME_H \
			HAVE_UNISTD_H \

COMMON_FLAGS = -Wall -O2 -fno-strict-aliasing -pthread -pipe

#
# Specific .cpp and .h files for hlcsg, hlbsp, hlvis and hlrad
#

HLCSG_CPPFILES = \
			$(COMMON_CPPFILES) \
			sCSG/ansitoutf8.cpp \
			sCSG/brush.cpp \
			sCSG/brushunion.cpp \
			sCSG/hullfile.cpp \
			sCSG/map.cpp \
			sCSG/qcsg.cpp \
			sCSG/textures.cpp \
			sCSG/wadpath.cpp \

HLCSG_INCLUDEDIRS = \
			$(COMMON_INCLUDEDIRS) \
			sCSG \

HLCSG_INCLUDEFILES = \
			$(COMMON_INCLUDEFILES) \
			sCSG/csg.h \
			sCSG/wadpath.h \

HLCSG_DEFINITIONS = \
			$(COMMON_DEFINITIONS) \
			sCSG \
			DOUBLEVEC_T \
			
HLBSP_CPPFILES = \
			$(COMMON_CPPFILES) \
			sBSP/brink.cpp \
			sBSP/merge.cpp \
			sBSP/outside.cpp \
			sBSP/portals.cpp \
			sBSP/qbsp.cpp \
			sBSP/solidbsp.cpp \
			sBSP/surfaces.cpp \
			sBSP/tjunc.cpp \
			sBSP/writebsp.cpp \

HLBSP_INCLUDEDIRS = \
			$(COMMON_INCLUDEDIRS) \
			sBSP \

HLBSP_INCLUDEFILES = \
			$(COMMON_INCLUDEFILES) \
			sBSP/bsp5.h \

HLBSP_DEFINITIONS = \
			$(COMMON_DEFINITIONS) \
			sBSP \
			DOUBLEVEC_T \

HLVIS_CPPFILES = \
			$(COMMON_CPPFILES) \
			sVIS/flow.cpp \
			sVIS/vis.cpp \

HLVIS_INCLUDEDIRS = \
			$(COMMON_INCLUDEDIRS) \
			sVIS \

HLVIS_INCLUDEFILES = \
			$(COMMON_INCLUDEFILES) \
			sVIS/vis.h \

HLVIS_DEFINITIONS = \
			$(COMMON_DEFINITIONS) \
			sVIS \

HLRAD_CPPFILES = \
			$(COMMON_CPPFILES) \
			sRAD/compress.cpp \
			sRAD/lerp.cpp \
			sRAD/lightmap.cpp \
			sRAD/loadtextures.cpp \
			sRAD/mathutil.cpp \
			sRAD/meshdesc.cpp \
			sRAD/meshtrace.cpp \
			sRAD/progmesh.cpp \
			sRAD/qrad.cpp \
			sRAD/qradutil.cpp \
			sRAD/sparse.cpp \
			sRAD/stringlib.cpp \
			sRAD/studio.cpp \
			sRAD/trace.cpp \
			sRAD/transfers.cpp \
			sRAD/transparency.cpp \
			sRAD/vismatrix.cpp \
			sRAD/vismatrixutil.cpp \

HLRAD_INCLUDEDIRS = \
			$(COMMON_INCLUDEDIRS) \
			sRAD \

HLRAD_INCLUDEFILES = \
			$(COMMON_INCLUDEFILES) \
			sRAD/compress.h \
			sRAD/list.h \
			sRAD/meshdesc.h \
			sRAD/meshtrace.h \
			sRAD/qrad.h \

HLRAD_DEFINITIONS = \
			$(COMMON_DEFINITIONS) \
			sRAD \

#
# Build commands
#

.PHONY : all
all : bin/sCSG bin/sBSP bin/sVIS bin/sRAD printusage
	@echo ======== OK ========

.PHONY : sCSG
sCSG : bin/sCSG printusage
	@echo ======== OK ========

.PHONY : sBSP
sBSP : bin/sBSP printusage
	@echo ======== OK ========

.PHONY : sVIS
sVIS : bin/sVIS printusage
	@echo ======== OK ========

.PHONY : sRAD
sRAD : bin/sRAD printusage
	@echo ======== OK ========

bin/sCSG : $(HLCSG_CPPFILES:%.cpp=sCSG/release/%.o) printusage
	@echo ======== sCSG : linking ========
	mkdir -p sCSG/release/bin
	g++ $(COMMON_FLAGS) -o sCSG/release/bin/sCSG $(addprefix -I,$(HLCSG_INCLUDEDIRS)) $(addprefix -D,$(HLCSG_DEFINITIONS)) $(HLCSG_CPPFILES:%.cpp=sCSG/release/%.o)
	mkdir -p bin
	cp sCSG/release/bin/sCSG bin/sCSG

$(HLCSG_CPPFILES:%.cpp=sCSG/release/%.o) : sCSG/release/%.o : %.cpp $(HLCSG_INCLUDEFILES) printusage
	@echo ======== sCSG : compiling $< ========
	mkdir -p $(dir $@)
	g++ -c $(COMMON_FLAGS) -o $@ $(addprefix -I,$(HLCSG_INCLUDEDIRS)) $(addprefix -D,$(HLCSG_DEFINITIONS)) $<

bin/sBSP : $(HLBSP_CPPFILES:%.cpp=sBSP/release/%.o) printusage
	@echo ======== sBSP : linking ========
	mkdir -p sBSP/release/bin
	g++ $(COMMON_FLAGS) -o sBSP/release/bin/sBSP $(addprefix -I,$(HLBSP_INCLUDEDIRS)) $(addprefix -D,$(HLBSP_DEFINITIONS)) $(HLBSP_CPPFILES:%.cpp=sBSP/release/%.o)
	mkdir -p bin
	cp sBSP/release/bin/sBSP bin/sBSP

$(HLBSP_CPPFILES:%.cpp=sBSP/release/%.o) : sBSP/release/%.o : %.cpp $(HLBSP_INCLUDEFILES) printusage
	@echo ======== sBSP : compiling $< ========
	mkdir -p $(dir $@)
	g++ -c $(COMMON_FLAGS) -o $@ $(addprefix -I,$(HLBSP_INCLUDEDIRS)) $(addprefix -D,$(HLBSP_DEFINITIONS)) $<

bin/sVIS : $(HLVIS_CPPFILES:%.cpp=sVIS/release/%.o) printusage
	@echo ======== sVIS : linking ========
	mkdir -p sVIS/release/bin
	g++ $(COMMON_FLAGS) -o sVIS/release/bin/sVIS $(addprefix -I,$(HLVIS_INCLUDEDIRS)) $(addprefix -D,$(HLVIS_DEFINITIONS)) $(HLVIS_CPPFILES:%.cpp=sVIS/release/%.o)
	mkdir -p bin
	cp sVIS/release/bin/sVIS bin/sVIS

$(HLVIS_CPPFILES:%.cpp=sVIS/release/%.o) : sVIS/release/%.o : %.cpp $(HLVIS_INCLUDEFILES) printusage
	@echo ======== sVIS : compiling $< ========
	mkdir -p $(dir $@)
	g++ -c $(COMMON_FLAGS) -o $@ $(addprefix -I,$(HLVIS_INCLUDEDIRS)) $(addprefix -D,$(HLVIS_DEFINITIONS)) $<

bin/sRAD : $(HLRAD_CPPFILES:%.cpp=sRAD/release/%.o) printusage
	@echo ======== sRAD : linking ========
	mkdir -p sRAD/release/bin
	g++ $(COMMON_FLAGS) -o sRAD/release/bin/sRAD $(addprefix -I,$(HLRAD_INCLUDEDIRS)) $(addprefix -D,$(HLRAD_DEFINITIONS)) $(HLRAD_CPPFILES:%.cpp=sRAD/release/%.o)
	mkdir -p bin
	cp sRAD/release/bin/sRAD bin/sRAD

$(HLRAD_CPPFILES:%.cpp=sRAD/release/%.o) : sRAD/release/%.o : %.cpp $(HLRAD_INCLUDEFILES) printusage
	@echo ======== sRAD : compiling $< ========
	mkdir -p $(dir $@)
	g++ -c $(COMMON_FLAGS) -o $@ $(addprefix -I,$(HLRAD_INCLUDEDIRS)) $(addprefix -D,$(HLRAD_DEFINITIONS)) $<

.PHONY : printusage
printusage :
	head -n 35 Makefile

#
# Clean
#

.PHONY : clean
clean : printusage
	rm -rf sCSG/release
	rm -rf sBSP/release
	rm -rf sVIS/release
	rm -rf sRAD/release
	rm -rf bin
	@echo ======== OK ========


