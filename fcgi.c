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

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <event.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "pkg.h"
#include "tmpl.h"

#define MIN(a, b)	((a) < (b) ? (a) : (b))

struct fcgi_header {
	unsigned char version;
	unsigned char type;
	unsigned char req_id1;
	unsigned char req_id0;
	unsigned char content_len1;
	unsigned char content_len0;
	unsigned char padding;
	unsigned char reserved;
} __attribute__((packed));

/*
 * number of bytes in a FCGI_HEADER.  Future version of the protocol
 * will not reduce this number.
 */
#define FCGI_HEADER_LEN	8

/*
 * values for the version component
 */
#define FCGI_VERSION_1	1

/*
 * values for the type component
 */
#define FCGI_BEGIN_REQUEST	 1
#define FCGI_ABORT_REQUEST	 2
#define FCGI_END_REQUEST	 3
#define FCGI_PARAMS		 4
#define FCGI_STDIN		 5
#define FCGI_STDOUT		 6
#define FCGI_STDERR		 7
#define FCGI_DATA		 8
#define FCGI_GET_VALUES		 9
#define FCGI_GET_VALUES_RESULT	10
#define FCGI_UNKNOWN_TYPE	11
#define FCGI_MAXTYPE		(FCGI_UNKNOWN_TYPE)

struct fcgi_begin_req {
	unsigned char role1;
	unsigned char role0;
	unsigned char flags;
	unsigned char reserved[5];
};

struct fcgi_begin_req_record {
	struct fcgi_header	header;
	struct fcgi_begin_req	body;
};

/*
 * mask for flags;
 */
#define FCGI_KEEP_CONN		1

/*
 * values for the role
 */
#define FCGI_RESPONDER	1
#define FCGI_AUTHORIZER	2
#define FCGI_FILTER	3

struct fcgi_end_req_body {
	unsigned char app_status3;
	unsigned char app_status2;
	unsigned char app_status1;
	unsigned char app_status0;
	unsigned char proto_status;
	unsigned char reserved[3];
};

/*
 * values for proto_status
 */
#define FCGI_REQUEST_COMPLETE	0
#define FCGI_CANT_MPX_CONN	1
#define FCGI_OVERLOADED		2
#define FCGI_UNKNOWN_ROLE	3

/*
 * Variable names for FCGI_GET_VALUES / FCGI_GET_VALUES_RESULT
 * records.
 */
#define FCGI_MAX_CONNS	"FCGI_MAX_CONNS"
#define FCGI_MAX_REQS	"FCGI_MAX_REQS"
#define FCGI_MPXS_CONNS	"FCGI_MPXS_CONNS"

#define CAT(f0, f1)	((f0) + ((f1) << 8))

enum {
	FCGI_RECORD_HEADER,
	FCGI_RECORD_BODY,
};

volatile int	fcgi_inflight;
int32_t		fcgi_id;

int	accept_reserve(int, struct sockaddr *, socklen_t *, int,
    volatile int *);

static int
fcgi_send_end_req(struct fcgi *fcgi, int id, int as, int ps)
{
	struct bufferevent	*bev = fcgi->fcg_bev;
	struct fcgi_header	 hdr;
	struct fcgi_end_req_body end;

	memset(&hdr, 0, sizeof(hdr));
	memset(&end, 0, sizeof(end));

	hdr.version = FCGI_VERSION_1;
	hdr.type = FCGI_END_REQUEST;
	hdr.req_id0 = (id & 0xFF);
	hdr.req_id1 = (id >> 8);
	hdr.content_len0 = sizeof(end);

	end.app_status0 = (unsigned char)as;
	end.proto_status = (unsigned char)ps;

	if (bufferevent_write(bev, &hdr, sizeof(hdr)) == -1)
		return (-1);
	if (bufferevent_write(bev, &end, sizeof(end)) == -1)
		return (-1);
	return (0);
}

static int
end_request(struct client *clt, int status, int proto_status)
{
	struct fcgi		*fcgi = clt->clt_fcgi;
	int			 r;

	if (clt_flush(clt) == -1)
		return (-1);

	r = fcgi_send_end_req(fcgi, clt->clt_id, status,
	    proto_status);
	if (r == -1) {
		fcgi_error(fcgi->fcg_bev, EV_WRITE, fcgi);
		return (-1);
	}

	SPLAY_REMOVE(client_tree, &fcgi->fcg_clients, clt);
	server_client_free(clt);

	if (!fcgi->fcg_keep_conn)
		fcgi->fcg_done = 1;

	return (0);
}

