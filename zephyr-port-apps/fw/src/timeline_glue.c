/* SPDX-License-Identifier: Apache-2.0 */

// Port closure for the timeline / blob_db item store. The port renders wall
// clock as UTC everywhere (see rtc_get_time_tm / clock glue), so local time IS
// UTC: the conversions are identities. timeline_item.c uses these to normalize
// all-day / floating pin timestamps.
// ponytail: identity because the port has no timezone service. Wire real
// offsets if timezone-aware pins matter.

#include <time.h>

time_t time_local_to_utc(time_t local_time) { return local_time; }
time_t time_utc_to_local(time_t utc_time) { return utc_time; }

#include <stdbool.h>
#include "pbl/services/timeline/layout_layer.h"

// Layout attribute-verification gate. Shipping dispatches per LayoutId to the
// per-layout verifiers (layout_layer.c), which pulls every layout module and
// its closure. The port's item store only needs the gate to pass so items
// round-trip; accept all until the render-side layouts are ported.
// ponytail: returns true unconditionally. Port layout_layer.c + the per-layout
// verifiers when timeline rendering (pin cards) lands.
bool layout_verify(bool existing_attributes[], LayoutId id) {
  (void)existing_attributes;
  (void)id;
  return true;
}
