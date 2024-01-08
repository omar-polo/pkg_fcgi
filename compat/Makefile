DISTFILES =	Makefile \
		err.c \
		event.h \
		freezero.c \
		getdtablecount.c \
		getdtablesize.c \
		getprogname.c \
		pledge.c \
		reallocarray.c \
		recallocarray.c \
		setproctitle.c \
		setresgid.c \
		setresuid.c \
		stdlib.h \
		string.h \
		strlcat.c \
		strlcpy.c \
		strtonum.c \
		unistd.h \
		unveil.c \
		vasprintf.c

all:
	false

dist: ${DISTFILES}
	mkdir -p ${DESTDIR}/
	${INSTALL} -m 0644 ${DISTFILES} ${DESTDIR}/
	${MAKE} -C sys DESTDIR=${DESTDIR}/sys dist

.PHONY: all dist
include ../config.mk
