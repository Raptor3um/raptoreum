/*
 * RELIC is an Efficient LIbrary for Cryptography
 * Copyright (c) 2009 RELIC Authors
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
 * Implementation of the low-level binary field bit shifting functions.
 *
 * @ingroup fb
 */

#include "relic_fb.h"
#include "relic_util.h"
#include "relic_fb_low.h"

/*============================================================================*/
/* Public definitions                                                         */
/*============================================================================*/

dig_t fb_lsh1_low(dig_t *c, const dig_t *a) {
	int i;
	dig_t r, carry;

	/* Prepare the bit mask. */
	carry = 0;
	for (i = 0; i < RLC_FB_DIGS; i++, a++, c++) {
		/* Get the most significant bit. */
		r = *a >> (RLC_DIG - 1);
		/* Shift the operand and insert the carry, */
		*c = (*a << 1) | carry;
		/* Update the carry. */
		carry = r;
	}
	return carry;
}

dig_t fb_lshb_low(dig_t *c, const dig_t *a, int bits) {
	int i;
	dig_t r, carry, mask, shift;

	/* Prepare the bit mask. */
	shift = RLC_DIG - bits;
	carry = 0;
	mask = RLC_MASK(bits);
	for (i = 0; i < RLC_FB_DIGS; i++, a++, c++) {
		/* Get the needed least significant bits. */
		r = ((*a) >> shift) & mask;
		/* Shift left the operand. */
		*c = ((*a) << bits) | carry;
		/* Update the carry. */
		carry = r;
	}
	return carry;
}

dig_t fb_rsh1_low(dig_t *c, const dig_t *a) {
	int i;
	dig_t r, carry;

	c += RLC_FB_DIGS - 1;
	a += RLC_FB_DIGS - 1;
	carry = 0;
	for (i = RLC_FB_DIGS - 1; i >= 0; i--, a--, c--) {
		/* Get the least significant bit. */
		r = *a & 0x01;
		/* Shift the operand and insert the carry. */
		carry <<= RLC_DIG - 1;
		*c = (*a >> 1) | carry;
		/* Update the carry. */
		carry = r;
	}
	return carry;
}

dig_t fb_rshb_low(dig_t *c, const dig_t *a, int bits) {
	int i;
	dig_t r, carry, mask, shift;

	c += RLC_FB_DIGS - 1;
	a += RLC_FB_DIGS - 1;
	/* Prepare the bit mask. */
	shift = RLC_DIG - bits;
	carry = 0;
	mask = RLC_MASK(bits);
	for (i = RLC_FB_DIGS - 1; i >= 0; i--, a--, c--) {
		/* Get the needed least significant bits. */
		r = (*a) & mask;
		/* Shift left the operand. */
		*c = ((*a) >> bits) | (carry << shift);
		/* Update the carry. */
		carry = r;
	}
	return carry;
}

dig_t fb_lsha_low(dig_t *c, const dig_t *a, int bits, int size) {
	int i, j;
	dig_t b1, b2;

	j = RLC_DIG - bits;
	b1 = a[0];
	c[0] ^= (b1 << bits);
	if (size == RLC_FB_DIGS) {
		for (i = 1; i < RLC_FB_DIGS; i++) {
			b2 = a[i];
			c[i] ^= ((b2 << bits) | (b1 >> j));
			b1 = b2;
		}
	} else {
		for (i = 1; i < size; i++) {
			b2 = a[i];
			c[i] ^= ((b2 << bits) | (b1 >> j));
			b1 = b2;
		}
	}
	return (b1 >> j);
}