int
fcgi_end_request(struct client *clt, int status)
{
	return (end_request(clt, status, FCGI_REQUEST_COMPLETE));
}

int
fcgi_abort_request(struct client *clt)
{
	return (end_request(clt, 1, FCGI_OVERLOADED));
}

static void
fcgi_inflight_dec(const char *why)
{
	fcgi_inflight--;
	log_debug("%s: fcgi inflight decremented, now %d, %s",
	    __func__, fcgi_inflight, why);
}

void
fcgi_accept(int fd, short event, void *arg)
{
	struct env		*env = arg;
	struct fcgi		*fcgi = NULL;
	socklen_t		 slen;
	struct sockaddr_storage	 ss;
	int			 s = -1;

	event_add(&env->env_pausev, NULL);
	if ((event & EV_TIMEOUT))
		return;

	slen = sizeof(ss);
	if ((s = accept_reserve(env->env_sockfd, (struct sockaddr *)&ss,
	    &slen, FD_RESERVE, &fcgi_inflight)) == -1) {
		/*
		 * Pause accept if we are out of file descriptors, or
		 * libevent will haunt us here too.
		 */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			event_del(&env->env_sockev);
			evtimer_add(&env->env_pausev, &evtpause);
			log_debug("%s: deferring connections", __func__);
		}
		return;
	}

	if ((fcgi = calloc(1, sizeof(*fcgi))) == NULL)
		goto err;

	fcgi->fcg_id = ++fcgi_id;
	fcgi->fcg_s = s;
	fcgi->fcg_env = env;
	fcgi->fcg_want = FCGI_RECORD_HEADER;
	fcgi->fcg_toread = sizeof(struct fcgi_header);
	SPLAY_INIT(&fcgi->fcg_clients);

	/* assume it's enabled until we get a FCGI_BEGIN_REQUEST */
	fcgi->fcg_keep_conn = 1;

	fcgi->fcg_bev = bufferevent_new(fcgi->fcg_s, fcgi_read, fcgi_write,
	    fcgi_error, fcgi);
	if (fcgi->fcg_bev == NULL)
		goto err;

	bufferevent_enable(fcgi->fcg_bev, EV_READ | EV_WRITE);
	return;

err:
	if (s != -1) {
		close(s);
		free(fcgi);
		fcgi_inflight_dec(__func__);
	}
}

static int
parse_len(struct fcgi *fcgi, struct evbuffer *src)
{
	unsigned char		 c, x[3];

	fcgi->fcg_toread--;
	evbuffer_remove(src, &c, 1);
	if (c >> 7 == 0)
		return (c);

	if (fcgi->fcg_toread < 3)
		return (-1);

	fcgi->fcg_toread -= 3;
	evbuffer_remove(src, x, sizeof(x));
	return (((c & 0x7F) << 24) | (x[0] << 16) | (x[1] << 8) | x[2]);
}

