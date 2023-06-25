/*
 * Copyright (c) 2022 Omar Polo <op@openbsd.org>
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

#ifndef TMPL_H
#define TMPL_H

struct template;

typedef int (*tmpl_puts)(struct template *, const char *);
typedef int (*tmpl_putc)(struct template *, int);

struct template {
	void		*tp_arg;
	char		*tp_tmp;
	tmpl_puts	 tp_escape;
	tmpl_puts	 tp_puts;
	tmpl_putc	 tp_putc;
};

int		 tp_urlescape(struct template *, const char *);
int		 tp_htmlescape(struct template *, const char *);

struct template	*template(void *, tmpl_puts, tmpl_putc);
void		 template_free(struct template *);

#endif
