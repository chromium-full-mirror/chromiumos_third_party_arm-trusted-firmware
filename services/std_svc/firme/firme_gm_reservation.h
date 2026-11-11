/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_GM_RESERVATION_H
#define FIRME_GM_RESERVATION_H

#include <stdint.h>

struct firme_gm_reservation;

/*
 * An opaque reservation handle identifies a registry-owned target PAR address.
 * Handles have no PE affinity.
 * Callers must not modify it and must serialize its use, handoff and release.
 * Do not retain aliases after release: a stale alias cannot be distinguished
 * from a reused slot. Calls must not be reentered from an interrupt on the same
 * PE. Reservations must be released before returning to the SMC caller.
 *
 * Only bookkeeping runs under the registry lock. CREATE and DESTROY retain
 * the target reservation through maintenance and any rollback. Reservations
 * exclude other lifecycle operations on that target, not donor transitions.
 * Callers validate the target PAR address before reserving it.
 *
 * Reserve one target address. *reservation must be NULL.
 * FIRME_GM_RESERVATIONS_MAX sets the slot count. Failure leaves output unchanged:
 * -EINVAL: invalid arguments; -EBUSY: registry full or bookkeeping lock busy;
 * -EINPROGRESS: another lifecycle operation has reserved the same target.
 */
int firme_gm_reserve(uint64_t target_addr,
		     struct firme_gm_reservation **reservation);

/*
 * Release and clear *reservation. NULL cleanup is harmless; any invalid
 * non-NULL handle is a fatal internal invariant violation. Release may wait
 * for the short bookkeeping lock; acquisition tries it without waiting.
 */
void firme_gm_release(struct firme_gm_reservation **reservation);

#endif /* FIRME_GM_RESERVATION_H */
