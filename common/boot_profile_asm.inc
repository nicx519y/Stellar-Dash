/* Inline recorder: usable before RAM code/data and libc are initialized.
 * Call only between startup operations: r0-r3/r12 are scratch; LR untouched. */
#include "boot_profile.h"
.macro BP_EARLY id, domain
#if HBOX_BOOT_PROFILE
    ldr r0, =BP_ADDRESS
    ldr r1, [r0]
    ldr r2, =BP_MAGIC
    cmp r1, r2
    bne .Lbp_done\@
    ldr r1, [r0, #12]
    cmp r1, #0
    bne .Lbp_done\@
    ldr r1, [r0, #8]
    cmp r1, #BP_CAPACITY
    bhs .Lbp_full\@
    add r2, r1, r1, lsl #1
    add r2, r0, r2, lsl #2
    add r2, r2, #64
    ldr r3, =((\domain << 16) | \id)
    str r3, [r2]
    mvn r3, #0
    str r3, [r2, #4]
    ldr r3, =0xE0001004
    ldr r3, [r3]
    str r3, [r2, #8]
    dmb
    adds r1, r1, #1
    str r1, [r0, #8]
    b .Lbp_done\@
.Lbp_full\@:
    movs r1, #2
    str r1, [r0, #12]
.Lbp_done\@:
#endif
.endm
