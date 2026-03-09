#include "UI/FlightLogCapture.h"

namespace Flight::Log
{

FLogCaptureDevice::FLogCaptureDevice()
    : StartTime(FPlatformTime::Seconds())
{
}

FLogCaptureDevice::~FLogCaptureDevice()
{
    Detach();
}

void FLogCaptureDevice::Attach()
{
    if (!bAttached)
    {
        GLog->AddOutputDevice(this);
        bAttached = true;
    }
}

void FLogCaptureDevice::Detach()
{
    if (bAttached)
    {
        GLog->RemoveOutputDevice(this);
        bAttached = false;
    }
}

void FLogCaptureDevice::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
    if (bIsLoggingInternal)
    {
        return;
    }

    const double Timestamp = FPlatformTime::Seconds() - StartTime;

    FLogEntry Entry(
        0,
        Timestamp,
        FromUE(Verbosity),
        Category,
        FString(V)
    );
    Entry.FrameNumber = GFrameNumber;
    Entry.ThreadId = FPlatformTLS::GetCurrentThreadId();

    const FLogEntry CapturedEntry = Buffer.Add(MoveTemp(Entry));
    OnLogReceived.Broadcast(CapturedEntry);
}

void FLogCaptureDevice::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time)
{
    Serialize(V, Verbosity, Category);
}

FLogCaptureDevice& FGlobalLogCapture::Get()
{
    static FLogCaptureDevice Instance;
    return Instance;
}

void FGlobalLogCapture::Initialize()
{
    Get().Attach();
}

void FGlobalLogCapture::Shutdown()
{
    Get().Detach();
}

} // namespace Flight::Log
