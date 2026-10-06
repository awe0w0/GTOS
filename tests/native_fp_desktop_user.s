.section .text
.code32
.global native_fp_desktop_seed
.type native_fp_desktop_seed,@function
native_fp_desktop_seed:
    fninit
    movl 12(%esp),%eax
    fldcw (%eax)
    movl 16(%esp),%eax
    ldmxcsr (%eax)
    movl 8(%esp),%eax
    fildl 0(%eax)
    fildl 4(%eax)
    fildl 8(%eax)
    fildl 12(%eax)
    fildl 16(%eax)
    fildl 20(%eax)
    fildl 24(%eax)
    fildl 28(%eax)
    fincstp
    ffree %st(3)
    movl 4(%esp),%eax
    movdqu 0(%eax),%xmm0
    movdqu 16(%eax),%xmm1
    movdqu 32(%eax),%xmm2
    movdqu 48(%eax),%xmm3
    movdqu 64(%eax),%xmm4
    movdqu 80(%eax),%xmm5
    movdqu 96(%eax),%xmm6
    movdqu 112(%eax),%xmm7
    ret
.size native_fp_desktop_seed,.-native_fp_desktop_seed
.global native_fp_desktop_capture
.type native_fp_desktop_capture,@function
native_fp_desktop_capture:
    movl 4(%esp),%eax
    fxsave (%eax)
    fnstenv 512(%eax)
    fldenv 512(%eax)
    ret
.size native_fp_desktop_capture,.-native_fp_desktop_capture
.section .note.GNU-stack,"",@progbits
