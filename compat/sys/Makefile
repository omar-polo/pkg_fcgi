DISTFILES =	Makefile \
		queue.h \
		tree.h

all:
	false

dist: ${DISTFILES}
	mkdir -p ${DESTDIR}/
	${INSTALL} -m 0644 ${DISTFILES} ${DESTDIR}/

.PHONY: all dist
include ../../config.mk
