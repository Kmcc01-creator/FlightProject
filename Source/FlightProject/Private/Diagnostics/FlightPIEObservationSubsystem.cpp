#include "Diagnostics/FlightPIEObservationSubsystem.h"

#include "FlightProject.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace
{

static TAutoConsoleVariable<int32> CVarFlightTracePIEActorsEnabled(
	TEXT("Flight.Trace.PIEActors.Enabled"),
	1,
	TEXT("Enable PIE actor origin tracing (1=on, 0=off)."));

static TAutoConsoleVariable<int32> CVarFlightTracePIEActorsAutoExport(
	TEXT("Flight.Trace.PIEActors.AutoExport"),
	1,
	TEXT("Auto-export PIE actor trace on PIE world teardown (1=on, 0=off)."));

static TAutoConsoleVariable<int32> CVarFlightTracePIEActorsLogEach(
	TEXT("Flight.Trace.PIEActors.LogEachEvent"),
	0,
	TEXT("Log every observed PIE actor event (1=on, 0=off)."));

FString WorldTypeToString(const EWorldType::Type WorldType)
{
	switch (WorldType)
	{
	case EWorldType::None:
		return TEXT("None");
	case EWorldType::Game:
		return TEXT("Game");
	case EWorldType::Editor:
		return TEXT("Editor");
	case EWorldType::PIE:
		return TEXT("PIE");
	case EWorldType::EditorPreview:
		return TEXT("EditorPreview");
	case EWorldType::GamePreview:
		return TEXT("GamePreview");
	case EWorldType::GameRPC:
		return TEXT("GameRPC");
	case EWorldType::Inactive:
		return TEXT("Inactive");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> MakeEventJson(const FFlightObservedActorEvent& Event)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("eventType"), Event.EventType);
	Object->SetStringField(TEXT("actorName"), Event.ActorName);
	Object->SetStringField(TEXT("actorLabel"), Event.ActorLabel);
	Object->SetStringField(TEXT("actorClassPath"), Event.ActorClassPath);
	Object->SetStringField(TEXT("actorModulePath"), Event.ActorModulePath);
	Object->SetStringField(TEXT("levelPath"), Event.LevelPath);
	Object->SetStringField(TEXT("packagePath"), Event.PackagePath);
	Object->SetStringField(TEXT("ownerName"), Event.OwnerName);
	Object->SetStringField(TEXT("instigatorName"), Event.InstigatorName);
	Object->SetStringField(TEXT("sourceHint"), Event.SourceHint);
	Object->SetBoolField(TEXT("wasLoaded"), Event.bWasLoaded);
	Object->SetNumberField(TEXT("wallSecondsSinceTraceStart"), Event.WallSecondsSinceTraceStart);
	Object->SetNumberField(TEXT("worldSeconds"), Event.WorldSeconds);
	return Object;
}

} // namespace

bool UFlightPIEObservationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::PIE;
}

void UFlightPIEObservationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!CVarFlightTracePIEActorsEnabled.GetValueOnGameThread())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TraceStartWallSeconds = FPlatformTime::Seconds();
	bTracingActive = true;

	SpawnedHandle = World->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &UFlightPIEObservationSubsystem::HandleActorSpawned));

	CaptureInitialActors();

	UE_LOG(
		LogFlightProject,
		Log,
		TEXT("PIE observation tracing enabled for world '%s'"),
		*World->GetName());
}

void UFlightPIEObservationSubsystem::Deinitialize()
{
	UWorld* World = GetWorld();
	if (World && SpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(SpawnedHandle);
		SpawnedHandle.Reset();
	}

	if (bTracingActive && CVarFlightTracePIEActorsAutoExport.GetValueOnGameThread())
	{
		const FString ExportPath = ExportObservationSnapshot(BuildDefaultRelativeExportPath());
		if (!ExportPath.IsEmpty())
		{
			UE_LOG(
				LogFlightProject,
				Log,
				TEXT("PIE observation trace exported: %s"),
				*ExportPath);
		}
	}

	bTracingActive = false;
	Events.Reset();

	Super::Deinitialize();
}

void UFlightPIEObservationSubsystem::CaptureInitialActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		RecordActorEvent(*It, TEXT("InitialActor"));
	}
}

