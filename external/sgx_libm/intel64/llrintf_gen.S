/*
* Math library
*
* Copyright (C) 2016 Intel Corporation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in
*     the documentation and/or other materials provided with the
*     distribution.
*   * Neither the name of Intel Corporation nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
* A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
* OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*
* Author Name <jingwei.zhang@intel.com>
*   History:
*   03-14-2016 Initial version. numerics svn rev. 12864
*/
	.file "llrintf_gen.c"
	.text
..TXTST0:
# -- Begin  llrintf
	.text
       .align    16,0x90
	.globl llrintf
llrintf:
# parameter 1: %xmm0
..B1.1:
	.cfi_startproc
..___tag_value_llrintf.1:
..L2:

        pushq     %rbp
	.cfi_def_cfa_offset 16
        movq      %rsp, %rbp
	.cfi_def_cfa 6, 16
	.cfi_offset 6, -16
        subq      $16, %rsp
        movq      $0, -16(%rbp)
        movl      $0, -8(%rbp)
..B1.2:
        pushq     %rax
        xorpd     %xmm1, %xmm1
        stmxcsr   -8(%rbp)
        andl      $-2, -8(%rbp)
        ldmxcsr   -8(%rbp)
        cvtss2si  %xmm0, %rax
        addss     %xmm0, %xmm1
        movq      %rax, -16(%rbp)
        stmxcsr   -8(%rbp)
        popq      %rax
..B1.3:
        movq      -16(%rbp), %rax
        movq      %rbp, %rsp
        popq      %rbp
	.cfi_restore 6
        ret       
        .align    16,0x90
	.cfi_endproc
	.type	llrintf,@function
	.size	llrintf,.-llrintf
	.data
# -- End  llrintf
	.data
	.section .note.GNU-stack, ""
// -- Begin DWARF2 SEGMENT .eh_frame
	.section .eh_frame,"a",@progbits
.eh_frame_seg:
	.align 1
# End
