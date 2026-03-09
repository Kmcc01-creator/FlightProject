// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightLogging Implementation

#include "Core/FlightLogging.h"
#include "UI/FlightLogCapture.h"

namespace Flight::Logging
{

void FInternalLogService::Receive(const FLogEntry& Entry, const FLogContext& Context)
{
    // Directly add to the global log buffer managed by the capture device
    // This allows the reactive UI to see structured logs from FLogger
    Flight::Log::FGlobalLogCapture::Get().GetBuffer().Add(Entry);
}

} // namespace Flight::Logging
