// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - which features this firmware carries
//
// A panel that plays with others: a hub on the same Wi-Fi links their knobs
// and their patterns (features/ensemble/core_ensemble.h).
//
//   ensemble   one position per knob, shown as a lane and shared with the
//              hub; K4 steps the pattern when the hub says so.
//   show       the Performance edition's player: a .pfs table stored on the
//              panel, played and looped by the panel itself. The console
//              puts shows on a panel and starts and stops them, through the
//              hub (PUT /api/shows, POST /api/shows/control).
//
// Who leads is decided: while a show plays, the show does. The panel stands
// outside the ensemble for that long - its knobs move nobody, the hub's
// values wait - and takes what the ensemble shows when the show ends.
//
// Nothing else, on purpose. OSC, MQTT and network MIDI each move the knobs
// or the pattern from somewhere else, and a panel with two masters follows
// neither. Add one only after deciding which of them leads.
//
// Not on the shelf and not in build.sh's composition list: it is built by
// name, ./firmware/bundles/build.sh ensemble.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "ensemble/feature_ensemble.h"
#include "show/feature_show.h"

// The player's tables at this bundle's sizes (overrides.h). If this fails,
// the checkout's show player was not prepared: run ensemble/panel/install.py.
static_assert(PatternflowShow::PLAYLIST_MAX == PF_SHOW_PLAYLIST_MAX &&
              PatternflowShowHttp::SHOW_CACHE_MAX == PF_SHOW_CACHE_MAX,
              "the show player ignores PF_SHOW_PLAYLIST_MAX / PF_SHOW_CACHE_MAX: run ensemble/panel/install.py on this checkout");

// The ensemble asks the show player whether it has the panel.
namespace PFEnsembleShow {
inline const bool wired = (PatternflowEnsemble::soloNow = [] { return PatternflowShow::isPlaying(); }, true);
}

// The ensemble first, and it has to be: fillInput reaches it holding the
// physical encoders and nothing else, so what it turns into knob positions
// is a hand's detents and only those. The show player last: it claims the
// pattern while a sequence runs.
#define PF_FEATURE_LIST &PFFeatureEnsemble::descriptor, &PFFeatureShow::descriptor
