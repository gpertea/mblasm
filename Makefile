# Useful directories

#this is the path where the samtools source package was built
#so all the samtools headers (*.h) and (*.a) library files are in there
SAM := ./samtools-0.1.18

GDIR := ../gclib

#INCDIRS := -I${SAM} -I${GDIR}
INCDIRS := -I${GDIR}
SYSTYPE :=     $(shell uname)

MACHTYPE :=     $(shell uname -m)

CC      := g++
#BASEFLAGS  = -Wall ${INCDIRS} $(MARCH) -D_FILE_OFFSET_BITS=64 \
#-D_LARGEFILE_SOURCE -fno-exceptions -fno-rtti -fno-strict-aliasing \
#-D_REENTRANT

BASEFLAGS  = -Wall -Wextra ${INCDIRS} -fno-exceptions -fno-rtti \
 -D_REENTRANT -fno-strict-aliasing

# C/C++ linker and core libs

LINKER := g++
LDFLAGS := 
LIBS := 

ifneq (,$(filter %release %static, $(MAKECMDGOALS)))
  # -- release build
  CFLAGS := -O2 -msse2 -DNDEBUG $(BASEFLAGS)
  LDFLAGS := $(LDFLAGS)
  LIBS := $(LIBS)
  ifneq (,$(findstring static,$(MAKECMDGOALS)))
    LDFLAGS += -static-libstdc++ -static-libgcc
  endif
else # debug build
  ifneq (,$(filter %memcheck %memdebug, $(MAKECMDGOALS)))
     #make memcheck : use the statically linked address sanitizer in gcc 4.9.x
     GCCVER49 := $(shell expr `g++ -dumpversion | cut -f1,2 -d.` \>= 4.9)
     ifeq "$(GCCVER49)" "0"
       $(error gcc version 4.9 or greater is required for this build target)
     endif
     CFLAGS := -fno-omit-frame-pointer -fsanitize=undefined -fsanitize=address
     GCCVER5 := $(shell expr `g++ -dumpversion | cut -f1 -d.` \>= 5)
     ifeq "$(GCCVER5)" "1"
       CFLAGS += -fsanitize=bounds -fsanitize=float-divide-by-zero -fsanitize=vptr
       CFLAGS += -fsanitize=float-cast-overflow -fsanitize=object-size
       #CFLAGS += -fcheck-pointer-bounds -mmpx
     endif
     CFLAGS += $(BASEFLAGS)
     CFLAGS := -g -DDEBUG -D_DEBUG -DGDEBUG -fno-common -fstack-protector $(CFLAGS)
     LDFLAGS := -g $(LDFLAGS)
     #LIBS := -Wl,-Bstatic -lasan -lubsan -Wl,-Bdynamic -ldl $(LIBS)
     LIBS := -lasan -lubsan -ldl $(LIBS)
  else
     # regular debug build
     CFLAGS := -g -DDEBUG -D_DEBUG -DGDEBUG $(BASEFLAGS)
     LDFLAGS := -g $(LDFLAGS)
     LIBS := $(LIBS)
  endif
endif



%.o : %.cpp
	${CC} ${CFLAGS} -c $< -o $@

OBJS := ${GDIR}/GBase.o ${GDIR}/GStr.o ${GDIR}/GArgs.o ${GDIR}/gdna.o ./GapAssem.o

#ifndef NOTHREADS
# OBJS += ${GDIR}/GThreads.o 
#endif

#ifdef GDEBUG
# OBJS += ${GDIR}/proc_mem.o
#endif

.PHONY : all debug release
all:    mblasm mblaor nrcl tclust sclust
debug : all
release : all
memcheck : all
memdebug : all 
static : all

bamcons :  ./bamcons.o ${GDIR}/GFastaIndex.o ${GDIR}/GFaSeqGet.o ${GDIR}/GBam.o ${OBJS} 
	${LINKER} $(LDFLAGS) -o $@ ${filter-out %.a %.so, $^} -L${SAM} ${LIBS} -lbam

mblasm :  ./mblasm.o ${GDIR}/GCdbYank.o ${GDIR}/gcdb.o ${OBJS}
	${LINKER} $(LDFLAGS) -o $@ ${filter-out %.a %.so, $^} ${LIBS}

nrcl:  ./nrcl.o ${GDIR}/GBase.o ${GDIR}/GStr.o ${GDIR}/GArgs.o
	${LINKER} $(LDFLAGS) -o $@ ${filter-out %.a %.so, $^}

tclust:  ./tclust.o ${GDIR}/GBase.o ${GDIR}/GStr.o ${GDIR}/GArgs.o
	${LINKER} $(LDFLAGS) -o $@ ${filter-out %.a %.so, $^}

sclust:  ./sclust.o ${GDIR}/GBase.o ${GDIR}/GStr.o ${GDIR}/GArgs.o
	${LINKER} $(LDFLAGS) -o $@ ${filter-out %.a %.so, $^}

./GapAssem.o: GapAssem.h

mblaor :  ./mblaor.o ./GapAssem.o ${GDIR}/GCdbYank.o ${GDIR}/gcdb.o ${OBJS}
	${LINKER} -o $@ ${filter-out %.a %.so, $^} $(LDFLAGS) ${LIBS}

# target for removing all object files

.PHONY : tidy
tidy::
	${RM} mblasm mblaor mblasm.o* mblaor.o* nrcl.o* *clust *clust.exe *.o ${OBJS} ${GDIR}/codons.o

# target for removing all object files

.PHONY : clean
clean:: tidy
	