static int
fcgi_parse_params(struct fcgi *fcgi, struct evbuffer *src, struct client *clt)
{
	char			 pname[32];
	char			 server[HOST_NAME_MAX + 1];
	char			 path[PATH_MAX];
	char			 query[GEMINI_MAXLEN];
	char			 method[8];
	int			 nlen, vlen;

	while (fcgi->fcg_toread > 0) {
		if ((nlen = parse_len(fcgi, src)) < 0 ||
		    (vlen = parse_len(fcgi, src)) < 0)
			return (-1);

		if (fcgi->fcg_toread < nlen + vlen)
			return (-1);

		if ((size_t)nlen > sizeof(pname) - 1) {
			/* ignore this parameter */
			fcgi->fcg_toread -= nlen - vlen;
			evbuffer_drain(src, nlen + vlen);
			continue;
		}

		fcgi->fcg_toread -= nlen;
		evbuffer_remove(src, &pname, nlen);
		pname[nlen] = '\0';

		if (!strcmp(pname, "SERVER_NAME") &&
		    (size_t)vlen < sizeof(server)) {
			fcgi->fcg_toread -= vlen;
			evbuffer_remove(src, &server, vlen);
			server[vlen] = '\0';

			free(clt->clt_server_name);
			if ((clt->clt_server_name = strdup(server)) == NULL)
				return (-1);
			DPRINTF("clt %d: server_name: %s", clt->clt_id,
			    clt->clt_server_name);
			continue;
		}

		if (!strcmp(pname, "SCRIPT_NAME") &&
		    (size_t)vlen < sizeof(path)) {
			fcgi->fcg_toread -= vlen;
			evbuffer_remove(src, &path, vlen);
			path[vlen] = '\0';

			free(clt->clt_script_name);
			clt->clt_script_name = NULL;

			if (vlen == 0 || path[vlen - 1] != '/')
				asprintf(&clt->clt_script_name, "%s/", path);
			else
				clt->clt_script_name = strdup(path);

			if (clt->clt_script_name == NULL)
				return (-1);

			DPRINTF("clt %d: script_name: %s", clt->clt_id,
			    clt->clt_script_name);
			continue;
		}

		/* XXX: fix gmid */
		if (/*!strcmp(pname, "PATH_INFO") && */
		    !strcmp(pname, "GEMINI_URL_PATH") &&
		    (size_t)vlen < sizeof(path)) {
			fcgi->fcg_toread -= vlen;
			evbuffer_remove(src, &path, vlen);
			path[vlen] = '\0';

			free(clt->clt_path_info);
			clt->clt_path_info = NULL;

			if (*path != '/')
				asprintf(&clt->clt_path_info, "/%s", path);
			else
				clt->clt_path_info = strdup(path);

			if (clt->clt_path_info == NULL)
				return (-1);

			DPRINTF("clt %d: path_info: %s", clt->clt_id,
			    clt->clt_path_info);
			continue;
		}

		if (!strcmp(pname, "QUERY_STRING") &&
		    (size_t)vlen < sizeof(query) &&
		    vlen > 0) {
			fcgi->fcg_toread -= vlen;
			evbuffer_remove(src, &query, vlen);
			query[vlen] = '\0';

			free(clt->clt_query);
			if ((clt->clt_query = strdup(query)) == NULL)
				return (-1);

			DPRINTF("clt %d: query: %s", clt->clt_id,
			    clt->clt_query);
			continue;
		}

		if (!strcmp(pname, "REQUEST_METHOD") &&
		    (size_t)vlen < sizeof(method)) {
			fcgi->fcg_toread -= vlen;
			evbuffer_remove(src, &method, vlen);
			method[vlen] = '\0';

			if (!strcasecmp(method, "GET"))
				clt->clt_method = METHOD_GET;
			if (!strcasecmp(method, "POST"))
				clt->clt_method = METHOD_POST;

			continue;
		}

		fcgi->fcg_toread -= vlen;
		evbuffer_drain(src, vlen);
	}

	return (0);
}

