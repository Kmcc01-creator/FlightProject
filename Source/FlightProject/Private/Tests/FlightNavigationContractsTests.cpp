// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Mass/FlightMassFragments.h"
#include "Navigation/FlightNavigationCommitProduct.h"
#include "Navigation/FlightNavigationContracts.h"
#include "Vex/FlightVexSymbolRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightNavigationContractsTest,
	"FlightProject.Navigation.VerticalSlice.Contracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightNavigationContractsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const Flight::Reflection::FTypeRegistry::FTypeInfo* IntentInfo =
		Flight::Reflection::FTypeRegistry::Get().Find(TEXT("Flight::Navigation::FFlightNavigationIntentContract"));
	const Flight::Reflection::FTypeRegistry::FTypeInfo* CandidateInfo =
		Flight::Reflection::FTypeRegistry::Get().Find(TEXT("Flight::Navigation::FFlightNavigationCandidateContract"));
	const Flight::Reflection::FTypeRegistry::FTypeInfo* FieldSampleInfo =
		Flight::Reflection::FTypeRegistry::Get().Find(TEXT("Flight::Navigation::FFlightNavigationFieldSampleContract"));
	const Flight::Reflection::FTypeRegistry::FTypeInfo* CommitInfo =
		Flight::Reflection::FTypeRegistry::Get().Find(TEXT("Flight::Navigation::FFlightNavigationCommitContract"));

	TestNotNull(TEXT("Navigation intent contract should be registered for reflection discovery"), IntentInfo);
	TestNotNull(TEXT("Navigation candidate contract should be registered for reflection discovery"), CandidateInfo);
	TestNotNull(TEXT("Navigation field sample contract should be registered for reflection discovery"), FieldSampleInfo);
	TestNotNull(TEXT("Navigation commit contract should be registered for reflection discovery"), CommitInfo);

	if (!IntentInfo || !CandidateInfo || !FieldSampleInfo || !CommitInfo)
	{
		return false;
	}

	TestEqual(TEXT("Navigation intent should classify as auto VEX-capable"),
		IntentInfo->VexCapability,
		Flight::Reflection::EVexCapability::VexCapableAuto);
	TestEqual(TEXT("Navigation candidate should classify as non-VEX"),
		CandidateInfo->VexCapability,
		Flight::Reflection::EVexCapability::NotVexCapable);
	TestEqual(TEXT("Navigation field sample should classify as auto VEX-capable"),
		FieldSampleInfo->VexCapability,
		Flight::Reflection::EVexCapability::VexCapableAuto);
	TestEqual(TEXT("Navigation commit should classify as manual VEX-capable"),
		CommitInfo->VexCapability,
		Flight::Reflection::EVexCapability::VexCapableManual);

	TestNotNull(TEXT("Navigation intent should expose an auto schema provider callback"),
		reinterpret_cast<const void*>(IntentInfo->ProvideVexSchemaFn));
	TestNull(TEXT("Navigation candidate should not expose a schema provider callback"),
		reinterpret_cast<const void*>(CandidateInfo->ProvideVexSchemaFn));
	TestNotNull(TEXT("Navigation field sample should expose an auto schema provider callback"),
		reinterpret_cast<const void*>(FieldSampleInfo->ProvideVexSchemaFn));
	TestNull(TEXT("Navigation commit should not use the auto-registration hook"),
		reinterpret_cast<const void*>(CommitInfo->EnsureVexSchemaRegisteredFn));
	TestNotNull(TEXT("Navigation commit should expose a manual schema provider callback"),
		reinterpret_cast<const void*>(CommitInfo->ProvideVexSchemaFn));

	const Flight::Vex::FVexSchemaResolutionResult IntentResolution =
		Flight::Vex::FVexSymbolRegistry::Get().ResolveSchemaForReflectedType(*IntentInfo);
	const Flight::Vex::FVexSchemaResolutionResult FieldResolution =
		Flight::Vex::FVexSymbolRegistry::Get().ResolveSchemaForReflectedType(*FieldSampleInfo);
	const Flight::Vex::FVexSchemaResolutionResult CommitResolution =
		Flight::Vex::FVexSymbolRegistry::Get().ResolveSchemaForReflectedType(*CommitInfo);

	TestNotNull(TEXT("Navigation intent should resolve a reflected schema"), IntentResolution.Schema);
	TestNotNull(TEXT("Navigation field sample should resolve a reflected schema"), FieldResolution.Schema);
	TestNotNull(TEXT("Navigation commit should resolve a manual-provider schema"), CommitResolution.Schema);

	if (IntentResolution.Schema)
	{
		TestTrue(TEXT("Navigation intent schema should expose @nav_route_bias"),
			IntentResolution.Schema->HasSymbol(TEXT("@nav_route_bias")));
	}

	if (FieldResolution.Schema)
	{
		TestTrue(TEXT("Navigation field sample schema should expose @nav_flow_direction"),
			FieldResolution.Schema->HasSymbol(TEXT("@nav_flow_direction")));
	}

	if (!CommitResolution.Schema)
	{
		if (!CommitResolution.Diagnostic.IsEmpty())
		{
			AddInfo(CommitResolution.Diagnostic);
		}
		return false;
	}

	TestTrue(TEXT("Navigation commit provider should resolve either after provider execution or from an existing schema"),
		CommitResolution.Status == Flight::Vex::EVexSchemaResolutionStatus::ResolvedAfterProvider
		|| CommitResolution.Status == Flight::Vex::EVexSchemaResolutionStatus::ResolvedExisting);

	const Flight::Vex::FVexSymbolRecord* PathProgress =
		CommitResolution.Schema->FindSymbolRecord(TEXT("@nav_path_progress"));
	const Flight::Vex::FVexSymbolRecord* DesiredSpeed =
		CommitResolution.Schema->FindSymbolRecord(TEXT("@nav_desired_speed"));
	const Flight::Vex::FVexSymbolRecord* Looping =
		CommitResolution.Schema->FindSymbolRecord(TEXT("@nav_looping"));

	TestNotNull(TEXT("Navigation commit schema should expose @nav_path_progress"), PathProgress);
	TestNotNull(TEXT("Navigation commit schema should expose @nav_desired_speed"), DesiredSpeed);
	TestNotNull(TEXT("Navigation commit schema should expose @nav_looping"), Looping);

	if (PathProgress)
	{
		TestEqual(TEXT("Navigation commit path progress should bind through Mass fragment storage"),
			PathProgress->Storage.Kind,
			Flight::Vex::EVexStorageKind::MassFragmentField);
		TestEqual(TEXT("Navigation commit path progress should target the path-follow fragment"),
			PathProgress->Storage.FragmentType,
			FName(TEXT("FFlightPathFollowFragment")));
		TestEqual(TEXT("Navigation commit path progress offset should match the Mass fragment"),
			PathProgress->Storage.MemberOffset,
			static_cast<int32>(STRUCT_OFFSET(FFlightPathFollowFragment, CurrentDistance)));
	}

	if (DesiredSpeed)
	{
		TestEqual(TEXT("Navigation commit desired speed should target the path-follow fragment"),
			DesiredSpeed->Storage.FragmentType,
			FName(TEXT("FFlightPathFollowFragment")));
	}

	if (Looping)
	{
		TestEqual(TEXT("Navigation commit looping flag should resolve as bool"),
			Looping->ValueType,
			Flight::Vex::EVexValueType::Bool);
	}

	Flight::Navigation::FFlightNavigationCommitProduct EmptyProduct;
	TestEqual(TEXT("Default navigation commit product should classify as None"),
		EmptyProduct.Kind,
		Flight::Navigation::EFlightNavigationCommitProductKind::None);
	TestFalse(TEXT("Default navigation commit product should be invalid"),
		EmptyProduct.IsValid());

	return true;
}
