// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/FlightReflection.h"
#include "Core/FlightReflectionDebug.h"
#include "Navigation/FlightNavigationCommitIdentity.h"
#include "Navigation/FlightNavigationCommitReport.h"

#if WITH_AUTOMATION_TESTS

namespace
{

FFlightNavigationCommitIdentity MakeNavigationCommitIdentity()
{
	FFlightNavigationCommitIdentity Identity;
	Identity.SourceCandidateId = FGuid::NewGuid();
	Identity.CohortName = TEXT("Automation.Commit.Cohort");
	Identity.RuntimePathId = FGuid::NewGuid();
	Identity.SetCommitKind(Flight::Navigation::EFlightNavigationCommitProductKind::SyntheticNodeOrbit);
	Identity.SetSourceKind(static_cast<Flight::Orchestration::EFlightParticipantKind>(4));
	return Identity;
}

FFlightNavigationCommitReport MakeNavigationCommitReport()
{
	FFlightNavigationCommitReport Report;
	Report.Identity = MakeNavigationCommitIdentity();
	Report.SourceCandidateName = TEXT("Automation.Commit.Candidate");
	Report.InitialLocation = FVector(120.0f, -45.0f, 300.0f);
	Report.PathLength = 640.0f;
	Report.bSynthetic = true;
	Report.bResolvedFromExecutionPlan = true;
	return Report;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightNavigationCommitIdentityTraitReflectionTest,
	"FlightProject.Unit.Navigation.CommitIdentityTraitReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightNavigationCommitIdentityTraitReflectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FFlightNavigationCommitIdentity Identity = MakeNavigationCommitIdentity();
	const Flight::Reflection::FTypeRegistry::FTypeInfo* TypeInfo =
		Flight::Reflection::FTypeRegistry::Get().FindByNativeStruct(FFlightNavigationCommitIdentity::StaticStruct());

	TestNotNull(TEXT("Commit identity should register with the trait reflection type registry"), TypeInfo);
	if (!TypeInfo)
	{
		return false;
	}

	TestEqual(TEXT("Commit identity should publish the expected reflected type name"),
		TypeInfo->TypeName,
		FName(TEXT("FFlightNavigationCommitIdentity")));
	TestTrue(TEXT("Commit identity should list SourceCandidateId in reflected field names"),
		TypeInfo->FieldNames.Contains(TEXT("SourceCandidateId")));
	TestTrue(TEXT("Commit identity should list RuntimePathId in reflected field names"),
		TypeInfo->FieldNames.Contains(TEXT("RuntimePathId")));

	const FString Dump = Flight::Reflection::Debug::DumpReflectableFieldsToString(Identity);
	TestTrue(TEXT("Trait-reflected dump should include the cohort name"),
		Dump.Contains(TEXT("CohortName=Automation.Commit.Cohort")));
	TestTrue(TEXT("Trait-reflected dump should include the typed commit kind payload"),
		Dump.Contains(TEXT("CommitKind=3")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightNavigationCommitIdentityNativeDumpTest,
	"FlightProject.Unit.Navigation.CommitIdentityNativeDump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightNavigationCommitIdentityNativeDumpTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FFlightNavigationCommitIdentity Identity = MakeNavigationCommitIdentity();
	const FString Dump = Flight::Reflection::Debug::DumpNativeStructFieldsToString(
		*FFlightNavigationCommitIdentity::StaticStruct(),
		&Identity);

	TestTrue(TEXT("Native reflected dump should include CohortName"),
		Dump.Contains(TEXT("CohortName=Automation.Commit.Cohort")));
	TestTrue(TEXT("Native reflected dump should include the runtime path id property"),
		Dump.Contains(TEXT("RuntimePathId=")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightNavigationCommitReportTraitReflectionTest,
	"FlightProject.Unit.Navigation.CommitReportTraitReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightNavigationCommitReportTraitReflectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FFlightNavigationCommitReport Report = MakeNavigationCommitReport();
	const Flight::Reflection::FTypeRegistry::FTypeInfo* TypeInfo =
		Flight::Reflection::FTypeRegistry::Get().FindByNativeStruct(FFlightNavigationCommitReport::StaticStruct());

	TestNotNull(TEXT("Commit report should register with the trait reflection type registry"), TypeInfo);
	if (!TypeInfo)
	{
		return false;
	}

	TestEqual(TEXT("Commit report should publish the expected reflected type name"),
		TypeInfo->TypeName,
		FName(TEXT("FFlightNavigationCommitReport")));
	TestTrue(TEXT("Commit report should list SourceCandidateName in reflected field names"),
		TypeInfo->FieldNames.Contains(TEXT("SourceCandidateName")));
	TestTrue(TEXT("Commit report should list bSynthetic in reflected field names"),
		TypeInfo->FieldNames.Contains(TEXT("bSynthetic")));

	const FString Dump = Flight::Reflection::Debug::DumpReflectableFieldsToString(Report);
	TestTrue(TEXT("Trait-reflected report dump should include the source candidate name"),
		Dump.Contains(TEXT("SourceCandidateName=Automation.Commit.Candidate")));
	TestTrue(TEXT("Trait-reflected report dump should include the synthetic flag"),
		Dump.Contains(TEXT("bSynthetic=true")));
	TestTrue(TEXT("Trait-reflected report dump should include the execution-plan resolution flag"),
		Dump.Contains(TEXT("bResolvedFromExecutionPlan=true")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightNavigationCommitReportNativeDumpTest,
	"FlightProject.Unit.Navigation.CommitReportNativeDump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightNavigationCommitReportNativeDumpTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FFlightNavigationCommitReport Report = MakeNavigationCommitReport();
	const FString Dump = Flight::Reflection::Debug::DumpNativeStructFieldsToString(
		*FFlightNavigationCommitReport::StaticStruct(),
		&Report);

	TestTrue(TEXT("Native reflected report dump should include SourceCandidateName"),
		Dump.Contains(TEXT("SourceCandidateName=Automation.Commit.Candidate")));
	TestTrue(TEXT("Native reflected report dump should include PathLength"),
		Dump.Contains(TEXT("PathLength=")));
	TestTrue(TEXT("Native reflected report dump should include the execution-plan resolution flag"),
		Dump.Contains(TEXT("bResolvedFromExecutionPlan=True")) || Dump.Contains(TEXT("bResolvedFromExecutionPlan=true")));

	return true;
}

#endif