void
fcgi_read(struct bufferevent *bev, void *d)
{
	struct fcgi		*fcgi = d;
	struct env		*env = fcgi->fcg_env;
	struct evbuffer		*src = EVBUFFER_INPUT(bev);
	struct fcgi_header	 hdr;
	struct fcgi_begin_req	 breq;
	struct client		*clt, q;
	int			 role;

	memset(&q, 0, sizeof(q));

	for (;;) {
		if (EVBUFFER_LENGTH(src) < (size_t)fcgi->fcg_toread)
			return;

		if (fcgi->fcg_want == FCGI_RECORD_HEADER) {
			fcgi->fcg_want = FCGI_RECORD_BODY;
			bufferevent_read(bev, &hdr, sizeof(hdr));

			DPRINTF("header: v=%d t=%d id=%d len=%d p=%d",
			    hdr.version, hdr.type,
			    CAT(hdr.req_id0, hdr.req_id1),
			    CAT(hdr.content_len0, hdr.content_len1),
			    hdr.padding);

			if (hdr.version != FCGI_VERSION_1) {
				log_warnx("unknown fastcgi version: %d",
				    hdr.version);
				fcgi_error(bev, EV_READ, d);
				return;
			}

			fcgi->fcg_toread = CAT(hdr.content_len0,
			    hdr.content_len1);
			if (fcgi->fcg_toread < 0) {
				log_warnx("invalid record length: %d",
				    fcgi->fcg_toread);
				fcgi_error(bev, EV_READ, d);
				return;
			}

			fcgi->fcg_padding = hdr.padding;
			if (fcgi->fcg_padding < 0) {
				log_warnx("invalid padding: %d",
				    fcgi->fcg_padding);
				fcgi_error(bev, EV_READ, d);
				return;
			}

			fcgi->fcg_type = hdr.type;
			fcgi->fcg_rec_id = CAT(hdr.req_id0, hdr.req_id1);
			continue;
		}

		q.clt_id = fcgi->fcg_rec_id;
		clt = SPLAY_FIND(client_tree, &fcgi->fcg_clients, &q);

		switch (fcgi->fcg_type) {
		case FCGI_BEGIN_REQUEST:
			if (sizeof(breq) != fcgi->fcg_toread) {
				log_warnx("unexpected size for "
				    "FCGI_BEGIN_REQUEST");
				fcgi_error(bev, EV_READ, d);
				return;
			}

			evbuffer_remove(src, &breq, sizeof(breq));

			role = CAT(breq.role0, breq.role1);
			if (role != FCGI_RESPONDER) {
				log_warnx("unknown fastcgi role: %d",
				    role);
				if (fcgi_send_end_req(fcgi, fcgi->fcg_rec_id,
				    1, FCGI_UNKNOWN_ROLE) == -1) {
					fcgi_error(bev, EV_READ, d);
					return;
				}
				break;
			}

			if (!fcgi->fcg_keep_conn) {
				log_warnx("trying to reuse the fastcgi "
				    "socket without marking it as so.");
				fcgi_error(bev, EV_READ, d);
				return;
			}
			fcgi->fcg_keep_conn = breq.flags & FCGI_KEEP_CONN;

			if (clt != NULL) {
				log_warnx("ignoring attemp to re-use an "
				    "active request id (%d)",
				    fcgi->fcg_rec_id);
				break;
			}

			if ((clt = calloc(1, sizeof(*clt))) == NULL) {
				log_warnx("calloc");
				break;
			}

#if template
			clt->clt_tp = template(clt, clt_tp_puts, clt_tp_putc);
			if (clt->clt_tp == NULL) {
				free(clt);
				log_warn("template");
				break;
			}
#endif

			clt->clt_id = fcgi->fcg_rec_id;
			clt->clt_fd = -1;
			clt->clt_fcgi = fcgi;
			SPLAY_INSERT(client_tree, &fcgi->fcg_clients, clt);
			break;
		case FCGI_PARAMS:
			if (clt == NULL) {
				log_warnx("got FCGI_PARAMS for inactive id "
				    "(%d)", fcgi->fcg_rec_id);
				evbuffer_drain(src, fcgi->fcg_toread);
				break;
			}
			if (fcgi->fcg_toread == 0) {
				evbuffer_drain(src, fcgi->fcg_toread);
				if (server_handle(env, clt) == -1)
					return;
				break;
			}
			if (fcgi_parse_params(fcgi, src, clt) == -1) {
				log_warnx("fcgi_parse_params failed");
				fcgi_error(bev, EV_READ, d);
				return;
			}
			break;
		case FCGI_STDIN:
			/* not interested in reading stdin */
			evbuffer_drain(src, fcgi->fcg_toread);
			break;
		case FCGI_ABORT_REQUEST:
			if (clt == NULL) {
				log_warnx("got FCGI_ABORT_REQUEST for inactive"
				    " id (%d)", fcgi->fcg_rec_id);
				evbuffer_drain(src, fcgi->fcg_toread);
				break;
			}
			if (fcgi_end_request(clt, 1) == -1) {
				/* calls fcgi_error on failure */
				return;
			}
			break;
		default:
			log_warnx("unknown fastcgi record type %d",
			    fcgi->fcg_type);
			evbuffer_drain(src, fcgi->fcg_toread);
			break;
		}

		/* Prepare for the next record. */
		evbuffer_drain(src, fcgi->fcg_padding);
		fcgi->fcg_want = FCGI_RECORD_HEADER;
		fcgi->fcg_toread = sizeof(struct fcgi_header);
	}
}

void
fcgi_write(struct bufferevent *bev, void *d)
{
	struct fcgi		*fcgi = d;
	struct evbuffer		*out = EVBUFFER_OUTPUT(bev);

	if (fcgi->fcg_done && EVBUFFER_LENGTH(out) == 0)
		fcgi_error(bev, EVBUFFER_EOF, fcgi);
}

void
fcgi_error(struct bufferevent *bev, short event, void *d)
{
	struct fcgi		*fcgi = d;
	struct env		*env = fcgi->fcg_env;
	struct client		*clt;

	log_debug("fcgi failure, shutting down connection (ev: %x)",
	    event);
	fcgi_inflight_dec(__func__);

	while ((clt = SPLAY_MIN(client_tree, &fcgi->fcg_clients)) != NULL) {
		SPLAY_REMOVE(client_tree, &fcgi->fcg_clients, clt);
		server_client_free(clt);
	}

	SPLAY_REMOVE(fcgi_tree, &env->env_fcgi_socks, fcgi);
	fcgi_free(fcgi);

	return;
}

void
fcgi_free(struct fcgi *fcgi)
{
	close(fcgi->fcg_s);
	bufferevent_free(fcgi->fcg_bev);
	free(fcgi);
}

