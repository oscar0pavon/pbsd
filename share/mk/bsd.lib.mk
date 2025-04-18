#	$NetBSD: bsd.lib.mk,v 1.419 2025/04/13 17:23:06 riastradh Exp $
#	@(#)bsd.lib.mk	8.3 (Berkeley) 4/22/94

.include <bsd.init.mk>
.include <bsd.shlib.mk>
.include <bsd.gcc.mk>
.include <bsd.sanitizer.mk>

# Pull in <bsd.sys.mk> here so we can override its .c.o rule
.include <bsd.sys.mk>

LIBISMODULE?=	no
LIBISPRIVATE?=	no
LIBISCXX?=	no

.if ${LIBISMODULE} != "no"
_LIB_PREFIX?=	# empty
MKDEBUGLIB:=	no
MKPROFILE:=	no
MKPICINSTALL:=	no
MAKESTATICLIB?=	no
MAKELINKLIB?=	yes
_LINTINSTALL?=	no
.else
_LIB_PREFIX?=	lib
.endif

.if ${LIBISPRIVATE} != "no"
MKDEBUGLIB:=	no
MKPROFILE:=	no
MKPICINSTALL:=	no
. if defined(NOSTATICLIB) && ${MKPICLIB} != "no"
MAKESTATICLIB?=	no
. elif ${LIBISPRIVATE} != "pic"
MKPIC:=		no
. endif
MAKELINKLIB?=	no
_LINTINSTALL?=	no
.endif

_LINTINSTALL?=	${MKLINT}
LINKINSTALL?=	${MAKELINKLIB}
MAKELINKLIB?=	${MKLINKLIB}
MAKESTATICLIB?=	${MKSTATICLIB}

##### Basic targets
.PHONY:		checkver libinstall
realinstall:	checkver libinstall

##### LIB specific flags.
# XXX: This is needed for programs that link with .a libraries
# Perhaps a more correct solution is to always generate _pic.a
# files or always have a shared library.
# Another fix is to provide rcrt0.o like OpenBSD does and
# do relocations for static PIE.
.if defined(MKPIE) && (${MKPIE} != "no") && !defined(NOPIE)
CFLAGS+=        ${PIE_CFLAGS}
AFLAGS+=        ${PIE_AFLAGS}
.endif
# The -fPIC is needed for libraries that include other libraries
# The order matters here, PIC needs to be last
.if ${LIBISPRIVATE} == "pic" && ${MKPIC} != "no"
CFLAGS+=        -fPIC
AFLAGS+=        -fPIC
.endif

PGFLAGS+=	-pg
.if ${MKPIC} != "no"
PGFLAGS+=	-fPIC
.endif

##### Libraries that this may depend upon.
.if defined(LIBDPLIBS) && ${MKPIC} != "no"				# {
.  for _lib _dir in ${LIBDPLIBS}
.    if !defined(LIBDO.${_lib})
LIBDO.${_lib}!=	cd "${_dir}" && ${PRINTOBJDIR}
.MAKEOVERRIDES+=LIBDO.${_lib}
.    endif
.    if ${LIBDO.${_lib}} == "_external"
LDADD+=		-l${_lib}
.    else
LDADD+=		-L${LIBDO.${_lib}} -l${_lib}
.      if exists(${LIBDO.${_lib}}/lib${_lib}_pic.a)
DPADD+=         ${LIBDO.${_lib}}/lib${_lib}_pic.a
.      elif exists(${LIBDO.${_lib}}/lib${_lib}.so)
DPADD+=         ${LIBDO.${_lib}}/lib${_lib}.so
.      else
DPADD+=         ${LIBDO.${_lib}}/lib${_lib}.a
.      endif
.    endif
.  endfor
.endif									# }

##### Build and install rules
MKDEP_SUFFIXES?=	.o .po .pico .go .ln .d

.if !defined(SHLIB_MAJOR) && exists(${SHLIB_VERSION_FILE})		# {
SHLIB_MAJOR != . ${SHLIB_VERSION_FILE} ; echo $$major
SHLIB_MINOR != . ${SHLIB_VERSION_FILE} ; echo $$minor
SHLIB_TEENY != . ${SHLIB_VERSION_FILE} ; echo $$teeny

DPADD+=	${SHLIB_VERSION_FILE}

# Check for higher installed library versions.
.if !defined(NOCHECKVER) && !defined(NOCHECKVER_${LIB}) && \
	exists(${NETBSDSRCDIR}/lib/checkver)
checkver:
	@(cd "${.CURDIR}" && \
	    HOST_SH=${HOST_SH:Q} AWK=${TOOL_AWK:Q} \
	    ${HOST_SH} ${NETBSDSRCDIR}/lib/checkver -v ${SHLIB_VERSION_FILE} \
		    -d ${_DEST.OBJ} ${LIB})
.endif
.endif									# }

.if !target(checkver)
checkver:
.endif

print-shlib-major:
.if defined(SHLIB_MAJOR) && ${MKPIC} != "no"
	@echo ${SHLIB_MAJOR}
.else
	@false
.endif

print-shlib-minor:
.if defined(SHLIB_MINOR) && ${MKPIC} != "no"
	@echo ${SHLIB_MINOR}
.else
	@false
.endif

