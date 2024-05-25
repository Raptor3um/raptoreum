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
 * Implementation of binary field trace function.
 *
 * @ingroup fb
 */

#include "relic_core.h"
#include "relic_fb.h"
#include "relic_fb_low.h"

/*============================================================================*/
/* Public definitions                                                         */
/*============================================================================*/

dig_t fb_trc_basic(const fb_t a) {
	int r = 0;
	fb_t t, u;

	fb_null(t);
	fb_null(u);

	RLC_TRY {
		fb_new(t);
		fb_new(u);

		fb_copy(t, a);
		fb_copy(u, a);
		for (int i = 1; i < RLC_FB_BITS; i++) {
			fb_sqr(t, t);
			fb_add(u, u, t);
		}

		r = u[0] & 1;
	}
	RLC_CATCH_ANY {
		RLC_THROW(ERR_CAUGHT);
	}
	RLC_FINALLY {
		fb_free(t);
		fb_free(u);
	}

	return r;
}

dig_t fb_trc_quick(const fb_t a) {
	return fb_trcn_low(a);
}
