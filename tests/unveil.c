#include <stdlib.h>
#include <unistd.h>

int
main(void)
{
	if (unveil(".", "r") == -1)
		return 1;
	return 0;
}
