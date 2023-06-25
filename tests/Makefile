DISTFILES =	Makefile \
		MMD.c \
		WAIT_ANY.c \
		__progname.c \
		accept4.c \
		asr_run.c \
		bufferevent_read_pressure_cb.c \
		err.c \
		event_asr_run.c \
		freezero.c \
		getdtablecount.c \
		getdtablesize.c \
		getexecname.c \
		getprogname.c \
		imsg.c \
		libevent.c \
		libevent2.c \
		libsqlite3.c \
		libtls.c \
		pledge.c \
		reallocarray.c \
		recallocarray.c \
		setgroups.c \
		setproctitle.c \
		setresgid.c \
		setresuid.c \
		strlcat.c \
		strlcpy.c \
		strtonum.c \
		sys_queue.c \
		sys_tree.c \
		unveil.c \
		vasprintf.c

all:
	false

dist: ${DISTFILES}
	mkdir -p ${DESTDIR}/
	${INSTALL} -m 0644 ${DISTFILES} ${DESTDIR}/

.PHONY: all dist
include ../config.mk
