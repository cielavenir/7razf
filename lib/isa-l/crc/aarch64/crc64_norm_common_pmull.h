########################################################################
#  Copyright(c) 2019 Arm Corporation All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in
#      the documentation and/or other materials provided with the
#      distribution.
#    * Neither the name of Arm Corporation nor the names of its
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#########################################################################

#include "crc_common_pmull.h"

.macro crc64_norm_func name:req
	.arch armv8-a+crypto
	.text
	.align	3
	.global	cdecl(\name)
#ifndef __MACH__
	.type	\name, %function
#endif

/* uint64_t crc64_norm_func(uint64_t seed, const uint8_t * buf, uint64_t len) */

cdecl(\name\()):
	mvn	x_seed, x_seed
	mov	x_counter, 0
	cmp	x_len, (FOLD_SIZE-1)
	bhi	.crc_clmul_pre

.crc_tab_pre:
	cmp	x_len, x_counter
	bls	.done

#ifndef __MACH__
	adrp	x_tmp, .lanchor_crc_tab
	add	x_buf_iter, x_buf, x_counter
	add	x_buf, x_buf, x_len
	add	x_crc_tab_addr, x_tmp, :lo12:.lanchor_crc_tab
#else
	adrp	x_tmp, .lanchor_crc_tab@PAGE
	add	x_buf_iter, x_buf, x_counter
	add	x_buf, x_buf, x_len
	add	x_crc_tab_addr, x_tmp, .lanchor_crc_tab@PAGEOFF
#endif

	.align 3
.loop_crc_tab:
	ldrb	w_tmp, [x_buf_iter], 1
	cmp	x_buf, x_buf_iter
	eor	x_tmp, x_tmp, x_seed, lsr 56
	ldr	x_tmp, [x_crc_tab_addr, x_tmp, lsl 3]
	eor	x_seed, x_tmp, x_seed, lsl 8
	bne	.loop_crc_tab

.done:
	mvn	x_crc_ret, x_seed
	ret

	.align 2
.crc_clmul_pre:
	movi	v_x0.2s, 0
	fmov	v_x0.d[1], x_seed // save crc to v_x0

	crc_norm_load_first_block

	bls	.clmul_loop_end

	crc64_load_p4

// 1024bit --> 512bit loop
// merge x0, x1, x2, x3, y0, y1, y2, y3 => x0, x1, x2, x3 (uint64x2_t)
	crc_norm_loop

.clmul_loop_end:
// folding 512bit --> 128bit
	crc64_fold_512b_to_128b

// folding 128bit --> 64bit
	mov	x_tmp, p0_low_b0
	movk	x_tmp, p0_low_b1, lsl 16
	movk	x_tmp, p0_low_b2, lsl 32
	movk	x_tmp, p0_low_b3, lsl 48
	fmov	d_p0_high, x_tmp

	pmull2	v_tmp_high.1q, v_x3.2d, v_p0.2d
	movi	v_tmp_low.2s, 0
	ext	v_tmp_low.16b, v_tmp_low.16b, v_x3.16b, #8

	eor	v_x3.16b, v_tmp_high.16b, v_tmp_low.16b

// barrett reduction
	mov	x_tmp, br_low_b0
	movk	x_tmp, br_low_b1, lsl 16
	movk	x_tmp, br_low_b2, lsl 32
	movk	x_tmp, br_low_b3, lsl 48
	fmov	d_br_low2, x_tmp

	mov	x_tmp2, br_high_b0
	movk	x_tmp2, br_high_b1, lsl 16
	movk	x_tmp2, br_high_b2, lsl 32
	movk	x_tmp2, br_high_b3, lsl 48
	fmov	d_br_high2, x_tmp2

	pmull2	v_tmp_low.1q, v_x3.2d, v_br_low.2d
	eor	v_tmp_low.16b, v_x3.16b, v_tmp_low.16b
	pmull2	v_tmp_low.1q, v_tmp_low.2d, v_br_high.2d
	eor	v_x3.8b, v_x3.8b, v_tmp_low.8b
	umov	x_seed, v_x3.d[0]

	b	.crc_tab_pre

#ifndef __MACH__
	.size	\name, .-\name
	.section .rodata.cst16,"aM",@progbits,16
#else
	.section __DATA,data
#endif
	.align  4
.shuffle_data:
	.byte   15, 14, 13, 12, 11, 10, 9, 8
	.byte   7, 6, 5, 4, 3, 2, 1, 0
.endm
