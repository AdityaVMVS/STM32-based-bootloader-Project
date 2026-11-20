#include <stddef.h>

/* GCC can emit this call for aggregate initialization in freestanding code. */
void *memset(void *destination, int value, size_t size)
{
    unsigned char *bytes = destination;
    for (size_t i = 0; i < size; ++i) bytes[i] = (unsigned char)value;
    return destination;
}
