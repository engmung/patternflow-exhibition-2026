// ═══════════════════════════════════════════════════════════
// PatternFlow - the ensemble, as a feature
//
// Several panels, one hub, and a hand on any of them moving the others.
// core_ensemble.h says what it does; this is only the descriptor.
//
// Hooks used: setup, onNetwork, loop, fillInput, claimsPattern, takePattern,
// appendStatus, drawOverlay.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "../pf_feature.h"
#include "core_ensemble.h"

namespace PFFeatureEnsemble {

inline const PFFeature descriptor = {
    "ensemble",
    "ensemble",    // cap - /api/status says this panel plays in an ensemble
    PatternflowEnsemble::setup,
    PatternflowEnsemble::onNetwork,
    PatternflowEnsemble::loop,
    nullptr,       // observeFrame
    PatternflowEnsemble::fillInput,
    nullptr,       // onUserInput
    PatternflowEnsemble::claimsPattern,   // true only while handing over the hub's pattern
    PatternflowEnsemble::takePattern,
    nullptr,       // onSleep
    nullptr,       // requestSleep
    nullptr,       // shortName - not listed on the NETWORK screen
    nullptr,       // isRuntimeEnabled
    nullptr,       // setRuntimeEnabled
    PatternflowEnsemble::appendStatus,
    PatternflowEnsemble::drawOverlay,     // the panel's number, when the console asks
    nullptr,       // navPath
    nullptr,       // navLabel
    nullptr,       // navDesc
    nullptr,       // composeFrame
    nullptr,       // handleSelectInput
    nullptr,       // drawSelect
    nullptr,       // decorateSelect
};

}  // namespace PFFeatureEnsemble