int
clt_flush(struct client *clt)
{
	struct fcgi		*fcgi = clt->clt_fcgi;
	struct bufferevent	*bev = fcgi->fcg_bev;
	struct fcgi_header	 hdr;

	if (clt->clt_buflen == 0)
		return (0);

	memset(&hdr, 0, sizeof(hdr));
	hdr.version = FCGI_VERSION_1;
	hdr.type = FCGI_STDOUT;
	hdr.req_id0 = (clt->clt_id & 0xFF);
	hdr.req_id1 = (clt->clt_id >> 8);
	hdr.content_len0 = (clt->clt_buflen & 0xFF);
	hdr.content_len1 = (clt->clt_buflen >> 8);

	if (bufferevent_write(bev, &hdr, sizeof(hdr)) == -1 ||
	    bufferevent_write(bev, clt->clt_buf, clt->clt_buflen) == -1) {
		fcgi_error(bev, EV_WRITE, fcgi);
		return (-1);
	}

	clt->clt_buflen = 0;

	return (0);
}

int
clt_write(struct client *clt, const uint8_t *buf, size_t len)
{
	size_t			 left, copy;

	while (len > 0) {
		left = sizeof(clt->clt_buf) - clt->clt_buflen;
		if (left == 0) {
			if (clt_flush(clt) == -1)
				return (-1);
			left = sizeof(clt->clt_buf);
		}

		copy = MIN(left, len);

		memcpy(&clt->clt_buf[clt->clt_buflen], buf, copy);
		clt->clt_buflen += copy;
		buf += copy;
		len -= copy;
	}

	return (0);
}

int
clt_putc(struct client *clt, char ch)
{
	return (clt_write(clt, &ch, 1));
}

int
clt_puts(struct client *clt, const char *str)
{
	return (clt_write(clt, str, strlen(str)));
}

int
clt_write_bufferevent(struct client *clt, struct bufferevent *bev)
{
	struct evbuffer		*src = EVBUFFER_INPUT(bev);
	size_t			 len, left, copy;

	len = EVBUFFER_LENGTH(src);
	while (len > 0) {
		left = sizeof(clt->clt_buf) - clt->clt_buflen;
		if (left == 0) {
			if (clt_flush(clt) == -1)
				return (-1);
			left = sizeof(clt->clt_buf);
		}

		copy = bufferevent_read(bev, &clt->clt_buf[clt->clt_buflen],
		    MIN(left, len));
		clt->clt_buflen += copy;

		len = EVBUFFER_LENGTH(src);
	}

	return (0);
}

int
clt_printf(struct client *clt, const char *fmt, ...)
{
	struct fcgi		*fcgi = clt->clt_fcgi;
	struct bufferevent	*bev = fcgi->fcg_bev;
	char			*str;
	va_list			 ap;
	int			 r;

	va_start(ap, fmt);
	r = vasprintf(&str, fmt, ap);
	va_end(ap);
	if (r == -1) {
		fcgi_error(bev, EV_WRITE, fcgi);
		return (-1);
	}

	r = clt_write(clt, str, r);
	free(str);
	return (r);
}

#if template
int
clt_tp_puts(struct template *tp, const char *str)
{
	struct client		*clt = tp->tp_arg;

	if (clt_puts(clt, str) == -1)
		return (-1);

	return (0);
}

int
clt_tp_putc(struct template *tp, int c)
{
	struct client		*clt = tp->tp_arg;

	if (clt_putc(clt, c) == -1)
		return (-1);

	return (0);
}
#endif

int
fcgi_cmp(struct fcgi *a, struct fcgi *b)
{
	return ((int)a->fcg_id - b->fcg_id);
}

int
fcgi_client_cmp(struct client *a, struct client *b)
{
	return ((int)a->clt_id - b->clt_id);
}

int
accept_reserve(int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    int reserve, volatile int *counter)
{
	int ret;

	if (getdtablecount() + reserve + *counter >= getdtablesize()) {
		errno = EMFILE;
		return (-1);
	}

	if ((ret = accept4(sockfd, addr, addrlen, SOCK_NONBLOCK)) > -1) {
		(*counter)++;
		log_debug("%s: inflight incremented, now %d", __func__,
		    *counter);
	}

	return (ret);
}

SPLAY_GENERATE(fcgi_tree, fcgi, fcg_nodes, fcgi_cmp);
SPLAY_GENERATE(client_tree, client, clt_nodes, fcgi_client_cmp);
