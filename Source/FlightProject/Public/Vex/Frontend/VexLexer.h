// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/Frontend/VexToken.h"

namespace Flight::Vex
{

/**
 * FVexLexer
 *
 * Responsibility: Break source text into a stream of tokens.
 */
class FLIGHTPROJECT_API FVexLexer
{
public:
	static TArray<FVexToken> Tokenize(const FString& Source, TArray<FVexIssue>& OutIssues);

private:
	static bool IsIdentStart(TCHAR Ch);
	static bool IsIdentBody(TCHAR Ch);
	static bool IsDigit(TCHAR Ch);
};

} // namespace Flight::Vex
