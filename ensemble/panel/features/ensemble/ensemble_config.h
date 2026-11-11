// ═══════════════════════════════════════════════════════════
// PatternFlow - ensemble feature: compile-time defaults
//
// Read only when a composition carries features/ensemble/. Every value is
// #ifndef-guarded: patternflow_secrets.h (per device) and the composition's
// overrides.h are included before this through config.h, so whatever they
// define wins.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once
#include "../../config.h"

// Where the hub is. Empty (the default) finds it: the panel calls out on the
// network's broadcast address until a hub answers, then talks to that one,
// and calls out again if it falls silent. That works on the hub's own
// hotspot and on a router alike. Set an address only for a network that
// does not pass broadcasts.
#ifndef PF_ENSEMBLE_HOST
#define PF_ENSEMBLE_HOST ""
#endif
// The hub's UDP port (PFE/1, ensemble/PROTOCOL.md).
#ifndef PF_ENSEMBLE_PORT
#define PF_ENSEMBLE_PORT 7400
#endif
// The port this panel sends from and listens on. The hub answers whatever
// port a report came from, so this only has to be free.
#ifndef PF_ENSEMBLE_LOCAL_PORT
#define PF_ENSEMBLE_LOCAL_PORT 7401
#endif
// A hub not heard for this long is looked for again.
#ifndef PF_ENSEMBLE_HUB_LOST_MS
#define PF_ENSEMBLE_HUB_LOST_MS 5000
#endif

// Detents from one end of a knob to the other. 32 is about a turn and a
// half, the exhibition panel's feel (PF_EXHIBIT_KNOB_DETENTS).
#ifndef PF_ENSEMBLE_KNOB_DETENTS
#define PF_ENSEMBLE_KNOB_DETENTS 32
#endif
// K4's role until a hub says otherwise: 1 = it steps through the patterns,
// 0 = it is a fourth knob.
#ifndef PF_ENSEMBLE_K4_PATTERN
#define PF_ENSEMBLE_K4_PATTERN 1
#endif
// K4 detents per pattern step, until a hub says otherwise.
#ifndef PF_ENSEMBLE_K4_STEPS
#define PF_ENSEMBLE_K4_STEPS 3
#endif
// Datagrams read per frame. The hub sends a panel at most 33 a second.
#ifndef PF_ENSEMBLE_RX_BUDGET
#define PF_ENSEMBLE_RX_BUDGET 6
#endif
