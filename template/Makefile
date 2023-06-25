PROG =		template
SRCS =		template.c y.tab.c
OBJS =		${SRCS:.c=.o} ${COBJS}

DISTFILES =	Makefile \
		configure \
		parse.y \
		template.c \
		tmpl.c \
		tmpl.h \
		y.tab.c

all: ${PROG}

include config.mk

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS}

y.tab.c: parse.y
	${YACC} -b y parse.y

clean:
	rm -rf *.o y.tab.* ${PROG}

distclean: clean
	rm -f config.h config.h.old config.mk config.log config.log.old

dist: ${DISTFILES}
	mkdir -p ${DESTDIR}/
	${INSTALL} -m 0644 ${DISTFILES} ${DESTDIR}/

.PHONY: all clean distclean dist
