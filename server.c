/*
 * Copyright (c) 2022 Omar Polo <op@omarpolo.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/tree.h>

#include <ctype.h>
#include <event.h>
#include <fnmatch.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "log.h"
#include "pkg.h"

#if template
#include "tmpl.h"
#endif

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

char		dbpath[PATH_MAX];

void		server_sig_handler(int, short, void *);
void		server_open_db(struct env *);
void		server_close_db(struct env *);
__dead void	server_shutdown(struct env *);
int		server_reply(struct client *, int, const char *);

int		route_dispatch(struct env *, struct client *);
int		route_home(struct env *, struct client *);
int		route_search(struct env *, struct client *);
int		route_categories(struct env *, struct client *);
int		route_listing(struct env *, struct client *);
int		route_port(struct env *, struct client *);

typedef int (*route_t)(struct env *, struct client *);

static const struct route {
	const char	*r_path;
	route_t		 r_fn;
} routes[] = {
	{ "/",		route_home },
	{ "/search",	route_search },
	{ "/all",	route_categories },
	{ "/*",		route_port },
};

void
server_sig_handler(int sig, short ev, void *arg)
{
	struct env	*env = arg;

	/*
	 * Normal signal handler rules don't apply because libevent
	 * decouples for us.
	 */

	switch (sig) {
	case SIGHUP:
		log_info("re-opening the db");
		server_close_db(env);
		server_open_db(env);
		break;
	case SIGTERM:
	case SIGINT:
		server_shutdown(env);
		break;
	default:
		fatalx("unexpected signal %d", sig);
	}
}

static inline void
loadstmt(sqlite3 *db, sqlite3_stmt **stmt, const char *sql)
{
	int		 err;

	err = sqlite3_prepare_v2(db, sql, -1, stmt, NULL);
	if (err != SQLITE_OK)
		fatalx("failed prepare statement \"%s\": %s",
		    sql, sqlite3_errstr(err));
}

void
server_open_db(struct env *env)
{
	int		 err;

	err = sqlite3_open_v2(dbpath, &env->env_db,
	    SQLITE_OPEN_READONLY, NULL);
	if (err != SQLITE_OK)
		fatalx("can't open database %s: %s", dbpath,
		    sqlite3_errmsg(env->env_db));

	/* load prepared statements */
	loadstmt(env->env_db, &env->env_qsearch,
	    "select webpkg_fts.pkgstem, webpkg_fts.comment, paths.fullpkgpath"
	    " from webpkg_fts"
	    " join _ports p on p.fullpkgpath = webpkg_fts.id"
	    " join _paths paths on paths.id = webpkg_fts.id"
	    " where webpkg_fts match ?"
	    " order by bm25(webpkg_fts)");

	loadstmt(env->env_db, &env->env_qfullpkgpath,
	    "select p.fullpkgpath, pp.pkgstem, pp.comment, pp.pkgname,"
	    "       d.value, e.value, r.value, pp.homepage"
	    " from _paths p"
	    " join _descr d on d.fullpkgpath = p.id"
	    " join _ports pp on pp.fullpkgpath = p.id"
	    " join _email e on e.keyref = pp.maintainer"
	    " left join _readme r on r.fullpkgpath = p.id"
	    " where p.fullpkgpath = ?");

	loadstmt(env->env_db, &env->env_qcats,
	    "select distinct value from categories order by value");

	loadstmt(env->env_db, &env->env_qbycat,
	    "select fullpkgpath from categories where value = ?"
	    " order by fullpkgpath");
}

void
server_close_db(struct env *env)
{
	int		 err;

	sqlite3_finalize(env->env_qsearch);
	sqlite3_finalize(env->env_qfullpkgpath);
	sqlite3_finalize(env->env_qcats);
	sqlite3_finalize(env->env_qbycat);

	if ((err = sqlite3_close(env->env_db)) != SQLITE_OK)
		log_warnx("sqlite3_close %s", sqlite3_errstr(err));
}

int
server_main(const char *db)
{
	struct env	 env;
	struct event	 sighup;
	struct event	 sigint;
	struct event	 sigterm;

	signal(SIGPIPE, SIG_IGN);

	memset(&env, 0, sizeof(env));

	if (pledge("stdio rpath flock unix", NULL) == -1)
		fatal("pledge");

	if (realpath(db, dbpath) == NULL)
		fatal("realpath %s", db);

	server_open_db(&env);

	event_init();

	env.env_sockfd = 3;

	event_set(&env.env_sockev, env.env_sockfd, EV_READ | EV_PERSIST,
	    fcgi_accept, &env);
	event_add(&env.env_sockev, NULL);

	evtimer_set(&env.env_pausev, fcgi_accept, &env);

	signal_set(&sighup, SIGHUP, server_sig_handler, &env);
	signal_set(&sigint, SIGINT, server_sig_handler, &env);
	signal_set(&sigterm, SIGTERM, server_sig_handler, &env);

	signal_add(&sighup, NULL);
	signal_add(&sigint, NULL);
	signal_add(&sigterm, NULL);

	log_info("ready");
	event_dispatch();

	server_shutdown(&env);
}

