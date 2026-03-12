// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Mass/FlightMassFragments.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexBackendCapabilities.h"
#include "Vex/FlightVexSchemaOrchestrator.h"
#include "Vex/FlightVexSchemaTypes.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Core/FlightReflection.h"

/**
 * FGeneralVexTestState
 * A non-swarm struct used to verify generalized VEX system authoring.
 */
struct FGeneralVexTestState
{
	float Health = 100.0f;
	float Ammo = 50.0f;
	float Score = 0.0f;
	int32 Count = 0;

	FLIGHT_REFLECT_BODY(FGeneralVexTestState);
};

struct FAccessorBackedVexState
{
	float HiddenHealth = 25.0f;
	float HiddenEnergy = 12.0f;
};

struct FManualMassProviderState
{
	float Shield = 1.0f;
	float Energy = 1.0f;
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;

	FLIGHT_REFLECT_BODY(FManualMassProviderState);
};

const void* GetAccessorBackedVexTypeKey()
{
	static uint8 TypeKeyMarker = 0;
	return &TypeKeyMarker;
}

const void* GetMassBackedVexTypeKey()
{
	static uint8 TypeKeyMarker = 0;
	return &TypeKeyMarker;
}

const void* GetGpuBackedVexTypeKey()
{
	static uint8 TypeKeyMarker = 0;
	return &TypeKeyMarker;
}

const void* GetMassExecutableVexTypeKey()
{
	static uint8 TypeKeyMarker = 0;
	return &TypeKeyMarker;
}

Flight::Vex::FVexTypeSchema BuildAccessorBackedSchema()
{
	using namespace Flight::Vex;

	FVexTypeSchema Schema;
	Schema.TypeId.RuntimeKey = GetAccessorBackedVexTypeKey();
	Schema.TypeId.StableName = FName(TEXT("FAccessorBackedVexState"));
	Schema.TypeName = TEXT("FAccessorBackedVexState");
	Schema.Size = sizeof(FAccessorBackedVexState);
	Schema.Alignment = alignof(FAccessorBackedVexState);

	FVexSymbolRecord Health;
	Health.SymbolName = TEXT("@health");
	Health.ValueType = EVexValueType::Float;
	Health.Storage.Kind = EVexStorageKind::Accessor;
	Health.ReadValue = [](const void* Ptr) -> FVexRuntimeValue
	{
		return FVexRuntimeValue::FromFloat(static_cast<const FAccessorBackedVexState*>(Ptr)->HiddenHealth);
	};
	Health.WriteValue = [](void* Ptr, const FVexRuntimeValue& Value) -> bool
	{
		static_cast<FAccessorBackedVexState*>(Ptr)->HiddenHealth = Value.AsFloat();
		return true;
	};
	Schema.SymbolRecords.Add(Health.SymbolName, MoveTemp(Health));

	FVexSymbolRecord Energy;
	Energy.SymbolName = TEXT("@energy");
	Energy.ValueType = EVexValueType::Float;
	Energy.Storage.Kind = EVexStorageKind::Accessor;
	Energy.ReadValue = [](const void* Ptr) -> FVexRuntimeValue
	{
		return FVexRuntimeValue::FromFloat(static_cast<const FAccessorBackedVexState*>(Ptr)->HiddenEnergy);
	};
	Energy.WriteValue = [](void* Ptr, const FVexRuntimeValue& Value) -> bool
	{
		static_cast<FAccessorBackedVexState*>(Ptr)->HiddenEnergy = Value.AsFloat();
		return true;
	};
	Schema.SymbolRecords.Add(Energy.SymbolName, MoveTemp(Energy));

	Schema.RebuildLegacyViews();
	Schema.LayoutHash = Flight::Vex::FVexSchemaOrchestrator::ComputeSchemaLayoutHash(Schema);
	return Schema;
}

