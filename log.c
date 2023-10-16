/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 */

#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include "log.h"

__dead void	log_syslog_fatal(int, const char *, ...);
__dead void	log_syslog_fatalx(int, const char *, ...);
void		log_syslog_warn(const char *, ...);
void		log_syslog_warnx(const char *, ...);
void		log_syslog_info(const char *, ...);
void		log_syslog_debug(const char *, ...);

const struct logger syslogger = {
	.fatal =	&log_syslog_fatal,
	.fatalx =	&log_syslog_fatalx,
	.warn =		&log_syslog_warn,
	.warnx =	&log_syslog_warnx,
	.info =		&log_syslog_info,
	.debug =	&log_syslog_debug,
};

const struct logger dbglogger = {
	.fatal =	&err,
	.fatalx =	&errx,
	.warn =		&warn,
	.warnx =	&warnx,
	.info =		&warnx,
	.debug =	&warnx,
};

const struct logger *logger = &dbglogger;

static char logbuf[4096];
static int debug;
static int verbose;

void
log_init(int n_debug, int facility)
{
	debug = n_debug;
	verbose = n_debug;

	tzset();
	if (debug)
		setvbuf(stderr, logbuf, _IOLBF, sizeof(logbuf));
	else {
		openlog(getprogname(), LOG_PID | LOG_NDELAY, facility);
		logger = &syslogger;
	}
}

void
log_setverbose(int v)
{
	verbose = v;
}

__dead void
log_syslog_fatal(int eval, const char *fmt, ...)
{
	static char	 s[BUFSIZ];
	va_list		 ap;
	int		 r, save_errno;

	save_errno = errno;

	va_start(ap, fmt);
	r = vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);

	errno = save_errno;

	if (r > 0 && (size_t)r <= sizeof(s))
		syslog(LOG_DAEMON|LOG_CRIT, "%s: %s", s, strerror(errno));

	exit(eval);
}

__dead void
log_syslog_fatalx(int eval, const char *fmt, ...)
{
	va_list		 ap;

	va_start(ap, fmt);
	vsyslog(LOG_DAEMON|LOG_CRIT, fmt, ap);
	va_end(ap);

	exit(eval);
}

void
log_syslog_warn(const char *fmt, ...)
{
	static char	 s[BUFSIZ];
	va_list		 ap;
	int		 r, save_errno;

	save_errno = errno;

	va_start(ap, fmt);
	r = vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);

	errno = save_errno;

	if (r > 0 && (size_t)r < sizeof(s))
		syslog(LOG_DAEMON|LOG_ERR, "%s: %s", s, strerror(errno));

	errno = save_errno;
}

void
log_syslog_warnx(const char *fmt, ...)
{
	va_list		 ap;
	int		 save_errno;

	save_errno = errno;
	va_start(ap, fmt);
	vsyslog(LOG_DAEMON|LOG_ERR, fmt, ap);
	va_end(ap);
	errno = save_errno;
}

void
log_syslog_info(const char *fmt, ...)
{
	va_list		 ap;
	int		 save_errno;

	if (verbose < 1)
		return;

	save_errno = errno;
	va_start(ap, fmt);
	vsyslog(LOG_DAEMON|LOG_INFO, fmt, ap);
	va_end(ap);
	errno = save_errno;
}

void
log_syslog_debug(const char *fmt, ...)
{
	va_list		 ap;
	int		 save_errno;

	if (verbose < 2)
		return;

	save_errno = errno;
	va_start(ap, fmt);
	vsyslog(LOG_DAEMON|LOG_DEBUG, fmt, ap);
	va_end(ap);
	errno = save_errno;
}
