// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - which features this firmware carries
//
// A panel that plays with others: a hub on the same Wi-Fi links their knobs
// and their patterns (features/ensemble/core_ensemble.h).
//
//   ensemble   one position per knob, shown as a lane and shared with the
//              hub; K4 steps the pattern when the hub says so.
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

// First, and it has to be if anything is added after it: fillInput reaches
// this feature holding the physical encoders and nothing else, so what it
// turns into knob positions is a hand's detents and only those.
#define PF_FEATURE_LIST &PFFeatureEnsemble::descriptor