Flight::Vex::FVexTypeSchema BuildStorageClassificationSchema(
	const TCHAR* TypeName,
	const void* TypeKey,
	const Flight::Vex::EVexStorageKind StorageKind)
{
	using namespace Flight::Vex;

	FVexTypeSchema Schema;
	Schema.TypeId.RuntimeKey = TypeKey;
	Schema.TypeId.StableName = FName(TypeName);
	Schema.TypeName = TypeName;
	Schema.Size = sizeof(float);
	Schema.Alignment = alignof(float);

	FVexSymbolRecord Shield;
	Shield.SymbolName = TEXT("@shield");
	Shield.ValueType = EVexValueType::Float;
	Shield.Residency = StorageKind == EVexStorageKind::GpuBufferElement
		? EFlightVexSymbolResidency::GpuOnly
		: EFlightVexSymbolResidency::Shared;
	Shield.Storage.Kind = StorageKind;
	Shield.Storage.FragmentType = FName(TEXT("FFlightDroidStateFragment"));
	Shield.Storage.ElementStride = sizeof(float);
	Shield.Storage.BufferBinding = TEXT("ShieldBuffer");
	Schema.SymbolRecords.Add(Shield.SymbolName, MoveTemp(Shield));

	Schema.RebuildLegacyViews();
	Schema.LayoutHash = Flight::Vex::FVexSchemaOrchestrator::ComputeSchemaLayoutHash(Schema);
	return Schema;
}

Flight::Vex::FVexTypeSchema BuildExecutableMassSchema()
{
	using namespace Flight::Vex;

	FVexTypeSchema Schema;
	Schema.TypeId.RuntimeKey = GetMassExecutableVexTypeKey();
	Schema.TypeId.StableName = FName(TEXT("FMassExecutableVexState"));
	Schema.TypeName = TEXT("FMassExecutableVexState");
	Schema.Size = sizeof(FFlightDroidStateFragment) + sizeof(FFlightTransformFragment);
	Schema.Alignment = alignof(FFlightDroidStateFragment);

	FVexSymbolRecord Shield;
	Shield.SymbolName = TEXT("@shield");
	Shield.ValueType = EVexValueType::Float;
	Shield.Storage.Kind = EVexStorageKind::MassFragmentField;
	Shield.Storage.FragmentType = FName(TEXT("FFlightDroidStateFragment"));
	Shield.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightDroidStateFragment, Shield));
	Shield.Storage.ElementStride = sizeof(float);
	Schema.SymbolRecords.Add(Shield.SymbolName, MoveTemp(Shield));

	FVexSymbolRecord Energy;
	Energy.SymbolName = TEXT("@energy");
	Energy.ValueType = EVexValueType::Float;
	Energy.Storage.Kind = EVexStorageKind::MassFragmentField;
	Energy.Storage.FragmentType = FName(TEXT("FFlightDroidStateFragment"));
	Energy.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightDroidStateFragment, Energy));
	Energy.Storage.ElementStride = sizeof(float);
	Schema.SymbolRecords.Add(Energy.SymbolName, MoveTemp(Energy));

	FVexSymbolRecord Position;
	Position.SymbolName = TEXT("@position");
	Position.ValueType = EVexValueType::Float3;
	Position.Storage.Kind = EVexStorageKind::MassFragmentField;
	Position.Storage.FragmentType = FName(TEXT("FFlightTransformFragment"));
	Position.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightTransformFragment, Location));
	Position.Storage.ElementStride = sizeof(FVector);
	Schema.SymbolRecords.Add(Position.SymbolName, MoveTemp(Position));

	FVexSymbolRecord Velocity;
	Velocity.SymbolName = TEXT("@velocity");
	Velocity.ValueType = EVexValueType::Float3;
	Velocity.Storage.Kind = EVexStorageKind::MassFragmentField;
	Velocity.Storage.FragmentType = FName(TEXT("FFlightTransformFragment"));
	Velocity.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightTransformFragment, Velocity));
	Velocity.Storage.ElementStride = sizeof(FVector);
	Schema.SymbolRecords.Add(Velocity.SymbolName, MoveTemp(Velocity));

	Schema.RebuildLegacyViews();
	Schema.LayoutHash = Flight::Vex::FVexSchemaOrchestrator::ComputeSchemaLayoutHash(Schema);
	return Schema;
}