void __dead
server_shutdown(struct env *env)
{
	log_info("shutting down");
	server_close_db(env);
	exit(0);
}

int
server_reply(struct client *clt, int status, const char *ctype)
{
	if (clt_printf(clt, "%02d %s\r\n", status, ctype) == -1)
		return (-1);
	return (0);
}

int
server_handle(struct env *env, struct client *clt)
{
	log_debug("SCRIPT_NAME %s", clt->clt_script_name);
	log_debug("PATH_INFO   %s", clt->clt_path_info);
	return (route_dispatch(env, clt));
}

void
server_client_free(struct client *clt)
{
#if template
	template_free(clt->clt_tp);
#endif
	free(clt->clt_server_name);
	free(clt->clt_script_name);
	free(clt->clt_path_info);
	free(clt->clt_query);
	free(clt);
}

static inline int
unquote(char *str)
{
	char		*p, *q;
	char		 hex[3];
	unsigned long	 x;

	hex[2] = '\0';
	p = q = str;
	while (*p) {
		switch (*p) {
		case '%':
			if (!isxdigit((unsigned char)p[1]) ||
			    !isxdigit((unsigned char)p[2]) ||
			    (p[1] == '0' && p[2] == '0'))
				return (-1);

			hex[0] = p[1];
			hex[1] = p[2];

			x = strtoul(hex, NULL, 16);
			*q++ = (char)x;
			p += 3;
			break;
		default:
			*q++ = *p++;
			break;
		}
	}
	*q = '\0';
	return (0);
}

static inline int
fts_escape(const char *p, char *buf, size_t bufsize)
{
	char			*q;

	/*
	 * split p into words and quote them into buf.
	 * quoting means wrapping each word into "..." and
	 * replace every " with "".
	 * i.e. 'C++ "framework"' -> '"C++" """framework"""'
	 * flatting all the whitespaces seems fine too.
	 */

	q = buf;
	while (bufsize != 0) {
		p += strspn(p, " \f\n\r\t\v");
		if (*p == '\0')
			break;

		*q++ = '"';
		bufsize--;
		while (*p && !isspace((unsigned char)*p) && bufsize != 0) {
			if (*p == '"') { /* double the quote character */
				*q++ = '"';
				bufsize--;
				if (bufsize == 0)
					break;
			}
			*q++ = *p++;
			bufsize--;
		}

		if (bufsize < 2)
			break;
		*q++ = '"';
		*q++ = ' ';
		bufsize -= 2;
	}
	if ((*p == '\0') && bufsize != 0) {
		*q = '\0';
		return (0);
	}

	return (-1);
}

int
route_dispatch(struct env *env, struct client *clt)
{
	const struct route	*r;
	size_t			 i;

	for (i = 0; i < nitems(routes); ++i) {
		r = &routes[i];

		if (fnmatch(r->r_path, clt->clt_path_info, 0) != 0)
			continue;
		return (r->r_fn(env, clt));
	}

	if (server_reply(clt, 51, "not found") == -1)
		return (-1);
	return (fcgi_end_request(clt, 0));
}

int
route_home(struct env *env, struct client *clt)
{
	if (server_reply(clt, 20, "text/gemini") == -1)
		return (-1);

#if 1
	if (clt_printf(clt, "# pkg_fcgi\n\n") == -1)
		return (-1);
	if (clt_printf(clt, "Welcome to pkg_fcgi, the Gemini interface "
	    "for the OpenBSD ports collection.\n\n") == -1)
		return (-1);
	if (clt_printf(clt, "=> %s/search Search for a package\n",
	    clt->clt_script_name) == -1)
		return (-1);
	if (clt_printf(clt, "=> %s/all All categories\n",
	    clt->clt_script_name) == -1)
		return (-1);
	if (clt_printf(clt, "\n") == -1)
		return (-1);
	if (clt_printf(clt, "What you search will be matched against the "
	    "package name (pkgstem), comment, DESCR and maintainer.\n") == -1)
		return (-1);
#else
	if (tp_home(clt->clt_tp) == -1)
		return (-1);
#endif

	return (fcgi_end_request(clt, 0));
}

