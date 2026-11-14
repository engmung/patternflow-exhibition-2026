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
// A panel that has been with a hub and finds itself on another of its saved
// networks with no hub to hear tries its boot network again: this long after
// it got there, then every PF_ENSEMBLE_HOME_EVERY_MS. 0 = never.
#ifndef PF_ENSEMBLE_HOME_FIRST_MS
#define PF_ENSEMBLE_HOME_FIRST_MS 20000
#endif
#ifndef PF_ENSEMBLE_HOME_EVERY_MS
#define PF_ENSEMBLE_HOME_EVERY_MS 60000
#endif
// A panel that has not met a hub since it started still tries for this long
// after it started: switched on together, the panels are up before the hub's
// hotspot (13 s), take the next saved network and would stay there.
#ifndef PF_ENSEMBLE_HOME_AFTER_BOOT_MS
#define PF_ENSEMBLE_HOME_AFTER_BOOT_MS 180000
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
// 1 shows the pattern's name on the panel for a second when the pattern
// changes, as a panel on its own does. 0: the panels change pattern without
// a word, whoever changed it (the hub, the console, K4 on a panel).
#ifndef PF_ENSEMBLE_PATTERN_NAME
#define PF_ENSEMBLE_PATTERN_NAME 0
#endif

// Datagrams read per frame. The hub sends a panel at most 33 a second.
#ifndef PF_ENSEMBLE_RX_BUDGET
#define PF_ENSEMBLE_RX_BUDGET 6
#endif
