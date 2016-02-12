#include <stdint.h>
#include <unistd.h>

uintptr_t __brk(uintptr_t newbrk)
{
	return brk(newbrk);
}
