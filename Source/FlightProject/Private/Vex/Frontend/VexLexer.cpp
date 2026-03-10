// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/Frontend/VexLexer.h"

namespace Flight::Vex
{

bool FVexLexer::IsIdentStart(TCHAR Ch) { return FChar::IsAlpha(Ch) || Ch == TEXT('_'); }
bool FVexLexer::IsIdentBody(TCHAR Ch) { return FChar::IsAlnum(Ch) || Ch == TEXT('_'); }
bool FVexLexer::IsDigit(TCHAR Ch) { return FChar::IsDigit(Ch) || Ch == TEXT('.'); }

TArray<FVexToken> FVexLexer::Tokenize(const FString& Source, TArray<FVexIssue>& OutIssues)
{
	TArray<FVexToken> Tokens;
	int32 Index = 0, Line = 1, Col = 1;
	const FString OpsChars = TEXT("+-*/%=<>!|&");

	auto AddIssue = [&](EVexIssueSeverity Severity, const FString& Message, int32 L, int32 C)
	{
		FVexIssue Issue; Issue.Severity = Severity; Issue.Message = Message; Issue.Line = L; Issue.Column = C; OutIssues.Add(Issue);
	};

	while (Index < Source.Len())
	{
		const TCHAR Ch = Source[Index];
		const int32 TLine = Line, TCol = Col;

		if (FChar::IsWhitespace(Ch))
		{
			if (Ch == TEXT('\n')) { ++Line; Col = 1; }
			else ++Col;
			++Index;
			continue;
		}

		if (Ch == TEXT('#'))
		{
			while (Index < Source.Len() && Source[Index] != TEXT('\n')) { ++Index; ++Col; }
			continue;
		}

		if (IsIdentStart(Ch))
		{
			int32 End = Index + 1;
			while (End < Source.Len() && IsIdentBody(Source[End])) ++End;
			const FString Lex = Source.Mid(Index, End - Index);
			FVexToken T; T.Lexeme = Lex; T.Line = TLine; T.Column = TCol;
			if (Lex == TEXT("if")) T.Kind = EVexTokenKind::KeywordIf;
			else if (Lex == TEXT("else")) T.Kind = EVexTokenKind::KeywordElse;
			else if (Lex == TEXT("while")) T.Kind = EVexTokenKind::KeywordWhile;
			else if (Lex == TEXT("for")) T.Kind = EVexTokenKind::KeywordFor;
			else T.Kind = EVexTokenKind::Identifier;
			Tokens.Add(MoveTemp(T)); Col += End - Index; Index = End;
			continue;
		}

		if (Ch == TEXT('@'))
		{
			int32 End = Index + 1;
			if (End < Source.Len() && IsIdentStart(Source[End]))
			{
				++End;
				while (End < Source.Len() && IsIdentBody(Source[End])) ++End;
				const FString Lex = Source.Mid(Index, End - Index);
				FVexToken T; T.Lexeme = Lex; T.Line = TLine; T.Column = TCol;
				if (Lex == TEXT("@cpu") || Lex == TEXT("@gpu") || Lex == TEXT("@rate") || 
					Lex == TEXT("@job") || Lex == TEXT("@thread") || Lex == TEXT("@async"))
				{
					T.Kind = EVexTokenKind::TargetDirective;
				}
				else T.Kind = EVexTokenKind::Symbol;
				Tokens.Add(MoveTemp(T)); Col += End - Index; Index = End;
				continue;
			}
			AddIssue(EVexIssueSeverity::Error, TEXT("Invalid symbol."), TLine, TCol);
			++Index; ++Col;
			continue;
		}

		if (FChar::IsDigit(Ch))
		{
			int32 End = Index + 1;
			while (End < Source.Len() && IsDigit(Source[End])) ++End;
			FVexToken T; T.Kind = EVexTokenKind::Number; T.Lexeme = Source.Mid(Index, End - Index); T.Line = TLine; T.Column = TCol;
			Tokens.Add(MoveTemp(T)); Col += End - Index; Index = End;
			continue;
		}

		if (Ch == TEXT('(')) { FVexToken T; T.Kind = EVexTokenKind::LParen; T.Lexeme = TEXT("("); T.Line = TLine; T.Column = TCol; Tokens.Add(MoveTemp(T)); ++Index; ++Col; continue; }
		if (Ch == TEXT(')')) { FVexToken T; T.Kind = EVexTokenKind::RParen; T.Lexeme = TEXT(")"); T.Line = TLine; T.Column = TCol; Tokens.Add(MoveTemp(T)); ++Index; ++Col; continue; }
		if (Ch == TEXT('{')) { FVexToken T; T.Kind = EVexTokenKind::LBrace; T.Lexeme = TEXT("{"); T.Line = TLine; T.Column = TCol; Tokens.Add(MoveTemp(T)); ++Index; ++Col; continue; }
		if (Ch == TEXT('}')) { FVexToken T; T.Kind = EVexTokenKind::RBrace; T.Lexeme = TEXT("}"); T.Line = TLine; T.Column = TCol; Tokens.Add(MoveTemp(T)); ++Index; ++Col; continue; }
		if (Ch == TEXT(',')) { FVexToken T; T.Kind = EVexTokenKind::Comma; T.Lexeme = TEXT(","); T.Line = TLine; T.Column = TCol; Tokens.Add(MoveTemp(T)); ++Index; ++Col; continue; }
		if (Ch == TEXT('.')) { FVexToken T; T.Kind = EVexTokenKind::Dot; T.Lexeme = TEXT("."); T.Line = TLine; T.Column = TCol; Tokens.Add(MoveTemp(T)); ++Index; ++Col; continue; }
		if (Ch == TEXT(';')) { FVexToken T; T.Kind = EVexTokenKind::Semicolon; T.Lexeme = TEXT(";"); T.Line = TLine; T.Column = TCol; Tokens.Add(MoveTemp(T)); ++Index; ++Col; continue; }

		if (OpsChars.Contains(FString(1, &Ch)))
		{
			int32 End = Index + 1;
			static const TCHAR* MultiOps[] = { TEXT("||"), TEXT("&&"), TEXT("<|"), TEXT("|>"), TEXT("+="), TEXT("-="), TEXT("*="), TEXT("/="), TEXT("=="), TEXT("!="), TEXT("<="), TEXT(">=") };
			if (Index + 1 < Source.Len())
			{
				const FString Sub = Source.Mid(Index, 2);
				for (const TCHAR* MultiOp : MultiOps)
				{
					if (Sub == MultiOp) { End = Index + 2; break; }
				}
			}
			FVexToken T; T.Kind = EVexTokenKind::Operator; T.Lexeme = Source.Mid(Index, End - Index); T.Line = TLine; T.Column = TCol;
			Tokens.Add(MoveTemp(T)); Col += End - Index; Index = End;
			continue;
		}

		AddIssue(EVexIssueSeverity::Error, FString::Printf(TEXT("Unsupported character '%c'."), Ch), TLine, TCol);
		++Index; ++Col;
	}
	Tokens.Add({EVexTokenKind::EndOfFile, TEXT(""), Line, Col});
	return Tokens;
}

} // namespace Flight::Vex
