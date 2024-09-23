/*
 * RELIC is an Efficient LIbrary for Cryptography
 * Copyright (c) 2013 RELIC Authors
 *
 * This file is part of RELIC. RELIC is legal property of its developers,
 * whose names are not listed here. Please refer to the COPYRIGHT file
 * for contact information.
 *
 * RELIC is free software; you can redistribute it and/or modify it under the
 * terms of the version 2.1 (or later) of the GNU Lesser General Public License
 * as published by the Free Software Foundation; or version 2.0 of the Apache
 * License as published by the Apache Software Foundation. See the LICENSE files
 * for more details.
 *
 * RELIC is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the LICENSE files for more details.
 *
 * You should have received a copy of the GNU Lesser General Public or the
 * Apache License along with RELIC. If not, see <https://www.gnu.org/licenses/>
 * or <https://www.apache.org/licenses/>.
 */

#include "relic_fp_low.h"

/**
 * @file
 *
 * Implementation of low-level prime field multiplication.
 *
 * @ingroup fp
 */

#include "macro.s"

.text
.global cdecl(fp_sqrn_low)

/*
 * Function: fp_sqrn_low
 * Inputs: rdi = c, rsi = a
 * Output: rax
 */
cdecl(fp_sqrn_low):
	xorq %r10,%r10
	movq 0(%rsi),%rax
	mulq 8(%rsi)
	addq %rax,%rax
	movq %rax,%r8
	adcq %rdx,%rdx
	movq %rdx,%r9
	adcq $0,%r10

	movq 0(%rsi),%rax
	mulq %rax
	movq %rax,0(%rdi)
	addq %rdx,%r8
	movq %r8,8(%rdi)
	adcq $0,%r9
	adcq $0,%r10

	xorq %rcx,%rcx
	movq 0(%rsi),%rax
	mulq 16(%rsi)
	addq %rax,%rax
	movq %rax,%r8
	adcq %rdx,%rdx
	movq %rdx,%r11
	adcq $0,%rcx

	movq 8(%rsi),%rax
	mulq 8(%rsi)
	addq %rax,%r8
	adcq %rdx,%r11
	adcq $0,%rcx

	movq 0(%rsi),%rax
	mulq 24(%rsi)
	addq %r9,%r8
	movq %r8,16(%rdi)
	adcq %r10,%r11
	adcq $0,%rcx
	movq %rax,%r8
	movq %rdx,%r10

	xorq %r9,%r9
	movq 8(%rsi),%rax
	mulq 16(%rsi)
	addq %rax,%r8
	adcq %rdx,%r10
	adcq $0,%r9
	addq %r8,%r8
	adcq %r10,%r10
	adcq %r9,%r9

	movq 8(%rsi),%rax
	mulq 24(%rsi)
	addq %r11,%r8
	movq %r8,24(%rdi)
	adcq %rcx,%r10
	adcq $0,%r9
	xorq %rcx,%rcx
	addq %rax,%rax
	movq %rax,%r8
	adcq %rdx,%rdx
	movq %rdx,%r11
	adcq $0,%rcx

	movq 16(%rsi),%rax
	mulq 16(%rsi)
	addq %r10,%r8
	adcq %r11,%r9
	adcq $0,%rcx
	addq %rax,%r8
	movq %r8,32(%rdi)
	adcq %rdx,%r9
	adcq $0,%rcx

	xorq %r11,%r11
	movq 16(%rsi),%rax
	mulq 24(%rsi)
	addq %rax,%rax
	adcq %rdx,%rdx
	adcq $0,%r11
	addq %rax,%r9
	movq %r9,40(%rdi)
	adcq %rdx,%rcx
	adcq $0,%r11

	movq 24(%rsi),%rax
	mulq %rax
	addq %rax,%rcx
	movq %rcx,48(%rdi)
	adcq %rdx,%r11
	movq %r11,56(%rdi)
	ret
