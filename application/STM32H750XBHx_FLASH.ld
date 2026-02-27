/*
******************************************************************************
**

**  File        : LinkerScript.ld
**
**  Author		: STM32CubeMX
**
**  Abstract    : Linker script for STM32H750XBHx series
**                128Kbytes FLASH and 1056Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed "as is," without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** <h2><center>&copy; COPYRIGHT(c) 2019 STMicroelectronics</center></h2>
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**   1. Redistributions of source code must retain the above copyright notice,
**      this list of conditions and the following disclaimer.
**   2. Redistributions in binary form must reproduce the above copyright notice,
**      this list of conditions and the following disclaimer in the documentation
**      and/or other materials provided with the distribution.
**   3. Neither the name of STMicroelectronics nor the names of its contributors
**      may be used to endorse or promote products derived from this software
**      without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);    /* end of DTCMRAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x400;      /* 32KB 堆 */
_Min_Stack_Size = 0x200;     /* 8KB 栈 */

/* Specify the memory areas */
MEMORY
{
ITCMRAM (xrw)                 : ORIGIN = 0x00000000, LENGTH = 64K
DTCMRAM (xrw)                 : ORIGIN = 0x20000000, LENGTH = 128K
RAM (xrw)                     : ORIGIN = 0x24000000, LENGTH = 512K
RAM_D2 (xrw)                  : ORIGIN = 0x30000000, LENGTH = 288K
RAM_D3 (xrw)                  : ORIGIN = 0x38000000, LENGTH = 64K
FLASH (rx)                    : ORIGIN = 0x90000000, LENGTH = 2048K
}

/* Define Flash起始地址符号供代码使用 */
_flash_start = ORIGIN(FLASH);

/* Define output sections */
SECTIONS
{
  /* 向量表必须在 Flash */
  _sivector = LOADADDR(.isr_vector);
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);

    _evector = .;           /* 添加向量表结束标记 */
  } >ITCMRAM AT>FLASH

  /* Reset_Handler 必须在 Flash */
  .text.boot :
  {
    . = ALIGN(4);
    *(.text.boot.Reset_Handler)  /* 把Reset_Handler 放在 Flash */
    *(.text.startup)
    . = ALIGN(4);
  } >FLASH

  /* 其他代码在 RAM */
  _sitext = LOADADDR(.text);
  .text :
  {
    . = ALIGN(4);
    _stext = .;
    *(.text)           
    *(.text.*)         /* 这里不会匹配已经放置的 .text.boot.Reset_Handler */
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >RAM AT> FLASH

  /* Constant data goes into FLASH */
  _sirodata = LOADADDR(.rodata);
  .rodata :
  {
    . = ALIGN(4);
    _srodata = .;
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
    _erodata = .;
  } >RAM AT> FLASH
  
  /* ARM exception tables */
  .ARM.extab :
  {
    __extab_start = .;
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    __extab_end = .;
  } >RAM AT>FLASH

  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } >RAM AT>FLASH

  _sipreinit_array = LOADADDR(.preinit_array);
  .preinit_array     :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >RAM AT> FLASH

  _siinit_array = LOADADDR(.init_array);
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >RAM AT> FLASH

  _sifini_array = LOADADDR(.fini_array);
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >RAM AT> FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data : 
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >RAM AT> FLASH

  
  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );        /* C 标准库（如 newlib）用它来确定堆的起始位置 */
    PROVIDE ( _end = . );       /* 某些嵌入式系统用它来初始化堆管理器 */ 
    . = . + _Min_Heap_Size;     /* 堆大小 */
    . = . + _Min_Stack_Size;    /* 栈大小 */
    . = ALIGN(8);
  } >DTCMRAM

  
  ._RAM_D2_Area :
  {
      . = ALIGN(32);
      *(.DMA_Section)         /* RAM_D2 area section */
      *(.DMA_Section*)        /* RAM_D2 area section */
      . = ALIGN(32);
  } >RAM_D2

  ._RAM_D3_Area :
  {
      . = ALIGN(32);
      *(.BDMA_Section)         /* RAM_D2 area section */
      *(.BDMA_Section*)        /* RAM_D2 area section */
      . = ALIGN(32);
  } >RAM_D3

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }


}


