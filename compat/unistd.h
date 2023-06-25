#include_next "unistd.h"

#include "../config.h"

#if !HAVE_PLEGDE
int	pledge(const char *, const char *);
#endif

#if !HAVE_UNVEIL
int	unveil(const char *, const char *);
#endif

#if !HAVE_GETDTABLECOUNT
int	getdtablecount(void);
#endif

#if !HAVE_GETDTABLESIZE
int	getdtablesize(void);
#endif

#if !HAVE_SETRESGID
int	setresgid(gid_t, gid_t, gid_t);
#endif

#if !HAVE_SETRESUID
int	setresuid(uid_t, uid_t, uid_t);
#endif
