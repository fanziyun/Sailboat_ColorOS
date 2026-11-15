.section .text._start,"ax",%progbits
.balign 4
.global _start
.type _start,%function
_start:
    bti j
    mov x29, xzr
    mov x30, xzr
    mov x0, sp
    b Sail_FrameBoost_start
.size _start,.-_start
.section .note.GNU-stack,"",%progbits
