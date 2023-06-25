#include <stdlib.h>
#include <unistd.h>

int
main(void)
{
	if (pledge("stdio", NULL) == -1)
		return 1;
	return 0;
}
