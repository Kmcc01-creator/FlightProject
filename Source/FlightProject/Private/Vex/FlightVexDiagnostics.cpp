// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexParser.h"

namespace Flight::Vex
{

FString FormatIssues(const TArray<FVexIssue>& Issues)
{
	FString Formatted;
	for (const FVexIssue& Issue : Issues)
	{
		Formatted += FString::Printf(
			TEXT("[%s L%d:C%d] %s\n"),
			Issue.Severity == EVexIssueSeverity::Error ? TEXT("error") : TEXT("warning"),
			Issue.Line,
			Issue.Column,
			*Issue.Message);
	}
	return Formatted;
}

} // namespace Flight::Vex
