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

#define FD_RESERVE	5
#define GEMINI_MAXLEN	1025	/* including NUL */

#ifdef DEBUG
#define DPRINTF		log_debug
#else
#define DPRINTF(x...)	do {} while (0)
#endif

struct bufferevent;
struct event;
struct fcgi;
struct sqlite3;
struct sqlite3_stmt;

enum {
	METHOD_UNKNOWN,
	METHOD_GET,
	METHOD_POST,
};

struct client {
	uint32_t		 clt_id;
	int			 clt_fd;
	struct fcgi		*clt_fcgi;
	char			*clt_server_name;
	char			*clt_script_name;
	char			*clt_path_info;
	char			*clt_query;
	int			 clt_method;
#if template
	struct template		*clt_tp;
#endif
	char			 clt_buf[1024];
	size_t			 clt_buflen;

	SPLAY_ENTRY(client)	 clt_nodes;
};
SPLAY_HEAD(client_tree, client);

struct fcgi {
	uint32_t		 fcg_id;
	int			 fcg_s;
	struct client_tree	 fcg_clients;
	struct bufferevent	*fcg_bev;
	int			 fcg_toread;
	int			 fcg_want;
	int			 fcg_padding;
	int			 fcg_type;
	int			 fcg_rec_id;
	int			 fcg_keep_conn;
	int			 fcg_done;

	struct env		*fcg_env;

	SPLAY_ENTRY(fcgi)	 fcg_nodes;
};
SPLAY_HEAD(fcgi_tree, fcgi);

struct env {
	int			 env_sockfd;
	struct event		 env_sockev;
	struct event		 env_pausev;
	struct fcgi_tree	 env_fcgi_socks;

	struct sqlite3		*env_db;
	struct sqlite3_stmt	*env_qsearch;
	struct sqlite3_stmt	*env_qfullpkgpath;
	struct sqlite3_stmt	*env_qcats;
	struct sqlite3_stmt	*env_qbycat;
};

/* fcgi.c */
int	fcgi_end_request(struct client *, int);
int	fcgi_abort_request(struct client *);
void	fcgi_accept(int, short, void *);
void	fcgi_read(struct bufferevent *, void *);
void	fcgi_write(struct bufferevent *, void *);
void	fcgi_error(struct bufferevent *, short, void *);
void	fcgi_free(struct fcgi *);
int	clt_putc(struct client *, char);
int	clt_puts(struct client *, const char *);
int	clt_write_bufferevent(struct client *, struct bufferevent *);
int	clt_flush(struct client *);
int	clt_write(struct client *, const uint8_t *, size_t);
int	clt_printf(struct client *, const char *, ...)
	    __attribute__((__format__(printf, 2, 3)))
	    __attribute__((__nonnull__(2)));
#if template
int	clt_tp_puts(struct template *, const char *);
int	clt_tp_putc(struct template *, int);
#endif
int	fcgi_cmp(struct fcgi *, struct fcgi *);
int	fcgi_client_cmp(struct client *, struct client *);

/* server.c */
int	server_main(const char *);
int	server_handle(struct env *, struct client *);
void	server_client_free(struct client *);

#if template
/* ui.tmpl */
int	tp_home(struct template *);
#endif

SPLAY_PROTOTYPE(client_tree, client, clt_nodes, fcgi_client_cmp);
SPLAY_PROTOTYPE(fcgi_tree, fcgi, fcg_nodes, fcgi_cmp);
