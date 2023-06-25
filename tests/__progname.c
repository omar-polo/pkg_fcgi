#include <stdio.h>

extern const char *__progname;

int
main(void)
{
	puts(__progname);
	return 0;
}
