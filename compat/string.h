#include_next "string.h"

#include "../config.h"

#if !HAVE_STRLCPY
size_t		strlcpy(char *, const char *, size_t);
#endif

#if !HAVE_STRLCAT
size_t		strlcat(char *, const char *, size_t);
#endif
