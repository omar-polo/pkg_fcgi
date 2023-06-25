/*
 * Copyright (c) 2000-2004 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <netdb.h>
#include <asr.h>
#include <event.h>
#include <stdlib.h>

/*
 * Libevent glue for ASR.
 */
struct event_asr {
	struct event	 ev;
	struct asr_query *async;
	void		(*cb)(struct asr_result *, void *);
	void		*arg;
};

static void
event_asr_dispatch(int fd __attribute__((__unused__)),
    short ev __attribute__((__unused__)), void *arg)
{
	struct event_asr	*eva = arg;
	struct asr_result	 ar;
	struct timeval		 tv;

	event_del(&eva->ev);

	if (asr_run(eva->async, &ar)) {
		eva->cb(&ar, eva->arg);
		free(eva);
	} else {
		event_set(&eva->ev, ar.ar_fd,
		    ar.ar_cond == ASR_WANT_READ ? EV_READ : EV_WRITE,
		    event_asr_dispatch, eva);
		tv.tv_sec = ar.ar_timeout / 1000;
		tv.tv_usec = (ar.ar_timeout % 1000) * 1000;
		event_add(&eva->ev, &tv);
	}
}

struct event_asr *
event_asr_run(struct asr_query *async, void (*cb)(struct asr_result *, void *),
    void *arg)
{
	struct event_asr *eva;
	struct timeval tv;

	eva = calloc(1, sizeof *eva);
	if (eva == NULL)
		return (NULL);
	eva->async = async;
	eva->cb = cb;
	eva->arg = arg;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	evtimer_set(&eva->ev, event_asr_dispatch, eva);
	evtimer_add(&eva->ev, &tv);
	return (eva);
}

void
event_asr_abort(struct event_asr *eva)
{
	asr_abort(eva->async);
	event_del(&eva->ev);
	free(eva);
}