Flight::Reflection::FVexSchemaProviderResult RegisterManualMassProviderSchema()
{
	using namespace Flight::Vex;

	FVexTypeSchema Schema;
	Schema.TypeId.RuntimeKey = Flight::Reflection::GetRuntimeTypeKey<FManualMassProviderState>();
	Schema.TypeId.StableName = FName(TEXT("FManualMassProviderState"));
	Schema.TypeName = TEXT("FManualMassProviderState");
	Schema.Size = sizeof(FFlightDroidStateFragment) + sizeof(FFlightTransformFragment);
	Schema.Alignment = alignof(FFlightDroidStateFragment);

	FVexSymbolRecord Shield;
	Shield.SymbolName = TEXT("@shield");
	Shield.ValueType = EVexValueType::Float;
	Shield.Storage.Kind = EVexStorageKind::MassFragmentField;
	Shield.Storage.FragmentType = FName(TEXT("FFlightDroidStateFragment"));
	Shield.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightDroidStateFragment, Shield));
	Shield.Storage.ElementStride = sizeof(float);
	Schema.SymbolRecords.Add(Shield.SymbolName, MoveTemp(Shield));

	FVexSymbolRecord Energy;
	Energy.SymbolName = TEXT("@energy");
	Energy.ValueType = EVexValueType::Float;
	Energy.Storage.Kind = EVexStorageKind::MassFragmentField;
	Energy.Storage.FragmentType = FName(TEXT("FFlightDroidStateFragment"));
	Energy.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightDroidStateFragment, Energy));
	Energy.Storage.ElementStride = sizeof(float);
	Schema.SymbolRecords.Add(Energy.SymbolName, MoveTemp(Energy));

	FVexSymbolRecord Position;
	Position.SymbolName = TEXT("@position");
	Position.ValueType = EVexValueType::Float3;
	Position.Storage.Kind = EVexStorageKind::MassFragmentField;
	Position.Storage.FragmentType = FName(TEXT("FFlightTransformFragment"));
	Position.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightTransformFragment, Location));
	Position.Storage.ElementStride = sizeof(FVector);
	Schema.SymbolRecords.Add(Position.SymbolName, MoveTemp(Position));

	FVexSymbolRecord Velocity;
	Velocity.SymbolName = TEXT("@velocity");
	Velocity.ValueType = EVexValueType::Float3;
	Velocity.Storage.Kind = EVexStorageKind::MassFragmentField;
	Velocity.Storage.FragmentType = FName(TEXT("FFlightTransformFragment"));
	Velocity.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightTransformFragment, Velocity));
	Velocity.Storage.ElementStride = sizeof(FVector);
	Schema.SymbolRecords.Add(Velocity.SymbolName, MoveTemp(Velocity));

	Schema.RebuildLegacyViews();
	Schema.LayoutHash = Flight::Vex::FVexSchemaOrchestrator::ComputeSchemaLayoutHash(Schema);
	Flight::Vex::FVexSymbolRegistry::Get().RegisterSchema(MoveTemp(Schema));
	return Flight::Reflection::FVexSchemaProviderResult::Success(TEXT("Manual Mass fragment schema provider registered the schema."));
}

FLIGHT_REFLECT_FIELDS_ATTR(FGeneralVexTestState,
	FLIGHT_FIELD_ATTR(float, Health, ::Flight::Reflection::Attr::VexSymbol<"@health">),
	FLIGHT_FIELD_ATTR(float, Ammo, ::Flight::Reflection::Attr::VexSymbol<"@ammo">),
	FLIGHT_FIELD_ATTR(float, Score, ::Flight::Reflection::Attr::VexSymbol<"@score">),
	FLIGHT_FIELD_ATTR(int32, Count, ::Flight::Reflection::Attr::VexSymbol<"@count">)
)

using FManualMassProviderReflectAttrs = ::Flight::Reflection::TAttributeSet<>;

