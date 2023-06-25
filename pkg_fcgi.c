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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/tree.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "log.h"
#include "pkg.h"

#ifndef PKG_FCGI_DB
#define PKG_FCGI_DB "/pkg_fcgi/pkgs.sqlite3"
#endif

#ifndef PKG_FCGI_SOCK
#define PKG_FCGI_SOCK "/run/pkg_fcgi.sock"
#endif

#ifndef PKG_FCGI_USER
#define PKG_FCGI_USER "www"
#endif

#define MAX_CHILDREN	32

static const char		*argv0;
static pid_t			 pids[MAX_CHILDREN];
static int			 children = 3;

static volatile sig_atomic_t	 got_sigchld;

static void
handle_sigchld(int sig)
{
	int	i, saved_errno;

	if (got_sigchld)
		return;

	got_sigchld = 1;
	saved_errno = errno;

	for (i = 0; i < children; ++i)
		(void) kill(pids[i], SIGTERM);

	errno = saved_errno;
}

static int
bind_socket(const char *path, struct passwd *pw)
{
	struct sockaddr_un	 sun;
	int			 fd, old_umask;

	if ((fd = socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, 0)) == -1) {
		log_warn("%s: socket", __func__);
		return (-1);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;

	if (strlcpy(sun.sun_path, path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path)) {
		log_warnx("%s: path too long: %s", __func__, path);
		close(fd);
		return (-1);
	}

	if (unlink(path) == -1 && errno != ENOENT) {
		log_warn("%s: unlink %s", __func__, path);
		close(fd);
		return (-1);
	}

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_warn("%s: bind: %s (%d)", __func__, path, geteuid());
		close(fd);
		umask(old_umask);
		return (-1);
	}
	umask(old_umask);

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
		log_warn("%s: chmod %s", __func__, path);
		close(fd);
		(void) unlink(path);
		return (-1);
	}

	if (chown(path, pw->pw_uid, pw->pw_gid) == -1) {
		log_warn("%s: chown %s %s", __func__, pw->pw_name, path);
		close(fd);
		(void) unlink(path);
		return (-1);
	}

	if (listen(fd, 5) == -1) {
		log_warn("%s: listen", __func__);
		close(fd);
		(void) unlink(path);
		return (-1);
	}

	return (fd);
}

static pid_t
start_child(const char *root, const char *user, const char *db,
    int daemonize, int verbose, int fd)
{
	char	*argv[10];
	int	 argc = 0;
	pid_t	 pid;

	switch (pid = fork()) {
	case -1:
		fatal("cannot fork");
	case 0:
		break;
	default:
		close(fd);
		return (pid);
	}

	if (fd != 3) {
		if (dup2(fd, 3) == -1)
			fatal("cannot setup imsg fd");
	} else if (fcntl(fd, F_SETFD, 0) == -1)
		fatal("cannot setup imsg fd");

	argv[argc++] = (char *)argv0;
	argv[argc++] = (char *)"-S";
	argv[argc++] = (char *)"-p"; argv[argc++] = (char *)root;
	argv[argc++] = (char *)"-u"; argv[argc++] = (char *)user;
	if (!daemonize)
		argv[argc++] = (char *)"-d";
	if (verbose)
		argv[argc++] = (char *)"-v";
	argv[argc++] = (char *)db;
	argv[argc++] = NULL;

	execvp(argv0, argv);
	fatal("execvp");
}

static void __dead
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-dv] [-j n] [-p path] [-s socket] [-u user] [db]\n",
	    getprogname());
	exit(1);
}

int
main(int argc, char **argv)
{
	struct stat	 sb;
	struct passwd	*pw;
	pid_t		 pid;
	char		 path[PATH_MAX];
	const char	*cause;
	const char	*root = NULL;
	const char	*sock = PKG_FCGI_SOCK;
	const char	*user = PKG_FCGI_USER;
	const char	*db = PKG_FCGI_DB;
	const char	*errstr;
	int		 ch, i, daemonize = 1, verbosity = 0;
	int		 server = 0, fd = -1;
	int		 status;

	/*
	 * Ensure we have fds 0-2 open so that we have no issue with
	 * calling bind_socket before daemon(3).
	 */
	for (i = 0; i < 3; ++i) {
		if (fstat(i, &sb) == -1) {
			if ((fd = open("/dev/null", O_RDWR)) != -1) {
				if (dup2(fd, i) == -1)
					exit(1);
				if (fd > i)
					close(fd);
			} else
				exit(1);
		}
	}

	log_init(1, LOG_DAEMON); /* Log to stderr until daemonized. */

	if ((argv0 = argv[0]) == NULL)
		fatalx("argv[0] is NULL");

	while ((ch = getopt(argc, argv, "dj:p:Ss:u:v")) != -1) {
		switch (ch) {
		case 'd':
			daemonize = 0;
			break;
		case 'j':
			children = strtonum(optarg, 1, MAX_CHILDREN, &errstr);
			if (errstr)
				fatalx("number of children is %s: %s",
				    errstr, optarg);
			break;
		case 'p':
			root = optarg;
			break;
		case 'S':
			server = 1;
			break;
		case 's':
			sock = optarg;
			break;
		case 'u':
			user = optarg;
			break;
		case 'v':
			verbosity++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();
	if (argc == 1)
		db = argv[0];

	if (geteuid())
		fatalx("need root privileges");

	pw = getpwnam(user);
	if (pw == NULL)
		fatalx("user %s not found", user);
	if (pw->pw_uid == 0)
		fatalx("cannot run as %s: must not be the superuser", user);

	if (root == NULL)
		root = pw->pw_dir;

	if (!server) {
		int ret;

		ret = snprintf(path, sizeof(path), "%s/%s", root, sock);
		if (ret < 0 || (size_t)ret >= sizeof(path))
			fatalx("socket path too long");

		if ((fd = bind_socket(path, pw)) == -1)
			fatalx("failed to open socket %s", sock);

		for (i = 0; i < children; ++i) {
			int d;

			if ((d = dup(fd)) == -1)
				fatalx("dup");
			pids[i] = start_child(root, user, db,
			    daemonize, verbosity, d);
			log_debug("forking child %d (pid %lld)", i,
			    (long long)pids[i]);
		}

		signal(SIGCHLD, handle_sigchld);
	}

	if (chroot(root) == -1)
		fatal("chroot %s", root);
	if (chdir("/") == -1)
		fatal("chdir /");

	if (setgroups(1, &pw->pw_gid) == -1 ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1 ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1)
		fatal("failed to drop privileges");

	log_init(daemonize ? 0 : 1, LOG_DAEMON);
	log_setverbose(verbosity);

	if (server)
		exit(server_main(db));

	if (daemonize && daemon(1, 0) == -1)
		fatal("daemon");

	if (pledge("stdio proc", NULL) == -1)
		fatal("pledge");

	for (;;) {
		do {
			pid = waitpid(WAIT_ANY, &status, 0);
		} while (pid != -1 || errno == EINTR);

		if (pid == -1) {
			if (errno == ECHILD)
				break;
			fatal("waitpid failed");
		}

		if (WIFSIGNALED(status))
			cause = "was terminated";
		else if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != 0)
				cause = "exited abnormally";
			else
				cause = "exited successfully";
		} else
			cause = "died";

		log_warnx("child process %lld %s", (long long)pid, cause);
	}

	return (1);
}
