// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Flight::Vex
{

enum class EVexTokenKind : uint8
{
	Identifier,
	Symbol,
	TargetDirective, // @cpu, @gpu, @job, @thread, @async, @rate
	Number,
	KeywordIf,
	KeywordElse,
	KeywordWhile,
	KeywordFor,
	Operator,
	LParen,
	RParen,
	LBrace,
	RBrace,
	Comma,
	Dot,
	Semicolon,
	EndOfFile
};

enum class EVexIssueSeverity : uint8
{
	Warning,
	Error
};

struct FVexToken
{
	EVexTokenKind Kind = EVexTokenKind::Identifier;
	FString Lexeme;
	int32 Line = 1;
	int32 Column = 1;
};

struct FVexIssue
{
	EVexIssueSeverity Severity = EVexIssueSeverity::Error;
	FString Message;
	int32 Line = 1;
	int32 Column = 1;
};

} // namespace Flight::Vex
