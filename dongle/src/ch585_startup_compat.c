#include <stdint.h>

/*
 * startup_CH585.S jumps into highcode_init() before main.
 * We provide a minimal implementation here so generic
 * riscv-none-embed-gcc can link without relying on CH58x_sys.c,
 * which uses WCH-specific instructions not supported by vanilla GCC.
 */
__attribute__((section(".highcode_init")))
void highcode_init(void)
{
    /* TODO: Optional early clock/power setup can be placed here. */
    (void)0;
}