int
route_search(struct env *env, struct client *clt)
{
	const char	*stem, *comment, *fullpkgpath;
	char		*query = clt->clt_query;
	char		 equery[1024];
	int		 err;
	int		 found = 0;

	if (query == NULL || *query == '\0') {
		if (server_reply(clt, 10, "search for a package") == -1)
			return (-1);
		return (fcgi_end_request(clt, 0));
	}

	if (unquote(query) == -1 ||
	    fts_escape(query, equery, sizeof(equery)) == -1) {
		if (server_reply(clt, 59, "bad request") == -1)
			return (-1);
		return (fcgi_end_request(clt, 1));
	}

	log_debug("searching for %s", equery);

	err = sqlite3_bind_text(env->env_qsearch, 1, equery, -1, NULL);
	if (err != SQLITE_OK) {
		log_warnx("%s: sqlite3_bind_text \"%s\": %s", __func__,
		    query, sqlite3_errstr(err));
		sqlite3_reset(env->env_qsearch);

		if (server_reply(clt, 42, "internal error") == -1)
			return (-1);
		return (fcgi_end_request(clt, 1));
	}

	if (server_reply(clt, 20, "text/gemini") == -1)
		goto err;

	if (clt_printf(clt, "# search results for %s\n\n", query) == -1)
		goto err;

	for (;;) {
		err = sqlite3_step(env->env_qsearch);
		if (err == SQLITE_DONE)
			break;
		if (err != SQLITE_ROW) {
			log_warnx("%s: sqlite3_step %s", __func__,
			    sqlite3_errstr(err));
			break;
		}
		found = 1;

		stem = sqlite3_column_text(env->env_qsearch, 0);
		comment = sqlite3_column_text(env->env_qsearch, 1);
		fullpkgpath = sqlite3_column_text(env->env_qsearch, 2);

		if (clt_printf(clt, "=> %s/%s %s: %s\n", clt->clt_script_name,
		    fullpkgpath, stem, comment) == -1)
			goto err;
	}

	sqlite3_reset(env->env_qsearch);

	if (!found && clt_printf(clt, "No ports found\n") == -1)
		return (-1);

	return (fcgi_end_request(clt, 0));

 err:
	sqlite3_reset(env->env_qsearch);
	return (-1);
}

int
route_categories(struct env *env, struct client *clt)
{
	const char	*fullpkgpath;
	int		 err;

	if (server_reply(clt, 20, "text/gemini") == -1)
		return (-1);
	if (clt_printf(clt, "# list of all categories\n") == -1)
		return (-1);

	if (clt_puts(clt, "\n") == -1)
		return (-1);

	for (;;) {
		err = sqlite3_step(env->env_qcats);
		if (err == SQLITE_DONE)
			break;
		if (err != SQLITE_ROW) {
			log_warnx("%s: sqlite3_step %s", __func__,
			    sqlite3_errstr(err));
			break;
		}

		fullpkgpath = sqlite3_column_text(env->env_qcats, 0);

		if (clt_printf(clt, "=> %s/%s %s\n", clt->clt_script_name,
		    fullpkgpath, fullpkgpath) == -1) {
			sqlite3_reset(env->env_qcats);
			return (-1);
		}
	}

	sqlite3_reset(env->env_qcats);
	return (fcgi_end_request(clt, 0));
}

int
route_listing(struct env *env, struct client *clt)
{
	char		 buf[128], *s;
	const char	*path = clt->clt_path_info + 1;
	const char	*fullpkgpath;
	int		 err;

	strlcpy(buf, path, sizeof(buf));
	while ((s = strrchr(buf, '/')) != NULL)
		*s = '\0';

	err = sqlite3_bind_text(env->env_qbycat, 1, buf, -1, NULL);
	if (err != SQLITE_OK) {
		log_warnx("%s: sqlite3_bind_text \"%s\": %s", __func__,
		    path, sqlite3_errstr(err));
		sqlite3_reset(env->env_qbycat);

		if (server_reply(clt, 42, "internal error") == -1)
			return (-1);
		return (fcgi_end_request(clt, 1));
	}

	if (server_reply(clt, 20, "text/gemini") == -1)
		goto err;

	if (clt_printf(clt, "# port(s) under %s\n\n", path) == -1)
		goto err;

	for (;;) {
		err = sqlite3_step(env->env_qbycat);
		if (err == SQLITE_DONE)
			break;
		if (err != SQLITE_ROW) {
			log_warnx("%s: sqlite3_step %s", __func__,
			    sqlite3_errstr(err));
			break;
		}

		fullpkgpath = sqlite3_column_text(env->env_qbycat, 0);

		if (clt_printf(clt, "=> %s/%s %s\n", clt->clt_script_name,
		    fullpkgpath, fullpkgpath) == -1) {
			sqlite3_reset(env->env_qbycat);
			return (-1);
		}
	}

	sqlite3_reset(env->env_qbycat);
	return (fcgi_end_request(clt, 0));

 err:
	sqlite3_reset(env->env_qbycat);
	return (-1);
}

