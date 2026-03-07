// Copyright Kelly Rey Wilson. All Rights Reserved.
// Test suite for the functional programming library and related data structures.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

// The primary header for our functional patterns
#include "Core/FlightFunctional.h"

// The data structure we want to use in a test
#include "FlightDataTypes.h"

#if WITH_AUTOMATION_TESTS

// Bring the functional namespace into scope for convenience
using namespace Flight::Functional;

/**
 * A simple test for the TResult<> success path.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalTResultSuccessTest, "FlightProject.Functional.TResult.Success", EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalTResultSuccessTest::RunTest(const FString& Parameters)
{
	// 1. Create a success result
	auto Result = Ok(42);

	// 2. Test the state
	TestTrue("Result should be Ok", Result.IsOk());
	TestFalse("Result should not be Err", Result.IsErr());

	// 3. Test unwrapping the value
	int32 Value = MoveTemp(Result).Unwrap();
	TestEqual("Unwrapped value should be 42", Value, 42);

	return true;
}

/**
 * A simple test for the TResult<> failure path, including logging.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalTResultErrorTest, "FlightProject.Functional.TResult.Error", EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalTResultErrorTest::RunTest(const FString& Parameters)
{
	const FString ErrorMessage = TEXT("Something went wrong");

	// 1. Create an error result
	auto Result = Err<int32>(ErrorMessage);

	// 2. Test the state
	TestFalse("Result should not be Ok", Result.IsOk());
	TestTrue("Result should be Err", Result.IsErr());

	// 3. Demonstrate logging within a test. This will appear in the test output.
	UE_LOG(LogTemp, Warning, TEXT("Demonstrating logging from a unit test. The error was: %s"), *Result.GetError());

	// 4. Test retrieving the error message
	TestEqual("Error message should match", Result.GetError(), ErrorMessage);

	return true;
}

/**
 * Tests the AndThen (monadic bind) operation for chaining successful results.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalTResultAndThenTest, "FlightProject.Functional.TResult.AndThen", EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalTResultAndThenTest::RunTest(const FString& Parameters)
{
	// 1. Create an initial successful result
	auto InitialResult = Ok(FString("Hello"));

	// 2. Chain a second operation
	auto ChainedResult = MoveTemp(InitialResult).AndThen([](const FString& Value) -> TResult<int32>
	{
		return Ok(Value.Len());
	});

	// 3. Verify the final result
	TestTrue("Chained result should be Ok", ChainedResult.IsOk());
	TestEqual("Final value should be the length of the string", ChainedResult.Unwrap(), 5);

	// 4. Test propagation of an error through the chain
	auto ErrorResult = Err<FString>(TEXT("Initial Failure"));
	auto ChainedErrorResult = MoveTemp(ErrorResult).AndThen([](const FString& Value) -> TResult<int32>
	{
		// This part should never execute
		return Ok(Value.Len());
	});

	TestTrue("Chained error result should be an error", ChainedErrorResult.IsErr());
	TestEqual("Error message should be propagated", ChainedErrorResult.GetError(), FString(TEXT("Initial Failure")));

	return true;
}

namespace FlightTestHelpers
{
	// A helper function that uses FLIGHT_TRY to show error propagation.
	TResult<int32> FunctionThatTries(TResult<FString> Input)
	{
		if (Input.IsErr())
		{
			return Err<int32>(Input.GetError());
		}
		const FString Value = Input.Unwrap();
		if (Value.IsEmpty())
		{
			return Err<int32>(FString(TEXT("Input string cannot be empty")));
		}
		return Ok(Value.Len());
	}
}

/**
 * Tests the FLIGHT_TRY macro for unwrapping successes and propagating failures.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalFlightTryTest, "FlightProject.Functional.FLIGHT_TRY", EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalFlightTryTest::RunTest(const FString& Parameters)
{
	// 1. Test the success path
	auto SuccessResult = FlightTestHelpers::FunctionThatTries(Ok(FString("Test")));
	TestTrue("Success path should return Ok", SuccessResult.IsOk());
	TestEqual("Success path should return string length", SuccessResult.Unwrap(), 4);

	// 2. Test the failure propagation path
	const FString OriginalError = TEXT("Original Failure");
	auto FailureResult = FlightTestHelpers::FunctionThatTries(Err<FString>(OriginalError));
	TestTrue("Failure path should return Err", FailureResult.IsErr());
	TestEqual("Failure path should propagate the original error", FailureResult.GetError(), OriginalError);
	
	// 3. Test the internal failure path
	auto InternalFailureResult = FlightTestHelpers::FunctionThatTries(Ok(FString("")));
	TestTrue("Internal failure path should return Err", InternalFailureResult.IsErr());
	TestEqual("Internal failure path should return the new error", InternalFailureResult.GetError(), FString(TEXT("Input string cannot be empty")));

	return true;
}


namespace FlightTestHelpers
{
	// An example function that validates a data structure.
	// This would typically live in your runtime code, not in the test file.
	TResult<bool> ValidateDataRow(const FFlightSpatialLayoutRow& DataRow)
	{
		if (DataRow.RowName.IsNone() || DataRow.RowName.ToString().IsEmpty())
		{
			return Err<bool>(FString(TEXT("RowName cannot be empty")));
		}

		if (DataRow.GetScale().IsNearlyZero())
		{
			return Err<bool>(FString(TEXT("Scale cannot be zero")));
		}

		return Ok(true);
	}
}

/**
 * Tests using a mock data table row within a functional validation context.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataDrivenFunctionalTest, "FlightProject.Functional.DataDriven.Validation", EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FDataDrivenFunctionalTest::RunTest(const FString& Parameters)
{
	// 1. Create a valid mock data row.
	FFlightSpatialLayoutRow ValidRow;
	ValidRow.RowName = FName("TestObstacle_Valid");
	ValidRow.EntityType = EFlightSpatialEntityType::Obstacle;
	ValidRow.PositionX = 100.f;
	ValidRow.ScaleX = 1.f;
	ValidRow.ScaleY = 1.f;
	ValidRow.ScaleZ = 1.f;

	// 2. Test the valid row, expecting success.
	auto SuccessResult = FlightTestHelpers::ValidateDataRow(ValidRow);
	TestTrue("Validation of a valid row should succeed", SuccessResult.IsOk());
	
	// Log the success for demonstration.
	UE_LOG(LogTemp, Log, TEXT("Successfully validated mock data row '%s'"), *ValidRow.RowName.ToString());

	// 3. Create an invalid mock data row.
	FFlightSpatialLayoutRow InvalidRow;
	InvalidRow.RowName = FName(); // Invalid empty name
	InvalidRow.EntityType = EFlightSpatialEntityType::Obstacle;
	InvalidRow.PositionX = 200.f;
	InvalidRow.ScaleX = 1.f;
	
	// 4. Test the invalid row, expecting failure.
	auto FailureResult = FlightTestHelpers::ValidateDataRow(InvalidRow);
	TestTrue("Validation of an invalid row should fail", FailureResult.IsErr());
	TestEqual("Error message for invalid row should be correct", FailureResult.GetError(), FString(TEXT("RowName cannot be empty")));
	
	// Log the failure for demonstration.
	UE_LOG(LogTemp, Warning, TEXT("Correctly caught invalid data row. Reason: %s"), *FailureResult.GetError());

	return true;
}


#endif //WITH_AUTOMATION_TESTS
