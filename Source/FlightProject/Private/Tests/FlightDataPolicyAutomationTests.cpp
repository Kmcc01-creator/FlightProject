// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "FlightDataTypes.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightBehaviorCompilePolicySpecificityTest,
	"FlightProject.Functional.Data.Policy.SelectsMostSpecificMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightBehaviorCompilePolicySpecificityTest::RunTest(const FString& Parameters)
{
	TArray<FFlightBehaviorCompilePolicyRow> Policies;

	FFlightBehaviorCompilePolicyRow DefaultPolicy;
	DefaultPolicy.RowName = TEXT("DefaultPolicy");
	DefaultPolicy.Priority = 100;
	Policies.Add(DefaultPolicy);

	FFlightBehaviorCompilePolicyRow ExactBehaviorPolicy;
	ExactBehaviorPolicy.RowName = TEXT("ExactBehaviorPolicy");
	ExactBehaviorPolicy.BehaviorId = 42;
	ExactBehaviorPolicy.Priority = 1;
	Policies.Add(ExactBehaviorPolicy);

	FFlightBehaviorCompilePolicyRow ExactBehaviorAndCohortPolicy;
	ExactBehaviorAndCohortPolicy.RowName = TEXT("ExactBehaviorAndCohortPolicy");
	ExactBehaviorAndCohortPolicy.BehaviorId = 42;
	ExactBehaviorAndCohortPolicy.CohortName = TEXT("SwarmAnchor.Alpha");
	ExactBehaviorAndCohortPolicy.Priority = 0;
	Policies.Add(ExactBehaviorAndCohortPolicy);

	const FFlightBehaviorCompilePolicyRow* Selected = SelectBestBehaviorCompilePolicy(
		Policies,
		42,
		TEXT("SwarmAnchor.Alpha"),
		NAME_None);

	TestNotNull(TEXT("A matching policy should be selected"), Selected);
	if (!Selected)
	{
		return false;
	}

	TestEqual(TEXT("Most specific behavior+cohort policy should beat wildcard and lower-specificity matches"),
		Selected->RowName, ExactBehaviorAndCohortPolicy.RowName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightBehaviorCompilePolicyPriorityTest,
	"FlightProject.Functional.Data.Policy.PriorityBreaksTies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightBehaviorCompilePolicyPriorityTest::RunTest(const FString& Parameters)
{
	TArray<FFlightBehaviorCompilePolicyRow> Policies;

	FFlightBehaviorCompilePolicyRow LowerPriority;
	LowerPriority.RowName = TEXT("LowerPriority");
	LowerPriority.BehaviorId = 77;
	LowerPriority.CohortName = TEXT("Swarm.Default");
	LowerPriority.Priority = 1;
	Policies.Add(LowerPriority);

	FFlightBehaviorCompilePolicyRow HigherPriority;
	HigherPriority.RowName = TEXT("HigherPriority");
	HigherPriority.BehaviorId = 77;
	HigherPriority.CohortName = TEXT("Swarm.Default");
	HigherPriority.Priority = 5;
	Policies.Add(HigherPriority);

	const FFlightBehaviorCompilePolicyRow* Selected = SelectBestBehaviorCompilePolicy(
		Policies,
		77,
		TEXT("Swarm.Default"),
		NAME_None);

	TestNotNull(TEXT("A matching policy should be selected"), Selected);
	if (!Selected)
	{
		return false;
	}

	TestEqual(TEXT("Higher priority should win when specificity ties"),
		Selected->RowName, HigherPriority.RowName);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
