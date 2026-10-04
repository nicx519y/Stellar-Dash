OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x10000 }
SECTIONS {
  .text : { KEEP(*(.init)) *(.text*) *(.rodata*) } > RAM
  .data : { *(.data*) *(.sdata*) } > RAM
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM
  __global_pointer$ = ADDR(.data) + 0x800;
  __stack_top = 0x20017FF0;
  ASSERT(. <= 0x20010000, "Maintenance code exceeds dedicated RAM range")
}
