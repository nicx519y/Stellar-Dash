#include <stdint.h>

/* Keep as weak fallback; SDK CH58x_sys.c provides the real symbol when linked. */
__attribute__((weak, section(".highcode_init")))
void highcode_init(void)
{
    (void)0;
}