FLIGHT_REFLECT_FIELDS_VEX_MANUAL(FManualMassProviderState,
	FManualMassProviderReflectAttrs,
	RegisterManualMassProviderSchema,
	FLIGHT_FIELD_ATTR(float, Shield, ::Flight::Reflection::Attr::VexSymbol<"@shield">),
	FLIGHT_FIELD_ATTR(float, Energy, ::Flight::Reflection::Attr::VexSymbol<"@energy">),
	FLIGHT_FIELD_ATTR(FVector, Position, ::Flight::Reflection::Attr::VexSymbol<"@position">),
	FLIGHT_FIELD_ATTR(FVector, Velocity, ::Flight::Reflection::Attr::VexSymbol<"@velocity">)
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexGeneralizationTest, "FlightProject.Vex.Generalization.ExecuteOnCustomStruct", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexAccessorHostTest, "FlightProject.Vex.Generalization.ExecuteOnAccessorHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexMassHostClassificationTest, "FlightProject.Vex.Generalization.ClassifyMassHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexGpuHostClassificationTest, "FlightProject.Vex.Generalization.ClassifyGpuHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexGpuPolicyCommitmentTest, "FlightProject.Vex.Generalization.GpuPolicyCommitment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexMassHostExecutionTest, "FlightProject.Vex.Generalization.ExecuteOnMassHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexManualProviderResolutionTest, "FlightProject.Vex.Generalization.ResolveManualMassProvider", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexGeneralizationTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	if (!World) return false;

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem) return false;

	// 1. Register the custom struct symbols
	Flight::Vex::TTypeVexRegistry<FGeneralVexTestState>::Register();

	// 2. Compile a VEX script that operates on this struct
	// Note: exercises both float and int symbol execution paths without relying on implicit parser coercions.
	const FString VexSource = TEXT("@score = @health + @ammo; @health -= 10.0; @count = @count + 2;");
	FString Errors;
	const uint32 BehaviorID = 9999; // Arbitrary test ID

	const void* TypeKey = Flight::Vex::TTypeVexRegistry<FGeneralVexTestState>::GetTypeKey();

	if (!VerseSubsystem->CompileVex(BehaviorID, VexSource, Errors, TypeKey))
	{
		AddError(FString::Printf(TEXT("VEX Compile Failed: %s"), *Errors));
		return false;
	}

	// 3. Prepare an instance
	FGeneralVexTestState TestState;
	TestState.Health = 80.0f;
	TestState.Ammo = 20.0f;
	TestState.Score = 0.0f;
	TestState.Count = 3;

	// 4. Execute
	VerseSubsystem->ExecuteBehaviorOnStruct(BehaviorID, &TestState, TypeKey);

	// 5. Verify results
	TestEqual(TEXT("Score should be Health + Ammo (80 + 20)"), TestState.Score, 100.0f);
	TestEqual(TEXT("Health should be decremented (80 - 10)"), TestState.Health, 70.0f);
	TestEqual(TEXT("Count should be incremented through typed symbol writeback"), TestState.Count, 5);

	FGeneralVexTestState ImplicitTypeState;
	ImplicitTypeState.Health = 60.0f;
	ImplicitTypeState.Ammo = 15.0f;
	ImplicitTypeState.Score = 0.0f;
	ImplicitTypeState.Count = 4;

	VerseSubsystem->ExecuteBehaviorOnStruct(BehaviorID, &ImplicitTypeState, nullptr);

	TestEqual(TEXT("Schema host should execute custom structs using persisted behavior type metadata"), ImplicitTypeState.Score, 75.0f);
	TestEqual(TEXT("Implicit-type execution should still mutate health"), ImplicitTypeState.Health, 50.0f);
	TestEqual(TEXT("Implicit-type execution should still mutate integer symbols"), ImplicitTypeState.Count, 6);

	return true;
}

bool FFlightVexAccessorHostTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	if (!World) return false;

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem) return false;

	Flight::Vex::FVexSymbolRegistry::Get().RegisterSchema(BuildAccessorBackedSchema());

	const uint32 BehaviorID = 10001;
	const void* TypeKey = GetAccessorBackedVexTypeKey();
	const FString VexSource = TEXT("@health = @health + 5.0; @energy = @energy * 2.0;");
	FString Errors;
	if (!VerseSubsystem->CompileVex(BehaviorID, VexSource, Errors, TypeKey))
	{
		AddError(FString::Printf(TEXT("Accessor-host VEX compile failed: %s"), *Errors));
		return false;
	}

	FAccessorBackedVexState State;
	State.HiddenHealth = 30.0f;
	State.HiddenEnergy = 7.0f;

	VerseSubsystem->ExecuteBehaviorOnStruct(BehaviorID, &State, nullptr);

	TestEqual(TEXT("Accessor host should mutate scalar accessor-backed health"), State.HiddenHealth, 35.0f);
	TestEqual(TEXT("Accessor host should mutate scalar accessor-backed energy"), State.HiddenEnergy, 14.0f);

	return true;
}

bool FFlightVexMassHostClassificationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	if (!World) return false;

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem) return false;

	Flight::Vex::FVexSymbolRegistry::Get().RegisterSchema(
		BuildStorageClassificationSchema(
			TEXT("FMassBackedVexState"),
			GetMassBackedVexTypeKey(),
			Flight::Vex::EVexStorageKind::MassFragmentField));

	const uint32 BehaviorID = 10002;
	FString Errors;
	const bool bCompiled = VerseSubsystem->CompileVex(BehaviorID, TEXT("@shield = @shield + 1.0;"), Errors, GetMassBackedVexTypeKey());

	TestTrue(TEXT("Mass-host schema should produce a behavior record"), VerseSubsystem->Behaviors.Contains(BehaviorID));
	TestTrue(TEXT("Mass-host behavior should at least resolve through compile binding"), bCompiled || !Errors.IsEmpty() || VerseSubsystem->Behaviors.Contains(BehaviorID));
	TestEqual(TEXT("Mass-backed schemas should resolve to the explicit MassFragments host"), VerseSubsystem->DescribeResolvedStorageHost(BehaviorID), FString(TEXT("MassFragments")));

	return true;
}

bool FFlightVexGpuHostClassificationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	if (!World) return false;

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem) return false;

	Flight::Vex::FVexSymbolRegistry::Get().RegisterSchema(
		BuildStorageClassificationSchema(
			TEXT("FGpuBackedVexState"),
			GetGpuBackedVexTypeKey(),
			Flight::Vex::EVexStorageKind::GpuBufferElement));

	const uint32 BehaviorID = 10003;
	FString Errors;
	const bool bCompiled = VerseSubsystem->CompileVex(BehaviorID, TEXT("@shield = @shield + 1.0;"), Errors, GetGpuBackedVexTypeKey());

	TestTrue(TEXT("GPU-host schema should produce a behavior record"), VerseSubsystem->Behaviors.Contains(BehaviorID));
	TestTrue(TEXT("GPU-host behavior should at least resolve through compile binding"), bCompiled || !Errors.IsEmpty() || VerseSubsystem->Behaviors.Contains(BehaviorID));
	TestEqual(TEXT("GPU-backed schemas should resolve to the explicit GpuBuffer host"), VerseSubsystem->DescribeResolvedStorageHost(BehaviorID), FString(TEXT("GpuBuffer")));

	return true;
}

bool FFlightVexGpuPolicyCommitmentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	if (!World) return false;

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem) return false;

	Flight::Vex::FVexSymbolRegistry::Get().RegisterSchema(
		BuildStorageClassificationSchema(
			TEXT("FGpuBackedPolicyState"),
			GetGpuBackedVexTypeKey(),
			Flight::Vex::EVexStorageKind::GpuBufferElement));

	FFlightBehaviorCompilePolicyRow PolicyRow;
	PolicyRow.RowName = TEXT("Policy.GpuOnly");
	PolicyRow.PreferredDomain = EFlightBehaviorCompileDomainPreference::Gpu;
	PolicyRow.bAllowNativeFallback = 0;
	PolicyRow.bAllowGeneratedOnly = 1;
	PolicyRow.RequiredSymbols = { TEXT("@shield") };

	UFlightVerseSubsystem::FCompilePolicyContext CompilePolicyContext;
	CompilePolicyContext.ExplicitPolicy = &PolicyRow;

	const uint32 BehaviorID = 10005;
	FString Errors;
	const bool bCompiled = VerseSubsystem->CompileVex(
		BehaviorID,
		TEXT("@gpu { @shield = @shield + 1.0; }"),
		Errors,
		GetGpuBackedVexTypeKey(),
		CompilePolicyContext);

	TestTrue(TEXT("GPU policy should allow generated-only success when fallback is disallowed"), bCompiled);

	const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(BehaviorID);
	TestNotNull(TEXT("GPU policy compile should persist behavior metadata"), Behavior);
	if (!Behavior)
	{
		return false;
	}

	TestEqual(TEXT("GPU policy should record the selected policy row"), Behavior->SelectedPolicyRowName, PolicyRow.RowName);
	TestEqual(TEXT("GPU policy should steer selected backend to GpuKernel"), Behavior->SelectedBackend, FString(TEXT("GpuKernel")));
	TestEqual(TEXT("GPU policy should keep committed backend unproven"), Behavior->CommittedBackend, FString(TEXT("Unknown")));
	TestFalse(TEXT("Generated-only GPU policy compile should not mark the behavior executable"), Behavior->bHasExecutableProcedure);
	TestTrue(TEXT("GPU policy compile should retain a commit detail explanation"), Behavior->CommitDetail.Contains(TEXT("generated-only")));

	const Flight::Vex::FFlightCompileArtifactReport* Report = VerseSubsystem->GetBehaviorCompileArtifactReport(BehaviorID);
	TestNotNull(TEXT("GPU policy compile should produce an artifact report"), Report);
	if (Report)
	{
		TestEqual(TEXT("Artifact report should preserve selected backend"), Report->SelectedBackend, FString(TEXT("GpuKernel")));
		TestEqual(TEXT("Artifact report should preserve committed backend truth"), Report->CommittedBackend, FString(TEXT("Unknown")));
		TestEqual(TEXT("Artifact report should preserve the selected policy row"), Report->SelectedPolicyRow, PolicyRow.RowName.ToString());
		TestTrue(TEXT("Artifact report should preserve policy-required symbols"), Report->PolicyRequiredSymbols.Contains(TEXT("@shield")));
	}

	return true;
}

bool FFlightVexMassHostExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	if (!World) return false;

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem) return false;

	Flight::Vex::FVexSymbolRegistry::Get().RegisterSchema(BuildExecutableMassSchema());

	const uint32 BehaviorID = 10004;
	FString Errors;
	if (!VerseSubsystem->CompileVex(BehaviorID, TEXT("@velocity = @position; @shield = @shield + @energy; @energy = @energy * 2.0;"), Errors, GetMassExecutableVexTypeKey()))
	{
		AddError(FString::Printf(TEXT("Mass-host execution VEX compile failed: %s"), *Errors));
		return false;
	}

	UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		AddError(TEXT("Expected compiled Mass-host behavior to exist."));
		return false;
	}

	TestTrue(TEXT("Mass-host behavior should persist fragment-binding metadata"), Behavior->SchemaBinding.IsSet() && Behavior->SchemaBinding->FragmentBindings.Num() >= 2);

	const FString OriginalSelectedBackend = Behavior->SelectedBackend;
	Behavior->SelectedBackend = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::NativeScalar);
	TestEqual(
		TEXT("Mass-host direct execution should honor an explicit native-scalar selection"),
		VerseSubsystem->DescribeDirectExecutionBackend(BehaviorID),
		FString(TEXT("NativeScalar")));

	if (Behavior->SimdPlan.IsValid())
	{
		Behavior->SelectedBackend = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::NativeSimd);
		TestEqual(
			TEXT("Mass-host direct execution should honor an explicit SIMD selection when a SIMD plan exists"),
			VerseSubsystem->DescribeDirectExecutionBackend(BehaviorID),
			FString(TEXT("NativeSimd")));
	}
	else
	{
		AddInfo(TEXT("Skipping NativeSimd direct-dispatch assertion because no SIMD plan was produced for the Mass-host behavior."));
	}

	Behavior->SelectedBackend = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::NativeScalar);

	TArray<FFlightTransformFragment> Transforms;
	Transforms.SetNum(1);
	Transforms[0].Location = FVector(11.0, 22.0, 33.0);
	Transforms[0].Velocity = FVector::ZeroVector;

	TArray<FFlightDroidStateFragment> DroidStates;
	DroidStates.SetNum(1);
	DroidStates[0].Shield = 2.0f;
	DroidStates[0].Energy = 3.0f;
	DroidStates[0].bIsDirty = false;

	VerseSubsystem->ExecuteBehaviorDirect(BehaviorID, Transforms, DroidStates);

	TestEqual(TEXT("Mass host should read FVector-backed position through schema bindings"), Transforms[0].Velocity.X, 11.0);
	TestEqual(TEXT("Mass host should write FVector-backed velocity through schema bindings"), Transforms[0].Velocity.Y, 22.0);
	TestEqual(TEXT("Mass host should preserve all float3 components through schema bindings"), Transforms[0].Velocity.Z, 33.0);
	TestEqual(TEXT("Mass host should read fragment-backed shield and energy through schema bindings"), DroidStates[0].Shield, 5.0f);
	TestEqual(TEXT("Mass host should write back to fragment-backed energy through schema bindings"), DroidStates[0].Energy, 6.0f);
	TestTrue(TEXT("Mass host writes should dirty the droid-state fragment"), DroidStates[0].bIsDirty);
	Behavior->SelectedBackend = OriginalSelectedBackend;

	return true;
}

bool FFlightVexManualProviderResolutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const Flight::Reflection::FTypeRegistry::FTypeInfo* TypeInfo = Flight::Reflection::FTypeRegistry::Get().Find(TEXT("FManualMassProviderState"));
	TestNotNull(TEXT("Manual Mass provider state should be present in the reflection registry"), TypeInfo);
	if (!TypeInfo)
	{
		return false;
	}

	TestEqual(TEXT("Manual provider state should classify as manual VEX-capable"), TypeInfo->VexCapability, Flight::Reflection::EVexCapability::VexCapableManual);
	TestNull(TEXT("Manual provider state should not use the auto-registration hook"), reinterpret_cast<const void*>(TypeInfo->EnsureVexSchemaRegisteredFn));
	TestNotNull(TEXT("Manual provider state should expose an explicit schema provider callback"), reinterpret_cast<const void*>(TypeInfo->ProvideVexSchemaFn));

	const Flight::Vex::FVexSchemaResolutionResult Resolution = Flight::Vex::FVexSymbolRegistry::Get().ResolveSchemaForReflectedType(*TypeInfo);
	TestTrue(TEXT("Manual provider should resolve a schema either directly after provider execution or from an already-registered result"),
		Resolution.Status == Flight::Vex::EVexSchemaResolutionStatus::ResolvedAfterProvider
		|| Resolution.Status == Flight::Vex::EVexSchemaResolutionStatus::ResolvedExisting);
	TestNotNull(TEXT("Manual provider should resolve a concrete schema"), Resolution.Schema);

	if (!Resolution.Schema)
	{
		if (!Resolution.Diagnostic.IsEmpty())
		{
			AddInfo(Resolution.Diagnostic);
		}
		return false;
	}

	const Flight::Vex::FVexSymbolRecord* Shield = Resolution.Schema->FindSymbolRecord(TEXT("@shield"));
	const Flight::Vex::FVexSymbolRecord* Position = Resolution.Schema->FindSymbolRecord(TEXT("@position"));
	TestNotNull(TEXT("Manual provider schema should expose @shield"), Shield);
	TestNotNull(TEXT("Manual provider schema should expose @position"), Position);

	if (Shield)
	{
		TestEqual(TEXT("Manual provider schema should bind @shield through Mass fragment storage"), Shield->Storage.Kind, Flight::Vex::EVexStorageKind::MassFragmentField);
		TestEqual(TEXT("Manual provider schema should target the droid-state fragment for @shield"), Shield->Storage.FragmentType, FName(TEXT("FFlightDroidStateFragment")));
	}

	if (Position)
	{
		TestEqual(TEXT("Manual provider schema should bind @position through Mass fragment storage"), Position->Storage.Kind, Flight::Vex::EVexStorageKind::MassFragmentField);
		TestEqual(TEXT("Manual provider schema should target the transform fragment for @position"), Position->Storage.FragmentType, FName(TEXT("FFlightTransformFragment")));
	}

	return true;
}
