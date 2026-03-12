// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/FlightReflection.h"
#include "Core/FlightReflectionDebug.h"
#include "Orchestration/FlightBehaviorBinding.h"

#if WITH_AUTOMATION_TESTS

namespace
{

Flight::Orchestration::FFlightBehaviorBinding::FIdentity MakeBindingIdentity()
{
	Flight::Orchestration::FFlightBehaviorBinding::FIdentity Identity;
	Identity.CohortName = TEXT("Swarm.Default");
	Identity.BehaviorID = 7102;
	return Identity;
}

Flight::Orchestration::FFlightBehaviorBinding::FReport MakeBindingReport()
{
	Flight::Orchestration::FFlightBehaviorBinding::FReport Report;
	Report.Identity = MakeBindingIdentity();
	Report.ExecutionDomain = Flight::Orchestration::EFlightExecutionDomain::TaskGraph;
	Report.FrameInterval = 2;
	Report.bAsync = true;
	Report.RequiredContracts.Add(TEXT("Formation"));
	Report.Selection.Source = Flight::Orchestration::EFlightBehaviorBindingSelectionSource::ManualBinding;
	Report.Selection.Rule = Flight::Orchestration::EFlightBehaviorBindingSelectionRule::ExplicitRegistration;
	Report.Selection.ApplyDefaultCohortFallback(TEXT("Swarm.Anchor"), TEXT("Swarm.Default"));
	return Report;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightBehaviorBindingIdentityReflectionTest,
	"FlightProject.Unit.Orchestration.BindingIdentityReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightBehaviorBindingIdentityReflectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const auto Identity = MakeBindingIdentity();
	const Flight::Reflection::FTypeRegistry::FTypeInfo* TypeInfo =
		Flight::Reflection::FTypeRegistry::Get().Find(TEXT("Flight::Orchestration::FFlightBehaviorBinding::FIdentity"));

	TestNotNull(TEXT("Binding identity should register with trait reflection"), TypeInfo);
	if (!TypeInfo)
	{
		return false;
	}

	TestTrue(TEXT("Binding identity should expose CohortName"), TypeInfo->FieldNames.Contains(TEXT("CohortName")));
	TestTrue(TEXT("Binding identity should expose BehaviorID"), TypeInfo->FieldNames.Contains(TEXT("BehaviorID")));
	TestTrue(TEXT("Binding identity dump should include the cohort name"),
		Flight::Reflection::Debug::DumpReflectableFieldsToString(Identity).Contains(TEXT("CohortName=Swarm.Default")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightBehaviorBindingSelectionReflectionTest,
	"FlightProject.Unit.Orchestration.BindingSelectionReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightBehaviorBindingSelectionReflectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const auto Selection = MakeBindingReport().Selection;
	const Flight::Reflection::FTypeRegistry::FTypeInfo* TypeInfo =
		Flight::Reflection::FTypeRegistry::Get().Find(TEXT("Flight::Orchestration::FFlightBehaviorBinding::FReport::FSelectionProvenance"));

	TestNotNull(TEXT("Binding selection provenance should register with trait reflection"), TypeInfo);
	if (!TypeInfo)
	{
		return false;
	}

	TestTrue(TEXT("Binding selection should expose Source"), TypeInfo->FieldNames.Contains(TEXT("Source")));
	TestTrue(TEXT("Binding selection should expose Rule"), TypeInfo->FieldNames.Contains(TEXT("Rule")));
	TestTrue(TEXT("Binding selection should expose RequestedCohortName"), TypeInfo->FieldNames.Contains(TEXT("RequestedCohortName")));
	TestTrue(TEXT("Binding selection should expose fallback usage"), TypeInfo->FieldNames.Contains(TEXT("bUsedDefaultCohortFallback")));

	const FString Dump = Flight::Reflection::Debug::DumpReflectableFieldsToString(Selection);
	TestTrue(TEXT("Binding selection dump should include the manual binding source"), Dump.Contains(TEXT("Source=1")));
	TestTrue(TEXT("Binding selection dump should include the explicit registration rule"), Dump.Contains(TEXT("Rule=1")));
	TestTrue(TEXT("Binding selection dump should include the requested cohort"), Dump.Contains(TEXT("RequestedCohortName=Swarm.Anchor")));
	TestTrue(TEXT("Binding selection dump should include the fallback cohort"), Dump.Contains(TEXT("FallbackCohortName=Swarm.Default")));
	TestTrue(TEXT("Binding selection dump should include the fallback flag"), Dump.Contains(TEXT("bUsedDefaultCohortFallback=true")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightBehaviorBindingReportReflectionTest,
	"FlightProject.Unit.Orchestration.BindingReportReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightBehaviorBindingReportReflectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const auto Report = MakeBindingReport();
	const Flight::Reflection::FTypeRegistry::FTypeInfo* TypeInfo =
		Flight::Reflection::FTypeRegistry::Get().Find(TEXT("Flight::Orchestration::FFlightBehaviorBinding::FReport"));

	TestNotNull(TEXT("Binding report should register with trait reflection"), TypeInfo);
	if (!TypeInfo)
	{
		return false;
	}

	TestTrue(TEXT("Binding report should expose Selection"), TypeInfo->FieldNames.Contains(TEXT("Selection")));

	const FString Dump = Flight::Reflection::Debug::DumpReflectableFieldsToString(Report);
	TestTrue(TEXT("Binding report dump should include the execution domain"), Dump.Contains(TEXT("ExecutionDomain=2")));
	TestTrue(TEXT("Binding report dump should include the structured selection provenance"), Dump.Contains(TEXT("Selection={Source=1")));
	TestTrue(TEXT("Binding report dump should include the fallback flag"), Dump.Contains(TEXT("bUsedDefaultCohortFallback=true")));

	return true;
}

#endif
