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
 * Implementation of the low-level binary field addition and subtraction
 * functions.
 *
 * @ingroup fb
 */

#include <xmmintrin.h>
#include <tmmintrin.h>
#ifdef __PCLMUL__
#include <wmmintrin.h>
#endif

#include <tmmintrin.h>
#include "relic_fb.h"
#include "relic_fb_low.h"

#include "macros.h"

/*============================================================================*/
/* Public definitions                                                         */
/*============================================================================*/

void fb_add1_low(dig_t *c, const dig_t *a, dig_t digit) {
	int i;

	(*c) = (*a) ^ digit;
	c++;
	a++;
	for (i = 0; i < RLC_FB_DIGS - 1; i++, a++, c++)
		(*c) = (*a);
}

void fb_addn_low(dig_t *c, const dig_t *a, const dig_t *b) {
	*(__m128i *)c = XOR(*(__m128i*)(a), *(__m128i*)(b));
	*(__m128i *)(c + 2) = XOR(*(__m128i*)(a + 2), *(__m128i*)(b + 2));
}

void fb_addd_low(dig_t *c, const dig_t *a, const dig_t *b, int size) {
	if (size == 2 * RLC_FB_DIGS) {
		*(__m128i *)c = XOR(*(__m128i*)(a), *(__m128i*)(b));
		*(__m128i *)(c + 2) = XOR(*(__m128i*)(a + 2), *(__m128i*)(b + 2));
		*(__m128i *)(c + 4) = XOR(*(__m128i*)(a + 4), *(__m128i*)(b + 4));
		*(__m128i *)(c + 6) = XOR(*(__m128i*)(a + 6), *(__m128i*)(b + 6));
	} else {
		for (int i = 0; i < size; i++, a++, b++, c++) {
			(*c) = (*a) ^ (*b);
		}
	}
}