static int
print_maintainer(struct client *clt, const char *mail)
{
	int	r, in_addr;

	for (in_addr = 0; *mail != '\0'; ++mail) {
		if (!in_addr) {
			if (clt_putc(clt, *mail) == -1)
				return (-1);
			if (*mail == '<')
				in_addr = 1;
			continue;
		}

		switch (*mail) {
		case '@':
			r = clt_puts(clt, " at ");
			break;
		case '.':
			r = clt_puts(clt, " dot ");
			break;
		case '>':
			in_addr = 0;
			/* fallthrough */
		default:
			r = clt_putc(clt, *mail);
			break;
		}
		if (r == -1)
			return (-1);
	}

	return (0);
}

int
route_port(struct env *env, struct client *clt)
{
	const char	*path = clt->clt_path_info + 1;
	const char	*fullpkgpath, *stem, *pkgname, *descr;
	const char	*comment, *maintainer, *readme, *www;
	const char	*version;
	int		 err;

	err = sqlite3_bind_text(env->env_qfullpkgpath, 1, path, -1, NULL);
	if (err != SQLITE_OK) {
		log_warnx("%s: sqlite3_bind_text \"%s\": %s", __func__,
		    path, sqlite3_errstr(err));
		sqlite3_reset(env->env_qfullpkgpath);

		if (server_reply(clt, 42, "internal error") == -1)
			return (-1);
		return (fcgi_end_request(clt, 1));
	}

	err = sqlite3_step(env->env_qfullpkgpath);
	if (err == SQLITE_DONE) {
		/* No rows, retry as a category */
		sqlite3_reset(env->env_qfullpkgpath);
		return (route_listing(env, clt));
	}

	if (err != SQLITE_ROW) {
		log_warnx("%s: sqlite3_step %s", __func__,
		    sqlite3_errstr(err));
		if (server_reply(clt, 42, "internal error") == -1)
			goto err;
		goto done;
	}

	fullpkgpath = sqlite3_column_text(env->env_qfullpkgpath, 0);
	stem = sqlite3_column_text(env->env_qfullpkgpath, 1);
	comment = sqlite3_column_text(env->env_qfullpkgpath, 2);
	pkgname = sqlite3_column_text(env->env_qfullpkgpath, 3);
	descr = sqlite3_column_text(env->env_qfullpkgpath, 4);
	maintainer = sqlite3_column_text(env->env_qfullpkgpath, 5);
	readme = sqlite3_column_text(env->env_qfullpkgpath, 6);
	www = sqlite3_column_text(env->env_qfullpkgpath, 7);

	if ((version = strrchr(pkgname, '-')) != NULL)
		version++;
	else
		version = " unknown";

	if (server_reply(clt, 20, "text/gemini") == -1)
		goto err;

	if (clt_printf(clt, "# %s v%s\n", path, version) == -1 ||
	    clt_puts(clt, "\n") == -1 ||
	    clt_printf(clt, "``` Command to install the package %s\n",
	    stem) == -1 ||
	    clt_printf(clt, "# pkg_add %s\n", stem) == -1 ||
	    clt_printf(clt, "```\n") == -1 ||
	    clt_printf(clt, "\n") == -1 ||
	    clt_printf(clt, "> %s\n", comment) == -1 ||
	    clt_printf(clt, "\n") == -1 ||
	    clt_printf(clt, "=> https://cvsweb.openbsd.org/ports/%s "
	    "CVS Web\n", fullpkgpath) == -1)
		goto err;

	if (www && *www != '\0' &&
	    clt_printf(clt, "=> %s Port Homepage (WWW)\n", www) == -1)
		goto err;

	if (clt_printf(clt, "\n") == -1 ||
	    clt_printf(clt, "Maintainer: ") == -1 ||
	    print_maintainer(clt, maintainer) == -1 ||
	    clt_puts(clt, "\n\n") == -1 ||
	    clt_printf(clt, "## Description\n\n") == -1 ||
	    clt_printf(clt, "``` %s description\n", stem) == -1 ||
	    clt_puts(clt, descr) == -1 ||
	    clt_puts(clt, "```\n") == -1 ||
	    clt_puts(clt, "\n") == -1)
		goto err;

	if (readme && *readme != '\0') {
		if (clt_puts(clt, "## Readme\n\n") == -1 ||
		    clt_puts(clt, "\n") == -1 ||
		    clt_printf(clt, "``` README for %s\n", stem) == -1 ||
		    clt_puts(clt, readme) == -1 ||
		    clt_puts(clt, "\n") == -1)
			goto err;
	}

 done:
	sqlite3_reset(env->env_qfullpkgpath);
	return (fcgi_end_request(clt, 0));

 err:
	sqlite3_reset(env->env_qfullpkgpath);
	return (-1);
}
