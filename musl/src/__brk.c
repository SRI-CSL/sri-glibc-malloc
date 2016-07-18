#include <stdint.h>
#include <unistd.h>

uintptr_t __brk(uintptr_t newbrk)
{
  return (uintptr_t)brk((void *)newbrk);
}
