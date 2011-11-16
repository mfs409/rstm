	.file	"handler.c"
	.text
	.p2align 4,,15
	.globl	handle_sigsegv
	.type	handle_sigsegv, @function
handle_sigsegv:
.LFB3:
	.cfi_startproc
	rep
	ret
	.cfi_endproc
.LFE3:
	.size	handle_sigsegv, .-handle_sigsegv
	.ident	"GCC: (GNU) 4.6.2"
	.section	.note.GNU-stack,"",@progbits
