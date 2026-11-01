/* Supplemental assertions only; official IAP/Application layout unchanged. */
ASSERT(MAX(_ebss, ADDR(.noinit.xora_fault) + SIZEOF(.noinit.xora_fault))
       <= ORIGIN(RAM) + LENGTH(RAM) - 4096,
       "XORA: less than 4 KiB CH585 stack headroom")