print-shlib-teeny:
.if defined(SHLIB_TEENY) && ${MKPIC} != "no"
	@echo ${SHLIB_TEENY}
.else
	@false
.endif

.if ${LIBISPRIVATE} == "no"
.if defined(SHLIB_MAJOR) && !empty(SHLIB_MAJOR)				# {
.if defined(SHLIB_MINOR) && !empty(SHLIB_MINOR)
.if defined(SHLIB_TEENY) && !empty(SHLIB_TEENY)
SHLIB_FULLVERSION=${SHLIB_MAJOR}.${SHLIB_MINOR}.${SHLIB_TEENY}
.else
SHLIB_FULLVERSION=${SHLIB_MAJOR}.${SHLIB_MINOR}
.endif
.else
SHLIB_FULLVERSION=${SHLIB_MAJOR}
.endif
.endif									# }
.endif

# add additional suffixes not exported.
# .po is used for profiling object files.
# .pico is used for PIC object files.
.SUFFIXES: .out .a .ln .pico .po .go .o .s .S .c .cc .cpp .cxx .C .m .F .f .r .y .l .cl .p .h
.SUFFIXES: .sh .m4 .m


# Set PICFLAGS to cc flags for producing position-independent code,
# if not already set.

# Data-driven table using make variables to control how shared libraries
# are built for different platforms and object formats.
# SHLIB_MAJOR, SHLIB_MINOR, SHLIB_TEENY: Major, minor, and teeny version
#			numbers of shared library
# SHLIB_SOVERSION:	version number to be compiled into a shared library
#			via -soname. Usually ${SHLIB_MAJOR} on ELF.
#			NetBSD/pmax used to use ${SHLIB_MAJOR}[.${SHLIB_MINOR}
#			[.${SHLIB_TEENY}]]
# SHLIB_SHFLAGS:	Flags to tell ${LD} to emit shared library.
#			with ELF, also set shared-lib version for ld.so.
# SHLIB_LDSTARTFILE:	support .o file, call C++ file-level constructors
# SHLIB_LDENDFILE:	support .o file, call C++ file-level destructors

PICFLAGS ?= -fPIC

.if ${MKPICLIB} != "no"
CSHLIBFLAGS+= ${PICFLAGS} ${SANITIZERFLAGS} ${LIBCSANITIZERFLAGS}
.endif

.if defined(CSHLIBFLAGS) && !empty(CSHLIBFLAGS)
MKSHLIBOBJS= yes
.else
MKSHLIBOBJS= no
.endif

# Avoid adding "-g" if we already have a "-g*" option.
.if (${MKDEBUG:Uno} != "no" && !defined(NODEBUG)) && \
    (!defined(CFLAGS) || empty(CFLAGS:M-g*)) && \
    (!defined(CXXFLAGS) || empty(CXXFLAGS:M-g*))
# We only add -g to the shared library objects
# because we don't currently split .a archives.
CSHLIBFLAGS+=	-g
.if ${LIBISPRIVATE} != "no"
CFLAGS+=	-g
.endif
.endif

# Platform-independent linker flags for ELF shared libraries
SHLIB_SOVERSION=	${SHLIB_MAJOR}
SHLIB_SHFLAGS=		-Wl,-soname,${_LIB}.so.${SHLIB_SOVERSION}
SHLIB_SHFLAGS+=		${SANITIZERFLAGS}
.if !defined(SHLIB_WARNTEXTREL) || ${SHLIB_WARNTEXTREL} != "no"
SHLIB_SHFLAGS+=		-Wl,--warn-shared-textrel
.endif
.if !defined(SHLIB_MKMAP) || ${SHLIB_MKMAP} != "no"
SHLIB_SHFLAGS+=		-Wl,-Map=${_LIB}.so.${SHLIB_SOVERSION}.map
.endif
CLEANFILES+=		${_LIB}.so.${SHLIB_SOVERSION}.map
SHLIB_LDSTARTFILE?=	${_GCC_CRTI} ${_GCC_CRTBEGINS}
SHLIB_LDENDFILE?=	${_GCC_CRTENDS} ${_GCC_CRTN}

CFLAGS+=	${COPTS}
OBJCFLAGS+=	${OBJCOPTS}
AFLAGS+=	${COPTS}
FFLAGS+=	${FOPTS}

.if defined(CTFCONVERT)
.if defined(CFLAGS) && !empty(CFLAGS:M*-g*)
CTFFLAGS+=	-g
.if defined(HAVE_GCC)
#CFLAGS+=	-gdwarf-2
.endif
.endif
.endif

LIBSTRIPAOBJS=	yes
.if !defined(CFLAGS) || empty(CFLAGS:M*-g*)
LIBSTRIPCOBJS=	yes
.endif
.if !defined(OBJCFLAGS) || empty(OBJCFLAGS:M*-g*)
LIBSTRIPOBJCOBJS=	yes
.endif
.if !defined(FFLAGS) || empty(FFLAGS:M*-g*)
LIBSTRIPFOBJS=	yes
.endif
.if !defined(CSHLIBFLAGS) || empty(CSHLIBFLAGS:M*-g*)
LIBSTRIPSHLIBOBJS=	yes
.endif

