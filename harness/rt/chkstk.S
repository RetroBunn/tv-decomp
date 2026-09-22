/* Stack probe emitted by gcc for frames larger than a page: probes each
 * page between the current stack pointer and esp - eax.  Preserves all
 * registers (the caller subtracts eax from esp itself). */
        .text
        .globl ___chkstk_ms
___chkstk_ms:
        pushl   %ecx
        pushl   %eax
        cmpl    $0x1000, %eax
        leal    12(%esp), %ecx
        jb      2f
1:      subl    $0x1000, %ecx
        orl     $0, (%ecx)
        subl    $0x1000, %eax
        cmpl    $0x1000, %eax
        ja      1b
2:      subl    %eax, %ecx
        orl     $0, (%ecx)
        popl    %eax
        popl    %ecx
        ret
