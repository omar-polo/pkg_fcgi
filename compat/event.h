#include_next "event.h"

#include "../config.h"

#if !HAVE_EVENT_ASR_RUN
struct asr_query;
struct asr_result;
struct event_asr;

struct event_asr	*event_asr_run(struct asr_query *,
    void (*cb)(struct asr_result *, void *), void *);
void			 event_asr_abort(struct event_asr *);
#endif