.c.o:
	${_MKTARGET_COMPILE}
	${COMPILE.c} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPCOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.c.po:
	${_MKTARGET_COMPILE}
	${COMPILE.c} ${PROFFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${PGFLAGS} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPCOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.c.go:
	${_MKTARGET_COMPILE}
	${COMPILE.c} ${DEBUGFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} -g ${.IMPSRC} -o ${.TARGET}

.c.pico:
	${_MKTARGET_COMPILE}
	${COMPILE.c} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${CSHLIBFLAGS} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPSHLIBOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.cc.o .cpp.o .cxx.o .C.o:
	${_MKTARGET_COMPILE}
	${COMPILE.cc} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPCOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.cc.po .cpp.po .cxx.po .C.po:
	${_MKTARGET_COMPILE}
	${COMPILE.cc} ${PROFFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${PGFLAGS} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPCOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.cc.go .cpp.go .cxx.go .C.go:
	${_MKTARGET_COMPILE}
	${COMPILE.cc} ${DEBUGFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} -g ${.IMPSRC} -o ${.TARGET}

.cc.pico .cpp.pico .cxx.pico .C.pico:
	${_MKTARGET_COMPILE}
	${COMPILE.cc} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${CSHLIBFLAGS} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPSHLIBOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.f.o:
	${_MKTARGET_COMPILE}
	${COMPILE.f} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPFOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.f.po:
	${_MKTARGET_COMPILE}
	${COMPILE.f} ${PROFFLAGS} ${PGFLAGS} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPFOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.f.go:
	${_MKTARGET_COMPILE}
	${COMPILE.f} ${DEBUGFLAGS} -g ${.IMPSRC} -o ${.TARGET}

.f.pico:
	${_MKTARGET_COMPILE}
	${COMPILE.f} ${PICFLAGS} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPFOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.f.ln:
	${_MKTARGET_COMPILE}
	@echo Skipping lint for Fortran libraries.

.m.o:
	${_MKTARGET_COMPILE}
	${COMPILE.m} ${OBJCOPTS.${.IMPSRC:T}} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPOBJCOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.m.po:
	${_MKTARGET_COMPILE}
	${COMPILE.m} ${PROFFLAGS} ${PGFLAGS} ${OBJCOPTS.${.IMPSRC:T}} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPOBJCOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.m.go:
	${_MKTARGET_COMPILE}
	${COMPILE.m} ${DEBUGFLAGS} -g ${OBJCOPTS.${.IMPSRC:T}} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPOBJCOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.m.pico:
	${_MKTARGET_COMPILE}
	${COMPILE.m} ${CSHLIBFLAGS} ${OBJCOPTS.${.IMPSRC:T}} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPOBJCOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.s.o:
	${_MKTARGET_COMPILE}
	${COMPILE.s} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPAOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.S.o:
	${_MKTARGET_COMPILE}
	${COMPILE.S} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPAOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.s.po:
	${_MKTARGET_COMPILE}
	${COMPILE.s} ${PROFFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPAOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.S.po:
	${_MKTARGET_COMPILE}
	${COMPILE.S} ${PROFFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} ${OBJECT_TARGET}
	${CTFCONVERT_RUN}
.if defined(LIBSTRIPAOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.s.go:
	${_MKTARGET_COMPILE}
	${COMPILE.s} ${DEBUGFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} -o ${.TARGET}

.S.go:
	${_MKTARGET_COMPILE}
	${COMPILE.S} ${DEBUGFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} -o ${.TARGET}

.s.pico:
	${_MKTARGET_COMPILE}
	${COMPILE.s} ${PICFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPAOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

.S.pico:
	${_MKTARGET_COMPILE}
	${COMPILE.S} ${PICFLAGS} ${COPTS.${.IMPSRC:T}} ${CPUFLAGS.${.IMPSRC:T}} ${CPPFLAGS.${.IMPSRC:T}} ${.IMPSRC} -o ${.TARGET}
.if defined(LIBSTRIPAOBJS)
	${OBJCOPY} ${OBJCOPYLIBFLAGS} ${.TARGET}
.endif

# Declare a few variables to make our life easier later.
_LIB:=${_LIB_PREFIX}${LIB}
_LIB.a:=${_LIB}.a
_LIB_p.a:=${_LIB}_p.a
_LIB_g.a:=${_LIB}_g.a
_LIB_pic.a:=${_LIB}_pic.a
_LIB.ln:=llib-l${LIB}.ln

.if ${MKPIC} != "no" && defined(SHLIB_FULLVERSION)
_LIB.so:=${_LIB}.so
_LIB.so.major:=${_LIB}.so.${SHLIB_MAJOR}
_LIB.so.full:=${_LIB}.so.${SHLIB_FULLVERSION}
_LIB.so.link:=${_LIB}.so.${SHLIB_FULLVERSION}.link
.if ${MKDEBUG:Uno} != "no" && !defined(NODEBUG)
_LIB.so.debug:=${_LIB.so.full}.debug
.endif
.endif

_DEST.LIB:=${DESTDIR}${LIBDIR}
_DEST.OBJ:=${DESTDIR}${_LIBSODIR}
_DEST.LINT:=${DESTDIR}${LINTLIBDIR}
_DEST.DEBUG:=${DESTDIR}${DEBUGDIR}${LIBDIR}
_DEST.ODEBUG:=${DESTDIR}${DEBUGDIR}${_LIBSODIR}

.if ${MKPIC} == "no" || (defined(LDSTATIC) && ${LDSTATIC} != "") \
    || ${MAKELINKLIB} != "no" || ${MAKESTATICLIB} != "no"
_BUILDSTATICLIB=yes
.else
_BUILDSTATICLIB=no
.endif

.if defined(LIB)							# {
.if ${_BUILDSTATICLIB} != "no"
_LIBS=${_LIB.a}
.else
_LIBS=
.endif

OBJS+=${SRCS:N*.h:N*.sh:R:S/$/.o/g}

STOBJS+=${OBJS}

LOBJS+=${LSRCS:.c=.ln} ${SRCS:M*.c:.c=.ln}

.if ${LIBISPRIVATE} != "no"
# No installation is required
libinstall::
.endif

.if ${MKDEBUGLIB} != "no"
_LIBS+=${_LIB_g.a}
GOBJS+=${OBJS:.o=.go}
DEBUGFLAGS?=-DDEBUG
.endif

.if ${MKPROFILE} != "no"
_LIBS+=${_LIB_p.a}
POBJS+=${OBJS:.o=.po}
PROFFLAGS?=-DGPROF -DPROF
.endif

.if ${MKPIC} != "no"							# {
.if ${MKPICLIB} == "no"
.if ${MKSHLIBOBJS} != "no"
# make _pic.a, which isn't really pic,
# since it's needed for making shared lib.
# but don't install it.
SOLIB=${_LIB_pic.a}
SOBJS+=${OBJS:.o=.pico}
.else
SOLIB=${_LIB.a}
.endif
.else
SOLIB=${_LIB_pic.a}
_LIBS+=${SOLIB}
SOBJS+=${OBJS:.o=.pico}
.endif
.if defined(SHLIB_FULLVERSION)
_LIBS+=${_LIB.so.full}
.endif
.endif									# }

.if ${MKLINT} != "no" && !empty(LOBJS)
_LIBS+=${_LIB.ln}
.endif

ALLOBJS=
.if ${_BUILDSTATICLIB} != "no"
ALLOBJS+=${STOBJS}
.endif
ALLOBJS+=${POBJS} ${SOBJS}
.if ${MKLINT} != "no" && !empty(LOBJS)
ALLOBJS+=${LOBJS}
.endif
.else	# !defined(LIB)							# } {
LOBJS=
SOBJS=
.endif	# !defined(LIB)							# }

_YLSRCS=	${SRCS:M*.[ly]:C/\..$/.c/} ${YHEADER:D${SRCS:M*.y:.y=.h}}

.NOPATH: ${ALLOBJS} ${_LIBS} ${_YLSRCS}

realall: ${SRCS} ${ALLOBJS:O} ${_LIBS} ${_LIB.so.debug}

.if ${MKARZERO} == "yes"
_ARFL=crsD
_ARRANFL=sD
_INSTRANLIB=
.else
_ARFL=crs
_ARRANFL=s
_INSTRANLIB=${empty(PRESERVE):?-a "${RANLIB} -t":}
.endif

# If you change this, please consider reflecting the change in
# the override in sys/rump/Makefile.rump.
.if !target(__archivebuild)
__archivebuild: .USE
	${_MKTARGET_BUILD}
	@rm -f ${.TARGET}
	${AR} ${_ARFL} ${.TARGET} $$(NM=${NM} ${LORDER} ${.ALLSRC:M*o} | ${TSORT})
.endif

.if !target(__archiveinstall)
__archiveinstall: .USE
	${_MKTARGET_INSTALL}
	${INSTALL_FILE} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${_INSTRANLIB} ${.ALLSRC} ${.TARGET}
.endif

__archivesymlinkpic: .USE
	${_MKTARGET_INSTALL}
	${INSTALL_SYMLINK} ${.ALLSRC} ${.TARGET}

.if !target(__buildstdlib)
__buildstdlib: .USE
	@echo building standard ${.TARGET:T:S/.o//:S/lib//} library
	@rm -f ${.TARGET}
	@${LINK.c:S/-nostdinc//} -nostdlib ${LDFLAGS} -Wno-unused-command-line-argument -r -o ${.TARGET} $$(NM=${NM} ${LORDER} ${.ALLSRC:M*o} | ${TSORT})
.endif

.if !target(__buildproflib)
__buildproflib: .USE
	@echo building profiled ${.TARGET:T:S/.o//:S/lib//} library
	${_MKTARGET_BUILD}
	@rm -f ${.TARGET}
	@${LINK.c:S/-nostdinc//} -nostdlib ${LDFLAGS} -r -o ${.TARGET} $$(NM=${NM} ${LORDER} ${.ALLSRC:M*po} | ${TSORT})
.endif

DPSRCS+=	${_YLSRCS}
CLEANFILES+=	${_YLSRCS}

${STOBJS} ${POBJS} ${GOBJS} ${SOBJS} ${LOBJS}: ${DPSRCS}

${_LIB.a}:: ${STOBJS} __archivebuild

${_LIB_p.a}:: ${POBJS} __archivebuild

${_LIB_pic.a}:: ${SOBJS} __archivebuild

${_LIB_g.a}:: ${GOBJS} __archivebuild


_LIBLDOPTS=
.if ${SHLIBDIR} != "/usr/lib"
_LIBLDOPTS+=	-Wl,-rpath,${SHLIBDIR} \
		-L=${SHLIBDIR}
.elif ${SHLIBINSTALLDIR} != "/usr/lib"
_LIBLDOPTS+=	-Wl,-rpath-link,${DESTDIR}${SHLIBINSTALLDIR} \
		-L=${SHLIBINSTALLDIR}
.endif
.if ${MKSTRIPSYM} != "no"
_LIBLDOPTS+=	-Wl,-x
.else
_LIBLDOPTS+=	-Wl,-X
.endif

# gcc -shared now adds -lc automatically. For libraries other than libc and
# libgcc* we add as a dependency the installed shared libc. For libc and
# libgcc* we avoid adding libc as a dependency by using -nostdlib. Note that
# -Xl,-nostdlib is not enough because we want to tell the compiler-driver not
# to add standard libraries, not the linker.
.if !defined(LIB)
.if !empty(LIBC_SO)
DPLIBC ?= ${DESTDIR}${LIBC_SO}
.endif
.else
.if ${LIB} != "c" && ${LIB:Mgcc*} == ""
.if !empty(LIBC_SO)
DPLIBC ?= ${DESTDIR}${LIBC_SO}
.endif
.else
LDLIBC ?= -nodefaultlibs
.if ${HAVE_LIBGCC} == "yes" && ${LIB} == "c"
LDADD+= -lgcc
.endif
.endif
.endif

.if ${LIBISCXX} != "no"
LIBCC:=	${CXX}
. if ${MKLIBCXX} == "yes"
LIBDPLIBS+=     c++	${.CURDIR}/../../../../../external/bsd/libc++/lib
. else
LIBDPLIBS+=     stdc++	${.CURDIR}/../../../../../external/gpl3/${EXTERNAL_GCC_SUBDIR}/lib/libstdc++-v3
. endif
.else
LIBCC:=	${CC}
.endif

# VERSION_MAP
#
#	Path to an ld version script to use when linking the library.
#	Resolved from .PATH like a target prerequisite.
#
#	Implemented by adding -Wl,--version-script=${${VERSION_MAP}:P}
#	to LDFLAGS, and by adding ${VERSION_MAP} to DPADD to make it a
#	target prerequisite so :P works.
#
.if !empty(VERSION_MAP)
DPADD+=			${VERSION_MAP}
LDFLAGS+=		-Wl,--version-script=${${VERSION_MAP}:P}
.endif

_LDADD.${_LIB}=	${LDADD} ${LDADD.${_LIB}}
_LDFLAGS.${_LIB}=	${LDFLAGS} ${LDFLAGS.${_LIB}}

_MAINLIBDEPS=	${SOLIB} ${DPADD} ${DPLIBC} \
		${SHLIB_LDSTARTFILE} ${SHLIB_LDENDFILE}

.if defined(_LIB.so.debug)
${_LIB.so.debug}: ${_LIB.so.link}
	${_MKTARGET_CREATE}
	( ${OBJCOPY} --only-keep-debug --compress-debug-sections \
	    ${_LIB.so.link} ${_LIB.so.debug} \
	) || (rm -f ${.TARGET}; false)
${_LIB.so.full}: ${_LIB.so.link} ${_LIB.so.debug}
	${_MKTARGET_CREATE}
	(  ${OBJCOPY} --strip-debug -p -R .gnu_debuglink \
	    --add-gnu-debuglink=${_LIB.so.debug} \
	    ${_LIB.so.link} ${_LIB.so.full}.tmp && \
	    ${MV} ${_LIB.so.full}.tmp ${_LIB.so.full} \
	) || (rm -f ${.TARGET}; false)
${_LIB.so.link}: ${_MAINLIBDEPS}
.else # aka no MKDEBUG
${_LIB.so.full}: ${_MAINLIBDEPS}
.endif
	${_MKTARGET_BUILD}
	rm -f ${.TARGET}
	${LIBCC} ${LDLIBC} -shared ${SHLIB_SHFLAGS} \
	    ${_LDFLAGS.${_LIB}} -o ${.TARGET}.tmp ${_LIBLDOPTS} \
	    -Wl,--whole-archive ${SOLIB} \
	    -Wl,--no-whole-archive ${_LDADD.${_LIB}}
.if ${MKSTRIPIDENT} != "no"
	${OBJCOPY} -R .ident ${.TARGET}.tmp
.endif
	${MV} ${.TARGET}.tmp ${.TARGET}
#  We don't use INSTALL_SYMLINK here because this is just
#  happening inside the build directory/objdir. XXX Why does
#  this spend so much effort on libraries that aren't live??? XXX
#  XXX Also creates dead symlinks until the .full rule runs
#  above and creates the main link
.if defined(SHLIB_FULLVERSION) && defined(SHLIB_MAJOR) && \
    "${SHLIB_FULLVERSION}" != "${SHLIB_MAJOR}"
	${HOST_LN} -sf ${_LIB.so.full} ${_LIB.so.major}.tmp
	${MV} ${_LIB.so.major}.tmp ${_LIB.so.major}
.endif
	${HOST_LN} -sf ${_LIB.so.full} ${_LIB.so}.tmp
	${MV} ${_LIB.so}.tmp ${_LIB.so}

# If there's a file listing expected symbols, fail if the diff from it
# to the actual symbols is nonempty, and show the diff in that case.
.if exists(${.CURDIR}/${LIB}.${LIBC_MACHINE_ARCH:U${MACHINE_ARCH}}.expsym)
LIB_EXPSYM?=	${LIB}.${LIBC_MACHINE_ARCH:U${MACHINE_ARCH}}.expsym
.elif exists(${.CURDIR}/${LIB}.${LIBC_MACHINE_CPU:U${MACHINE_CPU}}.expsym)
LIB_EXPSYM?=	${LIB}.${LIBC_MACHINE_CPU:U${MACHINE_CPU}}.expsym
.elif exists(${.CURDIR}/${LIB}.expsym)
LIB_EXPSYM?=	${LIB}.expsym
.endif

# If we don't have a version map enumerating the exact symbols
# exported, skip various machine-dependent crud that the linker
# automatically exports (even though it appears to be unnecessary, as
# demonstrated by libraries with version scripts which don't export
# these symbols).
#
# This list has been gathered empirically -- I'm not sure it's written
# down anywhere and I'm not sure there's any way to ask the linker to
# simply not export the symbols.
.if !empty(VERSION_MAP)
_EXPSYM_PIPE_GREP=	# empty
.else
_EXPSYM_PIPE_GREP=	| grep -Fvx ${_EXPSYM_IGNORE:@_s_@-e ${_s_:Q}@}
_EXPSYM_IGNORE+=		_end
_EXPSYM_IGNORE+=		_fini
_EXPSYM_IGNORE+=		_init
_EXPSYM_IGNORE.aarch64+=	__bss_end__
_EXPSYM_IGNORE.aarch64+=	__bss_start__
_EXPSYM_IGNORE.aarch64+=	__end__
_EXPSYM_IGNORE.aarch64+=	_bss_end__
_EXPSYM_IGNORE.hppa+=		_GLOBAL_OFFSET_TABLE_
_EXPSYM_IGNORE.powerpc64+=	._fini
_EXPSYM_IGNORE.powerpc64+=	._init
_EXPSYM_IGNORE.sh3+=		___ctors
_EXPSYM_IGNORE.sh3+=		___ctors_end
_EXPSYM_IGNORE.sh3+=		___dtors
_EXPSYM_IGNORE.sh3+=		___dtors_end
_EXPSYM_IGNORE+=		${_EXPSYM_IGNORE.${MACHINE_ARCH}}
.  if ${MACHINE_ARCH} != ${MACHINE_CPU}
_EXPSYM_IGNORE+=		${_EXPSYM_IGNORE.${MACHINE_CPU}}
.  endif
.endif

.if !empty(LIB_EXPSYM) && ${MKPIC} != "no"
realall: ${_LIB.so.full}.diffsym
${_LIB.so.full}.diffsym: ${LIB_EXPSYM} ${_LIB.so.full}.actsym
	${_MKTARGET_CREATE}
	if diff -u ${.ALLSRC} >${.TARGET}.tmp; then \
		${MV} ${.TARGET}.tmp ${.TARGET}; \
	else \
		ret=$$?; \
		cat ${.TARGET}.tmp; \
		echo ${_LIB.so.full}: error: \
			actual symbols differ from expected symbols >&2; \
		exit $$ret; \
	fi
${_LIB.so.full}.actsym: ${_LIB.so.full}
	${_MKTARGET_CREATE}
	${NM} --dynamic --extern-only --defined-only --with-symbol-versions \
		${.ALLSRC} \
	| cut -d' ' -f3	${_EXPSYM_PIPE_GREP} | LC_ALL=C sort -u >${.TARGET}.tmp
	${MV} ${.TARGET}.tmp ${.TARGET}
CLEANFILES+=	${_LIB.so.full}.actsym
CLEANFILES+=	${_LIB.so.full}.actsym.tmp
CLEANFILES+=	${_LIB.so.full}.diffsym
CLEANFILES+=	${_LIB.so.full}.diffsym.tmp
update-symbols: .PHONY
update-symbols: ${_LIB.so.full}.actsym
	@if [ -f ${${LIB_EXPSYM}:P:Q} ] && \
	    [ -n "`comm -23 ${${LIB_EXPSYM}:P:Q} ${.ALLSRC:Q}`" ]; then \
		echo 'WARNING: Symbols deleted, major bump required!' >&2; \
	elif [ -f ${${LIB_EXPSYM}:P:Q} ] && \
	    [ -n "`comm -13 ${${LIB_EXPSYM}:P:Q} ${.ALLSRC:Q}`" ]; then \
		echo 'WARNING: Symbols added, minor bump required!' >&2; \
	fi
	cp ${.ALLSRC} ${defined(NETBSDSRCDIR_RW):?${${LIB_EXPSYM}:P:C,^${NETBSDSRCDIR}/,${NETBSDSRCDIR_RW}/,}:${${LIB_EXPSYM}:P}}
.endif

.if !empty(LOBJS)							# {
LLIBS?=		-lc
${_LIB.ln}: ${LOBJS}
	${_MKTARGET_COMPILE}
	rm -f ${.TARGET}
.if defined(DESTDIR)
	${LINT} -C${LIB} ${.ALLSRC} -L${DESTDIR}/usr/libdata ${LLIBS}
.else
	${LINT} -C${LIB} ${.ALLSRC} ${LLIBS}
.endif
.endif									# }

# Only intended to be invoked manually, not as part of dependall.
lint: ${LOBJS}
.if defined(LOBJS) && !empty(LOBJS)
	${LINT} ${LINTFLAGS} -Cmanual-lint ${LOBJS}
.endif


# If the number of entries in CLEANFILES is too large, then the
# commands in bsd.clean.mk encounter errors like "exec(/bin/sh)
# failed (Argument list too long)".  Avoid that by splitting the
# files to clean into several lists using different variable names.
# __cleanuse is an internal target in bsd.clean.mk; the way we
# use it here mimics the way it's used by the clean target in
# bsd.clean.mk.
#
clean: libclean1 libclean2 libclean3 libclean4 libclean5
libclean1: .PHONY .MADE __cleanuse LIBCLEANFILES1
libclean2: .PHONY .MADE __cleanuse LIBCLEANFILES2
libclean3: .PHONY .MADE __cleanuse LIBCLEANFILES3
libclean4: .PHONY .MADE __cleanuse LIBCLEANFILES4
libclean5: .PHONY .MADE __cleanuse LIBCLEANFILES5
CLEANFILES+= a.out [Ee]rrs mklog core *.core
LIBCLEANFILES1+= ${_LIB.a}   ${STOBJS} ${STOBJS:=.tmp}
LIBCLEANFILES2+= ${_LIB_p.a} ${POBJS}  ${POBJS:=.tmp}
LIBCLEANFILES3+= ${_LIB_g.a} ${GOBJS}  ${GOBJS:=.tmp}
LIBCLEANFILES4+= ${_LIB_pic.a}
.if ${MKPIC} != "no" && defined(SHLIB_FULLVERSION)
LIBCLEANFILES4+= ${_LIB.so}.* ${_LIB.so} ${_LIB.so.debug}
.endif
LIBCLEANFILES4+= ${SOBJS} ${SOBJS:=.tmp}
LIBCLEANFILES5+= ${_LIB.ln} ${LOBJS} llib-lmanual-lint.ln

.if !target(libinstall)							# {
# Make sure it gets defined, in case MKPIC==no && MKLINKLIB==no
libinstall::

.if ${MAKELINKLIB} != "no" && ${MAKESTATICLIB} != "no" && ${LINKINSTALL} != "no"
libinstall:: ${_DEST.LIB}/${_LIB.a}
.PRECIOUS: ${_DEST.LIB}/${_LIB.a}

.if ${MKUPDATE} == "no"
.if !defined(BUILD) && !make(all) && !make(${_LIB.a})
${_DEST.LIB}/${_LIB.a}! .MADE
.endif
${_DEST.LIB}/${_LIB.a}! ${_LIB.a} __archiveinstall
.else
.if !defined(BUILD) && !make(all) && !make(${_LIB.a})
${_DEST.LIB}/${_LIB.a}: .MADE
.endif
${_DEST.LIB}/${_LIB.a}: ${_LIB.a} __archiveinstall
.endif
.endif

.if ${MKPROFILE} != "no"
libinstall:: ${_DEST.LIB}/${_LIB_p.a}
.PRECIOUS: ${_DEST.LIB}/${_LIB_p.a}

.if ${MKUPDATE} == "no"
.if !defined(BUILD) && !make(all) && !make(${_LIB_p.a})
${_DEST.LIB}/${_LIB_p.a}! .MADE
.endif
${_DEST.LIB}/${_LIB_p.a}! ${_LIB_p.a} __archiveinstall
.else
.if !defined(BUILD) && !make(all) && !make(${_LIB_p.a})
${_DEST.LIB}/${_LIB_p.a}: .MADE
.endif
${_DEST.LIB}/${_LIB_p.a}: ${_LIB_p.a} __archiveinstall
.endif
.endif

.if ${MKDEBUGLIB} != "no"
libinstall:: ${_DEST.LIB}/${_LIB_g.a}
.PRECIOUS: ${_DEST.LIB}/${_LIB_g.a}

.if ${MKUPDATE} == "no"
.if !defined(BUILD) && !make(all) && !make(${_LIB_g.a})
${_DEST.LIB}/${_LIB_g.a}! .MADE
.endif
${_DEST.LIB}/${_LIB_g.a}! ${_LIB_g.a} __archiveinstall
.else
.if !defined(BUILD) && !make(all) && !make(${LIB_g.a})
${_DEST.LIB}/${_LIB_g.a}: .MADE
.endif
${_DEST.LIB}/${_LIB_g.a}: ${_LIB_g.a} __archiveinstall
.endif
.endif

.if ${MKPIC} != "no" && ${MKPICINSTALL} != "no"
libinstall:: ${_DEST.LIB}/${_LIB_pic.a}
.PRECIOUS: ${_DEST.LIB}/${_LIB_pic.a}

.if ${MKUPDATE} == "no"
.if !defined(BUILD) && !make(all) && !make(${_LIB_pic.a})
${_DEST.LIB}/${_LIB_pic.a}! .MADE
.endif
.if ${MKPICLIB} == "no"
${_DEST.LIB}/${_LIB_pic.a}! ${_LIB.a} __archivesymlinkpic
.else
${_DEST.LIB}/${_LIB_pic.a}! ${_LIB_pic.a} __archiveinstall
.endif
.else
.if !defined(BUILD) && !make(all) && !make(${_LIB_pic.a})
${_DEST.LIB}/${_LIB_pic.a}: .MADE
.endif
.if ${MKPICLIB} == "no"
${_DEST.LIB}/${_LIB_pic.a}: ${_LIB.a} __archivesymlinkpic
.else
${_DEST.LIB}/${_LIB_pic.a}: ${_LIB_pic.a} __archiveinstall
.endif
.endif
.endif

.if ${MKPIC} != "no" && defined(SHLIB_FULLVERSION)

libinstall:: ${_DEST.OBJ}/${_LIB.so.full}
.PRECIOUS: ${_DEST.OBJ}/${_LIB.so.full}

.if ${MKUPDATE} == "no"
.if !defined(BUILD) && !make(all) && !make(${_LIB.so.full})
${_DEST.OBJ}/${_LIB.so.full}! .MADE
.endif
${_DEST.OBJ}/${_LIB.so.full}! ${_LIB.so.full}
.else
.if !defined(BUILD) && !make(all) && !make(${_LIB.so.full})
${_DEST.OBJ}/${_LIB.so.full}: .MADE
.endif
${_DEST.OBJ}/${_LIB.so.full}: ${_LIB.so.full}
.endif
	${_MKTARGET_INSTALL}
	${INSTALL_FILE} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${.ALLSRC} ${.TARGET}
.if ${_LIBSODIR} != ${LIBDIR}
	${INSTALL_SYMLINK} -l r ${_DEST.OBJ}/${_LIB.so.full} \
	    ${_DEST.LIB}/${_LIB.so.full}
.endif
.if defined(SHLIB_FULLVERSION) && defined(SHLIB_MAJOR) && \
    "${SHLIB_FULLVERSION}" != "${SHLIB_MAJOR}"
	${INSTALL_SYMLINK} ${_LIB.so.full} ${_DEST.OBJ}/${_LIB.so.major}
.if ${_LIBSODIR} != ${LIBDIR}
	${INSTALL_SYMLINK} -l r ${_DEST.OBJ}/${_LIB.so.full} \
	    ${_DEST.LIB}/${_LIB.so.major}
.endif
.endif
.if ${MAKELINKLIB} != "no" && ${LINKINSTALL} != "no"
	${INSTALL_SYMLINK}  ${_LIB.so.full} ${_DEST.OBJ}/${_LIB.so}
.if ${_LIBSODIR} != ${LIBDIR}
	${INSTALL_SYMLINK} -l r ${_DEST.OBJ}/${_LIB.so.full} \
	    ${_DEST.LIB}/${_LIB.so}
.endif
.endif
.endif

.if defined(_LIB.so.debug)
libinstall:: ${_DEST.DEBUG}/${_LIB.so.debug}
.PRECIOUS: ${_DEST.DEBUG}/${_LIB.so.debug}

${_DEST.DEBUG}/${_LIB.so.debug}: ${_LIB.so.debug}
	${_MKTARGET_INSTALL}
	${INSTALL_FILE} -o ${DEBUGOWN} -g ${DEBUGGRP} -m ${DEBUGMODE} \
	    ${.ALLSRC} ${.TARGET}
.if ${_LIBSODIR} != ${LIBDIR}
	${INSTALL_SYMLINK} -l r ${_DEST.DEBUG}/${_LIB.so.debug} \
	    ${_DEST.ODEBUG}/${_LIB.so.debug}
.endif
.endif

.if ${_LINTINSTALL} != "no" && !empty(LOBJS)
libinstall:: ${_DEST.LINT}/${_LIB.ln}
.PRECIOUS: ${_DEST.LINT}/${_LIB.ln}

.if ${MKUPDATE} == "no"
.if !defined(BUILD) && !make(all) && !make(${_LIB.ln})
${_DEST.LINT}/${_LIB.ln}! .MADE
.endif
${_DEST.LINT}/${_LIB.ln}! ${_LIB.ln}
.else
.if !defined(BUILD) && !make(all) && !make(${_LIB.ln})
${_DEST.LINT}/${_LIB.ln}: .MADE
.endif
${_DEST.LINT}/${_LIB.ln}: ${_LIB.ln}
.endif
	${_MKTARGET_INSTALL}
	${INSTALL_FILE} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
		${.ALLSRC} ${_DEST.LINT}
.endif
.endif	# !target(libinstall)						# }

##### Pull in related .mk logic
LINKSOWN?= ${LIBOWN}
LINKSGRP?= ${LIBGRP}
LINKSMODE?= ${LIBMODE}
.include <bsd.man.mk>
.include <bsd.nls.mk>
.include <bsd.files.mk>
.include <bsd.inc.mk>
.include <bsd.links.mk>
.include <bsd.dep.mk>
.include <bsd.clang-analyze.mk>
.include <bsd.clean.mk>

${TARGETS}:	# ensure existence