void UFlightPIEObservationSubsystem::HandleActorSpawned(AActor* SpawnedActor)
{
	RecordActorEvent(SpawnedActor, TEXT("SpawnedActor"));
}

void UFlightPIEObservationSubsystem::RecordActorEvent(AActor* Actor, const TCHAR* EventType)
{
	if (!Actor || !bTracingActive)
	{
		return;
	}

	FFlightObservedActorEvent Event;
	Event.EventType = EventType ? EventType : TEXT("Unknown");
	Event.ActorName = Actor->GetName();
#if WITH_EDITOR
	Event.ActorLabel = Actor->GetActorLabel();
#endif
	if (UClass* ActorClass = Actor->GetClass())
	{
		Event.ActorClassPath = ActorClass->GetPathName();
		if (UPackage* ClassPackage = ActorClass->GetOutermost())
		{
			Event.ActorModulePath = ClassPackage->GetName();
		}
	}

	if (ULevel* Level = Actor->GetLevel())
	{
		Event.LevelPath = Level->GetPathName();
	}
	if (UPackage* Package = Actor->GetPackage())
	{
		Event.PackagePath = Package->GetPathName();
	}

	Event.OwnerName = GetNameSafe(Actor->GetOwner());
	Event.InstigatorName = GetNameSafe(Actor->GetInstigator());
	Event.bWasLoaded = Actor->HasAnyFlags(RF_WasLoaded);
	Event.WallSecondsSinceTraceStart = FPlatformTime::Seconds() - TraceStartWallSeconds;
	if (UWorld* World = GetWorld())
	{
		Event.WorldSeconds = World->GetTimeSeconds();
	}

	if (Event.bWasLoaded)
	{
		Event.SourceHint = TEXT("LoadedFromMapOrAsset");
	}
	else if (!Event.OwnerName.IsEmpty() && Event.OwnerName != TEXT("None"))
	{
		Event.SourceHint = TEXT("SpawnedWithOwner");
	}
	else
	{
		Event.SourceHint = TEXT("SpawnedAtRuntime");
	}

	if (CVarFlightTracePIEActorsLogEach.GetValueOnGameThread())
	{
		UE_LOG(
			LogFlightProject,
			Log,
			TEXT("PIETrace[%s] Actor=%s Class=%s Source=%s WorldT=%.3f"),
			*Event.EventType,
			*Event.ActorName,
			*Event.ActorClassPath,
			*Event.SourceHint,
			Event.WorldSeconds);
	}

	Events.Add(MoveTemp(Event));
}

FString UFlightPIEObservationSubsystem::BuildDefaultRelativeExportPath() const
{
	const FString TimeStamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
	return FString::Printf(TEXT("Saved/Flight/Observations/pie_entity_trace_%s.json"), *TimeStamp);
}

FString UFlightPIEObservationSubsystem::ExportObservationSnapshot(const FString& RelativeOutputPath) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FString();
	}

	if (Events.Num() == 0)
	{
		return FString();
	}

	FString AbsolutePath = RelativeOutputPath;
	if (FPaths::IsRelative(AbsolutePath))
	{
		AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), AbsolutePath);
	}

	const FString DirectoryPath = FPaths::GetPath(AbsolutePath);
	if (!DirectoryPath.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*DirectoryPath, true);
	}

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("schemaVersion"), TEXT("0.1"));
	RootObject->SetStringField(TEXT("generatedAtUtc"), FDateTime::UtcNow().ToIso8601());
	RootObject->SetStringField(TEXT("worldName"), World->GetName());
	RootObject->SetStringField(TEXT("worldType"), WorldTypeToString(World->WorldType));
	RootObject->SetNumberField(TEXT("eventCount"), Events.Num());

	TArray<TSharedPtr<FJsonValue>> EventRows;
	EventRows.Reserve(Events.Num());
	for (const FFlightObservedActorEvent& Event : Events)
	{
		EventRows.Add(MakeShared<FJsonValueObject>(MakeEventJson(Event)));
	}
	RootObject->SetArrayField(TEXT("events"), EventRows);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter);

	if (!FFileHelper::SaveStringToFile(JsonText, *AbsolutePath))
	{
		UE_LOG(
			LogFlightProject,
			Error,
			TEXT("Failed to write PIE observation snapshot '%s'"),
			*AbsolutePath);
		return FString();
	}

	return AbsolutePath;
}
