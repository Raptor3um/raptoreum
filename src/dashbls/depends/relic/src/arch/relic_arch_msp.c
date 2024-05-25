/*
 * RELIC is an Efficient LIbrary for Cryptography
 * Copyright (c) 2012 RELIC Authors
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

/**
 * @file
 *
 * Implementation of MSP-dependent routines.
 *
 * @ingroup arch
 */

#include "relic_core.h"

/*============================================================================*/
/* Public definitions                                                         */
/*============================================================================*/

void arch_init(void) {
}

void arch_clean(void) {
}

#if TIMER == CYCLE

#ifdef __MSP430__
/* Support for MSPGCC with custom MSPsim simulator. */

static volatile uint8_t TEST_TEXTOUT __asm__("0x01b0");
static volatile unsigned int BENCH_CYCLES_0 __asm__("0x01b2");
static volatile unsigned int BENCH_CYCLES_1 __asm__("0x01b4");
static volatile unsigned int BENCH_CYCLES_2 __asm__("0x01b6");
static volatile unsigned int BENCH_CYCLES_3 __asm__("0x01b8");

union cycles_t {
    unsigned long long cycles;
    struct {
        unsigned int e0;
        unsigned int e1;
        unsigned int e2;
        unsigned int e3;
    } e;
};

int putchar(int c) {
    TEST_TEXTOUT = (char) c;
    return c;
}

unsigned long long arch_cycles(void) {
    union cycles_t cycles;
    cycles.e.e0 = BENCH_CYCLES_0;
    cycles.e.e1 = BENCH_CYCLES_1;
    cycles.e.e2 = BENCH_CYCLES_2;
    cycles.e.e3 = BENCH_CYCLES_3;
    return cycles.cycles;
}

#endif /* __MSP430__ */

#if __ICC430__
/* Support for IAR using simulator with custom macro. */

volatile ull_t  __cycles = 0;

ull_t arch_cycles(void) {
	return __cycles;
}

#endif /* __ICC430__ */

#endif /* TIMER = CYCLE */

unsigned int arch_lzcnt() {
    static const uint8_t table[16] = {
    	0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4
    };
#if WSIZE == 8
	if (a >> 4 == 0) {
		return table[a & 0xF];
	} else {
		return table[a >> 4] + 4;
	}
	return 0;
#elif WSIZE == 16
	int offset;

	if (a >= ((dig_t)1 << 8)) {
		offset = 8;
	} else {
		offset = 0;
	}
	a = a >> offset;
	if (a >> 4 == 0) {
		return table[a & 0xF] + offset;
	} else {
		return table[a >> 4] + 4 + offset;
	}
	return 0;
#endif
}
