NEXTV =		03

DISTFILES =	Makefile \
		pkg_fcgi-01.pub \
		pkg_fcgi-02.pub

all:
	false

newkey:
	signify -G -p pkg_fcgi-${NEXTV}.pub -s pkg_fcgi-${NEXTV}.sec

dist: ${DISTFILES}
	mkdir -p ${DESTDIR}/
	${INSTALL} -m 0644 ${DISTFILES} ${DESTDIR}/

.PHONY: all dist
include ../config.mk
