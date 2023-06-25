include config.mk

# -- options --

PREFIX =	/usr/local
SBINDIR =	${PREFIX}/sbin
MANDIR =	${PREFIX}/man
WWWDIR =	/var/www/htdocs

# -- build-related variables --

PROG =		pkg_fcgi
VERSION =	0.1
DISTNAME =	${PROG}-${VERSION}

SRCS =		pkg_fcgi.c fcgi.c log.c server.c xmalloc.c

COBJS =		${COMPATS:.c=.o}
OBJS =		${SRCS:.c=.o} ${COBJS}

MAN =		${PROG}.conf.5 ${PROG}.8

# -- public targets --

all: ${PROG}
.PHONY: all clean distclean install uninstall

clean:
	rm -f *.[do] compat/*.[do] tests/*.[do] ui.c ${PROG}
#	${MAKE} -C template clean

distclean: clean
	rm -f config.h config.h.old config.mk config.log config.log.old
#	${MAKE} -C template distclean

install:
	mkdir -p ${DESTDIR}${MANDIR}/man5
	mkdir -p ${DESTDIR}${MANDIR}/man8
	mkdir -p ${DESTDIR}${SBINDIR}
	mkdir -p ${DESTDIR}${WWWDIR}
	${INSTALL_MAN} galileo.conf.5 ${DESTDIR}${MANDIR}/man5/${PROG}.conf.5
	${INSTALL_MAN} galileo.8 ${DESTDIR}${MANDIR}/man8/${PROG}.8
	${INSTALL_PROGRAM} ${PROG} ${DESTDIR}${SBINDIR}
	${INSTALL_DATA} galileo.css ${DESTDIR}${WWWDIR}

uninstall:
	rm ${DESTDIR}${MANDIR}/man5/${PROG}.conf.5
	rm ${DESTDIR}${MANDIR}/man8/${PROG}.8
	rm ${DESTDIR}${SBINDIR}/${PROG}
	rm ${DESTDIR}${WWWDIR}/galileo.css

# -- internal build targets --

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LIBS} ${LDFLAGS}

#ui.c: ui.tmpl
#	${MAKE} -C template
#	./template/template -o $@ ui.tmpl

.c.o:
	${CC} ${CFLAGS} -c $< -o $@

# -- maintainer targets --

PRIVKEY =	set-PRIVKEY
DISTFILES =	CHANGES \
		Makefile \
		README \
		configure \
		fcgi.c \
		pkg_fcgi.8 \
		pkg_fcgi.c \
		pkg_fcgi.css \
		pkg_fcgi.h \
		log.c \
		log.h \
		server.c \
		ui.c \
		ui.tmpl \
		xmalloc.c \
		xmalloc.h \

.PHONY: release dist

release: ${DISTNAME}.sha256.sig
dist: ${DISTNAME}.sha256

${DISTNAME}.sha256.sig: ${DISTNAME}.sha256
	signify -S -e -m ${DISTNAME}.sha256 -s ${PRIVKEY}

${DISTNAME}.sha256: ${DISTNAME}.tar.gz
	sha256 ${DISTNAME}.tar.gz > $@

${DISTNAME}.tar.gz: ${DISTFILES}
	mkdir -p .dist/${DISTNAME}/
	${INSTALL} -m 0644 ${DISTFILES} .dist/${DISTNAME}
	${MAKE} -C compat	DESTDIR=${PWD}/.dist/${DISTNAME}/compat dist
	${MAKE} -C keys		DESTDIR=${PWD}/.dist/${DISTNAME}/keys dist
	${MAKE} -C template	DESTDIR=${PWD}/.dist/${DISTNAME}/template dist
	${MAKE} -C tests	DESTDIR=${PWD}/.dist/${DISTNAME}/tests dist
	cd .dist/${DISTNAME} && chmod 755 configure template/configure
	cd .dist && tar czf ../$@ ${DISTNAME}
	rm -rf .dist/

.PHONY: ${DISTNAME}.tar.gz

# -- dependencies --

-include fcgi.d
-include log.d
-include pgk_fcgi.d
-include server.d
-include ui.d
-include xmalloc.d
