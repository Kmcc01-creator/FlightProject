// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightReactiveUI - Reactive Data Binding for Slate (Refactored)
//
// This header is now a bridge including the decoupled reactive core
// and the Slate-specific integration.

#pragma once

#include "Core/FlightReactive.h"
#include "UI/FlightReactiveSlate.h"

namespace Flight::ReactiveUI
{
    // Re-export core types for backward compatibility in the ReactiveUI namespace
    using Flight::Reactive::TReactiveValue;
    using Flight::Reactive::TComputedValue;
    using Flight::Reactive::TStateContainer;
    using Flight::Reactive::TEffect;
}
