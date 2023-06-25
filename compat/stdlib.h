#include_next "stdlib.h"

#include "../config.h"

#ifndef __dead
# define __dead __attribute__((noreturn))
#endif

#if !HAVE_GETPROGNAME
const char	*getprogname(void);
#endif

#if !HAVE_SETPROCTITLE
const char	*setproctitle(const char *, ...);
#endif

#if !HAVE_FREEZERO
void		 freezero(void *, size_t);
#endif

#if !HAVE_REALLOCARRAY
void		*reallocarray(void *, size_t, size_t);
#endif

#if !HAVE_RECALLOCARRAY
void		*recallocarray(void *, size_t, size_t, size_t);
#endif

#if !HAVE_STRTONUM
long long	 strtonum(const char *, long long, long long, const char **);
#endif
