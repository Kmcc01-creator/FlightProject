// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexTypes.h"
#include "Vex/FlightVexOptics.h"
#include "Schema/FlightRequirementSchema.h"
#include "Schema/FlightRequirementRegistry.h"

namespace Flight::Vex
{

namespace
{
	void AddIssue(TArray<FVexIssue>& Issues, EVexIssueSeverity Severity, const FString& Message, int32 Line, int32 Column)
	{
		FVexIssue Issue;
		Issue.Severity = Severity;
		Issue.Message = Message;
		Issue.Line = Line;
		Issue.Column = Column;
		Issues.Add(Issue);
	}

	bool IsIdentStart(TCHAR Ch) { return FChar::IsAlpha(Ch) || Ch == TEXT('_'); }
	bool IsIdentBody(TCHAR Ch) { return FChar::IsAlnum(Ch) || Ch == TEXT('_'); }
	bool IsDigit(TCHAR Ch) { return FChar::IsDigit(Ch) || Ch == TEXT('.'); }

	TArray<FVexToken> Tokenize(const FString& Source, TArray<FVexIssue>& OutIssues)
	{
		TArray<FVexToken> Tokens;
		int32 Index = 0, Line = 1, Col = 1;
		while (Index < Source.Len())
		{
			const TCHAR Ch = Source[Index];
			const int32 TLine = Line, TCol = Col;
			if (FChar::IsWhitespace(Ch)) { if (Ch == TEXT('\n')) { ++Line; Col = 1; } else ++Col; ++Index; continue; }
			if (Ch == TEXT('#')) { while (Index < Source.Len() && Source[Index] != TEXT('\n')) { ++Index; ++Col; } continue; }
			if (IsIdentStart(Ch)) {
				int32 End = Index + 1; while (End < Source.Len() && IsIdentBody(Source[End])) ++End;
				const FString Lex = Source.Mid(Index, End - Index);
				FVexToken T; T.Lexeme = Lex; T.Line = TLine; T.Column = TCol;
				if (Lex == TEXT("if")) T.Kind = EVexTokenKind::KeywordIf;
				else if (Lex == TEXT("else")) T.Kind = EVexTokenKind::KeywordElse;
				else T.Kind = EVexTokenKind::Identifier;
				Tokens.Add(MoveTemp(T)); Col += End - Index; Index = End; continue;
			}
			if (Ch == TEXT('@')) {
				int32 End = Index + 1;
				if (End < Source.Len() && IsIdentStart(Source[End])) {
					++End; while (End < Source.Len() && IsIdentBody(Source[End])) ++End;
					const FString Lex = Source.Mid(Index, End - Index);
					FVexToken T;
					T.Line = TLine; T.Column = TCol;
					if (Lex == TEXT("@cpu") || Lex == TEXT("@gpu") || Lex == TEXT("@rate") || 
						Lex == TEXT("@job") || Lex == TEXT("@thread") || Lex == TEXT("@async"))
					{
						T.Kind = EVexTokenKind::TargetDirective;
					}
					else
					{
						T.Kind = EVexTokenKind::Symbol;
					}
					T.Lexeme = Lex;
					Tokens.Add(MoveTemp(T));
					Col += End - Index; Index = End; continue;
				}
				AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("Invalid symbol."), TLine, TCol); ++Index; ++Col; continue;
			}
			if (FChar::IsDigit(Ch)) {
				int32 End = Index + 1; while (End < Source.Len() && IsDigit(Source[End])) ++End;
				FVexToken T; T.Kind = EVexTokenKind::Number; T.Lexeme = Source.Mid(Index, End - Index); T.Line = TLine; T.Column = TCol;
				Tokens.Add(MoveTemp(T)); Col += End - Index; Index = End; continue;
			}
			auto AddSingle = [&](EVexTokenKind Kind) { FVexToken T; T.Kind = Kind; T.Lexeme = FString(1, &Ch); T.Line = TLine; T.Column = TCol; Tokens.Add(MoveTemp(T)); ++Index; ++Col; };
			if (Ch == TEXT('(')) AddSingle(EVexTokenKind::LParen);
			else if (Ch == TEXT(')')) AddSingle(EVexTokenKind::RParen);
			else if (Ch == TEXT('{')) AddSingle(EVexTokenKind::LBrace);
			else if (Ch == TEXT('}')) AddSingle(EVexTokenKind::RBrace);
			else if (Ch == TEXT(',')) AddSingle(EVexTokenKind::Comma);
			else if (Ch == TEXT('.')) AddSingle(EVexTokenKind::Dot);
			else if (Ch == TEXT(';')) AddSingle(EVexTokenKind::Semicolon);
			else if (FString(TEXT("+-*/%=<>!|")).Contains(FString(1, &Ch))) {
				int32 End = Index + 1; if (End < Source.Len() && FString(TEXT("=<>!|")).Contains(FString(1, &Source[End]))) ++End;
				FVexToken T; T.Kind = EVexTokenKind::Operator; T.Lexeme = Source.Mid(Index, End - Index); T.Line = TLine; T.Column = TCol;
				Tokens.Add(MoveTemp(T)); Col += End - Index; Index = End;
			}
			else { AddIssue(OutIssues, EVexIssueSeverity::Error, FString::Printf(TEXT("Unsupported character '%c'."), Ch), TLine, TCol); ++Index; ++Col; }
		}
		Tokens.Add({EVexTokenKind::EndOfFile, TEXT(""), Line, Col});
		return Tokens;
	}

	FString BuildSpanText(const TArray<FVexToken>& Tokens, int32 Start, int32 End) { FString S; for (int32 i = Start; i <= End; ++i) { if (!S.IsEmpty()) S += TEXT(" "); S += Tokens[i].Lexeme; } return S; }
	bool IsAssignmentOperator(const FString& Op) { return Op == TEXT("=") || Op == TEXT("+=") || Op == TEXT("-=") || Op == TEXT("*=") || Op == TEXT("/="); }

	void BuildStatementAst(const TArray<FVexToken>& Tokens, TArray<FVexStatementAst>& OutStatements, TArray<FVexIssue>& OutIssues)
	{
		int32 Start = INDEX_NONE, Paren = 0; TArray<FVexToken> Braces;
		for (int32 i = 0; i < Tokens.Num(); ++i)
		{
			const FVexToken& T = Tokens[i]; if (T.Kind == EVexTokenKind::EndOfFile) break; if (Start == INDEX_NONE && T.Kind != EVexTokenKind::Semicolon && T.Kind != EVexTokenKind::TargetDirective) Start = i;
			if (T.Kind == EVexTokenKind::TargetDirective) {
				FVexStatementAst S; S.Kind = EVexStatementKind::TargetDirective; S.StartTokenIndex = i; S.EndTokenIndex = i;
				if (T.Lexeme == TEXT("@rate") && i + 1 < Tokens.Num() && Tokens[i + 1].Kind == EVexTokenKind::LParen) {
					int32 D = 0, RP = INDEX_NONE;
					for (int32 C = i + 1; C < Tokens.Num(); ++C) {
						if (Tokens[C].Kind == EVexTokenKind::LParen) ++D;
						else if (Tokens[C].Kind == EVexTokenKind::RParen) { --D; if (D == 0) { RP = C; break; } }
					}
					if (RP != INDEX_NONE) { S.EndTokenIndex = RP; i = RP; }
				}
				S.SourceSpan = BuildSpanText(Tokens, S.StartTokenIndex, S.EndTokenIndex);
				OutStatements.Add(MoveTemp(S)); Start = INDEX_NONE; continue;
			}
			if (T.Kind == EVexTokenKind::KeywordIf) { if (i + 1 >= Tokens.Num() || Tokens[i + 1].Kind != EVexTokenKind::LParen) AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("`if` must be followed by '('."), T.Line, T.Column); else { int32 D = 0, RP = INDEX_NONE; for (int32 C = i + 1; C < Tokens.Num(); ++C) { if (Tokens[C].Kind == EVexTokenKind::LParen) ++D; else if (Tokens[C].Kind == EVexTokenKind::RParen) { --D; if (D == 0) { RP = C; break; } } } if (RP != INDEX_NONE) { FVexStatementAst S; S.Kind = EVexStatementKind::IfHeader; S.StartTokenIndex = i; S.EndTokenIndex = RP; S.SourceSpan = BuildSpanText(Tokens, i, RP); OutStatements.Add(MoveTemp(S)); Start = INDEX_NONE; } } }
			else if (T.Kind == EVexTokenKind::LBrace) { FVexStatementAst S; S.Kind = EVexStatementKind::BlockOpen; S.StartTokenIndex = i; S.EndTokenIndex = i; S.SourceSpan = TEXT("{"); OutStatements.Add(MoveTemp(S)); Start = INDEX_NONE; }
			else if (T.Kind == EVexTokenKind::RBrace) { FVexStatementAst S; S.Kind = EVexStatementKind::BlockClose; S.StartTokenIndex = i; S.EndTokenIndex = i; S.SourceSpan = TEXT("}"); OutStatements.Add(MoveTemp(S)); Start = INDEX_NONE; }
			else if (T.Kind == EVexTokenKind::Semicolon && Start != INDEX_NONE) { FVexStatementAst S; S.StartTokenIndex = Start; S.EndTokenIndex = i - 1; S.SourceSpan = BuildSpanText(Tokens, Start, i - 1); S.Kind = EVexStatementKind::Expression; for (int32 j = Start; j <= i; ++j) if (Tokens[j].Kind == EVexTokenKind::Operator && IsAssignmentOperator(Tokens[j].Lexeme)) { S.Kind = EVexStatementKind::Assignment; break; } OutStatements.Add(MoveTemp(S)); Start = INDEX_NONE; }
		}
	}

	enum class EVexValueType : uint8 { Unknown, Float, Float2, Float3, Float4, Int, Bool };
	EVexValueType ParseValueType(const FString& T) { if (T == TEXT("float")) return EVexValueType::Float; if (T == TEXT("float2")) return EVexValueType::Float2; if (T == TEXT("float3")) return EVexValueType::Float3; if (T == TEXT("float4")) return EVexValueType::Float4; if (T == TEXT("int")) return EVexValueType::Int; if (T == TEXT("bool")) return EVexValueType::Bool; return EVexValueType::Unknown; }
	bool IsDeclarationTypeToken(const FString& L) { return L == TEXT("float") || L == TEXT("float2") || L == TEXT("float3") || L == TEXT("float4") || L == TEXT("int") || L == TEXT("bool"); }
	bool AreTypesCompatibleForAssignment(EVexValueType L, EVexValueType R, const FString& Op) { if (L == EVexValueType::Unknown || R == EVexValueType::Unknown) return true; return L == R; }

	EVexValueType InferExpressionType(const TArray<FVexToken>& Tokens, const int32 StartIndex, const int32 EndIndex, const TMap<FString, FVexSymbolDefinition>& SymbolByName, const TMap<FString, EVexValueType>& LocalVariables, bool& bHasComparison, bool& bHasLogical)
	{
		for (int32 i = StartIndex; i <= EndIndex; ++i)
		{
			const FVexToken& T = Tokens[i];
			if (T.Kind == EVexTokenKind::Symbol) { if (const auto* D = SymbolByName.Find(T.Lexeme)) return ParseValueType(D->ValueType); }
			if (T.Kind == EVexTokenKind::Identifier) { if (const auto* VT = LocalVariables.Find(T.Lexeme)) return *VT; }
			if (T.Kind == EVexTokenKind::Number) return EVexValueType::Float;
		}
		return EVexValueType::Float;
	}

	EVexValueType InferExpressionTypeFromAst(int32 NodeIdx, const TArray<FVexExpressionAst>& Expressions, const TMap<FString, FVexSymbolDefinition>& SymbolByName, const TMap<FString, EVexValueType>& Locals)
	{
		if (NodeIdx == INDEX_NONE) return EVexValueType::Unknown;
		const auto& N = Expressions[NodeIdx];
		if (N.Kind == EVexExprKind::NumberLiteral) return EVexValueType::Float;
		if (N.Kind == EVexExprKind::SymbolRef) { if (const auto* D = SymbolByName.Find(N.Lexeme)) return ParseValueType(D->ValueType); }
		if (N.Kind == EVexExprKind::Identifier) { if (const auto* VT = Locals.Find(N.Lexeme)) return *VT; }
		if (N.Kind == EVexExprKind::BinaryOp || N.Kind == EVexExprKind::UnaryOp) return InferExpressionTypeFromAst(N.LeftNodeIndex, Expressions, SymbolByName, Locals);
		return EVexValueType::Unknown;
	}

	void PerformSemanticValidation(const TArray<FVexToken>& Tokens, FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, TArray<FVexIssue>& OutIssues)
	{
		TMap<FString, FVexSymbolDefinition> SymbolByName; for (const FVexSymbolDefinition& D : SymbolDefinitions) SymbolByName.Add(D.SymbolName, D);
		TMap<FString, EVexValueType> Locals;
		EFlightVexSymbolResidency CurrentContext = EFlightVexSymbolResidency::Shared;
		EFlightVexSymbolAffinity CurrentAffinity = EFlightVexSymbolAffinity::Any;
		FVexTaskDescriptor* CurrentTask = nullptr;

		for (const FVexStatementAst& S : Program.Statements)
		{
			if (S.Kind == EVexStatementKind::TargetDirective)
			{
				if (S.SourceSpan == TEXT("@cpu")) CurrentContext = EFlightVexSymbolResidency::CpuOnly;
				else if (S.SourceSpan == TEXT("@gpu")) CurrentContext = EFlightVexSymbolResidency::GpuOnly;
				else if (S.SourceSpan == TEXT("@job") || S.SourceSpan == TEXT("@thread") || S.SourceSpan == TEXT("@async"))
				{
					FVexTaskDescriptor Task;
					Task.Type = S.SourceSpan;
					Task.StartTokenIndex = S.StartTokenIndex;
					CurrentAffinity = (S.SourceSpan == TEXT("@async")) ? EFlightVexSymbolAffinity::Any : EFlightVexSymbolAffinity::WorkerThread;
					Program.Tasks.Add(MoveTemp(Task));
					CurrentTask = &Program.Tasks.Last();
				}
				else if (S.SourceSpan.StartsWith(TEXT("@rate")))
				{
					FString Args = S.SourceSpan;
					int32 LP = INDEX_NONE, RP = INDEX_NONE;
					if (Args.FindChar(TEXT('('), LP) && Args.FindChar(TEXT(')'), RP) && RP > LP + 1)
					{
						FString Inner = Args.Mid(LP + 1, RP - LP - 1).TrimStartAndEnd();
						if (Inner.EndsWith(TEXT("Hz"), ESearchCase::IgnoreCase)) { Program.ExecutionRateHz = FCString::Atof(*Inner.LeftChop(2)); Program.FrameInterval = 0; }
						else { Program.FrameInterval = FCString::Atoi(*Inner); Program.ExecutionRateHz = 0.0f; }
					}
				}
				continue;
			}

			if (S.Kind == EVexStatementKind::BlockClose)
			{
				if (CurrentTask)
				{
					CurrentTask->EndTokenIndex = S.EndTokenIndex;
					CurrentTask = nullptr;
					CurrentAffinity = EFlightVexSymbolAffinity::Any;
				}
			}

			if (S.StartTokenIndex == INDEX_NONE || S.EndTokenIndex == INDEX_NONE) continue;

			for (int32 i = S.StartTokenIndex; i <= S.EndTokenIndex; ++i)
			{
				if (Tokens[i].Kind == EVexTokenKind::Symbol)
				{
					if (const FVexSymbolDefinition* D = SymbolByName.Find(Tokens[i].Lexeme))
					{
						if (CurrentTask)
						{
							bool bIsBeingWritten = (S.Kind == EVexStatementKind::Assignment && i == S.StartTokenIndex);
							if (bIsBeingWritten) { CurrentTask->WriteSymbols.Add(D->SymbolName); if (!D->bWritable) AddIssue(OutIssues, EVexIssueSeverity::Error, FString::Printf(TEXT("Symbol '%s' is Read-Only and cannot be captured for writing in %s."), *D->SymbolName, *CurrentTask->Type), Tokens[i].Line, Tokens[i].Column); }
							else CurrentTask->ReadSymbols.Add(D->SymbolName);
						}
						if (D->Residency != EFlightVexSymbolResidency::Shared && CurrentContext != EFlightVexSymbolResidency::Shared)
						{
							if (D->Residency != CurrentContext) AddIssue(OutIssues, EVexIssueSeverity::Error, FString::Printf(TEXT("Symbol '%s' is %s and cannot be used in %s context."), *D->SymbolName, D->Residency == EFlightVexSymbolResidency::GpuOnly ? TEXT("GPU-only") : TEXT("CPU-only"), CurrentContext == EFlightVexSymbolResidency::GpuOnly ? TEXT("@gpu") : TEXT("@cpu")), Tokens[i].Line, Tokens[i].Column);
						}
						if (D->Affinity == EFlightVexSymbolAffinity::GameThread && CurrentAffinity == EFlightVexSymbolAffinity::WorkerThread)
						{
							AddIssue(OutIssues, EVexIssueSeverity::Error, FString::Printf(TEXT("Symbol '%s' is restricted to the Game Thread and cannot be used in a @job or @thread block."), *D->SymbolName), Tokens[i].Line, Tokens[i].Column);
						}
					}
				}
			}

			if (S.Kind == EVexStatementKind::Assignment || S.Kind == EVexStatementKind::Expression)
			{
				int32 OpIdx = INDEX_NONE; for (int32 i = S.StartTokenIndex; i <= S.EndTokenIndex; ++i) { if (Tokens[i].Kind == EVexTokenKind::Operator && IsAssignmentOperator(Tokens[i].Lexeme)) { OpIdx = i; break; } }
				if (S.Kind == EVexStatementKind::Assignment && OpIdx != INDEX_NONE)
				{
					const FVexToken& LTok = Tokens[S.StartTokenIndex]; const FString OpLex = Tokens[OpIdx].Lexeme;
					if (LTok.Kind == EVexTokenKind::Identifier && IsDeclarationTypeToken(LTok.Lexeme))
					{
						const int32 NameIdx = S.StartTokenIndex + 1;
						if (NameIdx < OpIdx && Tokens[NameIdx].Kind == EVexTokenKind::Identifier) { const EVexValueType DType = ParseValueType(LTok.Lexeme); const FString VName = Tokens[NameIdx].Lexeme; Locals.Add(VName, DType); EVexValueType RType = (S.ExpressionNodeIndex != INDEX_NONE) ? InferExpressionTypeFromAst(S.ExpressionNodeIndex, Program.Expressions, SymbolByName, Locals) : EVexValueType::Unknown; if (!AreTypesCompatibleForAssignment(DType, RType, OpLex)) AddIssue(OutIssues, EVexIssueSeverity::Error, FString::Printf(TEXT("Type mismatch for assignment to '%s'."), *VName), Tokens[NameIdx].Line, Tokens[NameIdx].Column); }
					}
					else
					{
						EVexValueType LType = EVexValueType::Unknown; FString LName = LTok.Lexeme;
						if (LTok.Kind == EVexTokenKind::Symbol) { if (const auto* D = SymbolByName.Find(LTok.Lexeme)) { LType = ParseValueType(D->ValueType); if (!D->bWritable) AddIssue(OutIssues, EVexIssueSeverity::Error, FString::Printf(TEXT("Read-only symbol '%s'."), *LTok.Lexeme), LTok.Line, LTok.Column); } }
						else if (LTok.Kind == EVexTokenKind::Identifier) { if (const EVexValueType* T = Locals.Find(LTok.Lexeme)) LType = *T; }
						EVexValueType RType = (S.ExpressionNodeIndex != INDEX_NONE) ? InferExpressionTypeFromAst(S.ExpressionNodeIndex, Program.Expressions, SymbolByName, Locals) : EVexValueType::Unknown;
						if (!AreTypesCompatibleForAssignment(LType, RType, OpLex)) AddIssue(OutIssues, EVexIssueSeverity::Error, FString::Printf(TEXT("Type mismatch for assignment to '%s'."), *LName), LTok.Line, LTok.Column);
					}
				}
			}
		}
		for (const FVexStatementAst& S : Program.Statements) { if (S.Kind == EVexStatementKind::IfHeader && S.ExpressionNodeIndex != INDEX_NONE) { if (InferExpressionTypeFromAst(S.ExpressionNodeIndex, Program.Expressions, SymbolByName, Locals) != EVexValueType::Bool) AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("`if` condition must be bool-like."), Tokens.IsValidIndex(S.StartTokenIndex) ? Tokens[S.StartTokenIndex].Line : 1, Tokens.IsValidIndex(S.StartTokenIndex) ? Tokens[S.StartTokenIndex].Column : 1); } }
	}

	int32 BuildExpressionAst(const TArray<FVexToken>& Tokens, int32 Start, int32 End, TArray<FVexExpressionAst>& OutNodes)
	{
		if (Start > End) return INDEX_NONE;
		if (Start == End)
		{
			FVexExpressionAst Node; Node.TokenIndex = Start; Node.Lexeme = Tokens[Start].Lexeme;
			if (Tokens[Start].Kind == EVexTokenKind::Number) Node.Kind = EVexExprKind::NumberLiteral;
			else if (Tokens[Start].Kind == EVexTokenKind::Symbol) Node.Kind = EVexExprKind::SymbolRef;
			else Node.Kind = EVexExprKind::Identifier;
			return OutNodes.Add(MoveTemp(Node));
		}
		int32 OpIdx = INDEX_NONE;
		for (int32 i = End; i >= Start; --i) { if (Tokens[i].Kind == EVexTokenKind::Operator && !IsAssignmentOperator(Tokens[i].Lexeme)) { OpIdx = i; break; } }
		if (OpIdx == INDEX_NONE)
		{
			// Check for dot operator (member access) - higher precedence
			for (int32 i = End; i >= Start; --i) { if (Tokens[i].Kind == EVexTokenKind::Dot) { OpIdx = i; break; } }
		}

		if (OpIdx != INDEX_NONE)
		{
			FVexExpressionAst Node; Node.Kind = EVexExprKind::BinaryOp; Node.TokenIndex = OpIdx; Node.Lexeme = Tokens[OpIdx].Lexeme;
			Node.LeftNodeIndex = BuildExpressionAst(Tokens, Start, OpIdx - 1, OutNodes);
			Node.RightNodeIndex = BuildExpressionAst(Tokens, OpIdx + 1, End, OutNodes);
			return OutNodes.Add(MoveTemp(Node));
		}
		return INDEX_NONE;
	}
}

EVexRequirement AnalyzeRequirements(const FVexProgramAst& Program)
{
	EVexRequirement Req = EVexRequirement::None;
	for (const auto& N : Program.Expressions)
	{
		if (N.Kind == EVexExprKind::SymbolRef) Req |= EVexRequirement::SwarmState;
		if (N.Kind == EVexExprKind::FunctionCall || N.Kind == EVexExprKind::Pipe)
		{
			const FString& Name = N.Lexeme;
			if (Name == TEXT("sample_lattice") || Name == TEXT("inject_light")) Req |= EVexRequirement::LightLattice;
			else if (Name == TEXT("sample_cloud") || Name == TEXT("inject_cloud")) Req |= EVexRequirement::VolumetricCloud;
			else if (Name == TEXT("sample_sdf") || Name.StartsWith(TEXT("sd")) || Name.StartsWith(TEXT("op"))) Req |= EVexRequirement::StructureSDF;
		}
	}
	for (const FVexStatementAst& S : Program.Statements)
	{
		if (S.Kind == EVexStatementKind::TargetDirective) { if (S.SourceSpan == TEXT("@async")) Req |= EVexRequirement::Async; continue; }
		if (S.Kind == EVexStatementKind::Assignment) Req |= EVexRequirement::WriteState;
	}
	return Req;
}

TArray<FVexSymbolDefinition> BuildSymbolDefinitionsFromManifest(const Flight::Schema::FManifestData& ManifestData)
{
	TArray<FVexSymbolDefinition> Defs;
	{
		FVexSymbolDefinition Dt; Dt.SymbolName = TEXT("@dt"); Dt.ValueType = TEXT("float"); Dt.Residency = EFlightVexSymbolResidency::Shared; Dt.bWritable = false; Dt.bRequired = false; Defs.Add(Dt);
		FVexSymbolDefinition Frame; Frame.SymbolName = TEXT("@frame"); Frame.ValueType = TEXT("int"); Frame.Residency = EFlightVexSymbolResidency::Shared; Frame.bWritable = false; Frame.bRequired = false; Defs.Add(Frame);
		FVexSymbolDefinition Time; Time.SymbolName = TEXT("@time"); Time.ValueType = TEXT("float"); Time.Residency = EFlightVexSymbolResidency::Shared; Time.bWritable = false; Time.bRequired = false; Defs.Add(Time);
	}
	for (const FFlightVexSymbolRow& R : ManifestData.VexSymbolRequirements)
	{
		FVexSymbolDefinition D; D.SymbolName = R.SymbolName;
		switch (R.ValueType) { case EFlightVexSymbolValueType::Float: D.ValueType = TEXT("float"); break; case EFlightVexSymbolValueType::Float2: D.ValueType = TEXT("float2"); break; case EFlightVexSymbolValueType::Float3: D.ValueType = TEXT("float3"); break; case EFlightVexSymbolValueType::Float4: D.ValueType = TEXT("float4"); break; case EFlightVexSymbolValueType::Int: D.ValueType = TEXT("int"); break; case EFlightVexSymbolValueType::Bool: D.ValueType = TEXT("bool"); break; default: D.ValueType = TEXT("unknown"); break; }
		D.Residency = R.Residency; D.Affinity = R.Affinity; D.bWritable = R.bWritable; D.bRequired = R.bRequired; Defs.Add(MoveTemp(D));
	}
	return Defs;
}

FVexParseResult ParseAndValidate(const FString& Source, const TArray<FVexSymbolDefinition>& SymbolDefinitions, bool bRequireAllRequiredSymbols)
{
	FVexParseResult Res; Res.Program.Tokens = Tokenize(Source, Res.Issues);
	BuildStatementAst(Res.Program.Tokens, Res.Program.Statements, Res.Issues);
	TMap<FString, FVexSymbolDefinition> SymbolByName; for (const FVexSymbolDefinition& D : SymbolDefinitions) SymbolByName.Add(D.SymbolName, D);
	TSet<FString> Known; for (const auto& D : SymbolDefinitions) Known.Add(D.SymbolName);
	for (auto& S : Res.Program.Statements) { if (S.Kind == EVexStatementKind::Expression || S.Kind == EVexStatementKind::Assignment || S.Kind == EVexStatementKind::IfHeader) S.ExpressionNodeIndex = BuildExpressionAst(Res.Program.Tokens, S.Kind == EVexStatementKind::Assignment ? S.StartTokenIndex + 2 : S.StartTokenIndex, S.EndTokenIndex, Res.Program.Expressions); }
	TSet<FString> Used; for (const FVexToken& T : Res.Program.Tokens) if (T.Kind == EVexTokenKind::Symbol) Used.Add(T.Lexeme);
	Res.Program.UsedSymbols = Used.Array(); Res.Program.UsedSymbols.Sort();
	for (const FString& U : Res.Program.UsedSymbols) if (Known.Num() > 0 && !Known.Contains(U)) { Res.UnknownSymbols.Add(U); AddIssue(Res.Issues, EVexIssueSeverity::Warning, FString::Printf(TEXT("Unknown symbol '%s'."), *U), 1, 1); }
	PerformSemanticValidation(Res.Program.Tokens, Res.Program, SymbolDefinitions, Res.Issues); Res.Requirements = AnalyzeRequirements(Res.Program);
	
	// Apply m2-inspired algebraic optimizations
	FVexOptimizer::Optimize(Res.Program);

	Res.bSuccess = true; for (const auto& I : Res.Issues) if (I.Severity == EVexIssueSeverity::Error) Res.bSuccess = false;
	return Res;
}

FString FormatIssues(const TArray<FVexIssue>& Issues) { FString F; for (const FVexIssue& I : Issues) F += FString::Printf(TEXT("[%s L%d:C%d] %s\n"), I.Severity == EVexIssueSeverity::Error ? TEXT("error") : TEXT("warning"), I.Line, I.Column, *I.Message); return F; }

namespace Lower
{
	FString Expression(int32 NodeIdx, const FVexProgramAst& Program, const TMap<FString, FString>& SymbolMap)
	{
		if (NodeIdx == INDEX_NONE) return TEXT("");
		const auto& Node = Program.Expressions[NodeIdx];
		switch (Node.Kind) {
		case EVexExprKind::NumberLiteral: return Node.Lexeme;
		case EVexExprKind::Identifier: return Node.Lexeme;
		case EVexExprKind::SymbolRef: return SymbolMap.Contains(Node.Lexeme) ? SymbolMap[Node.Lexeme] : Node.Lexeme;
		case EVexExprKind::UnaryOp: return Node.Lexeme + Expression(Node.LeftNodeIndex, Program, SymbolMap);
		case EVexExprKind::BinaryOp: return FString::Printf(TEXT("(%s %s %s)"), *Expression(Node.LeftNodeIndex, Program, SymbolMap), *Node.Lexeme, *Expression(Node.RightNodeIndex, Program, SymbolMap));
		default: return TEXT("/* unknown */");
		}
	}

	FString Program(const FVexProgramAst& ProgramAst, const TMap<FString, FString>& SymbolMap, const bool bVerseStyle)
	{
		FString Out; int32 Ind = 0; auto AddInd = [&](int32 L) { for (int32 i = 0; i < L; ++i) Out += TEXT("    "); };
		for (const FVexStatementAst& S : ProgramAst.Statements)
		{
			switch (S.Kind) {
			case EVexStatementKind::TargetDirective: break;
			case EVexStatementKind::BlockOpen: if (bVerseStyle) Out += TEXT(":\n"); else Out += TEXT("{\n"); Ind++; break;
			case EVexStatementKind::BlockClose: Ind--; if (!bVerseStyle) { AddInd(Ind); Out += TEXT("}\n"); } break;
			case EVexStatementKind::IfHeader: AddInd(Ind); Out += FString::Printf(TEXT("if (%s)%s"), *Expression(S.ExpressionNodeIndex, ProgramAst, SymbolMap), bVerseStyle ? TEXT(":") : TEXT(" ")); if (!bVerseStyle) Out += TEXT("\n"); break;
			case EVexStatementKind::Assignment:
			{
				AddInd(Ind); FString L; int32 Op = INDEX_NONE; for (int32 i = S.StartTokenIndex; i <= S.EndTokenIndex; ++i) { if (ProgramAst.Tokens[i].Kind == EVexTokenKind::Operator && IsAssignmentOperator(ProgramAst.Tokens[i].Lexeme)) { Op = i; break; } if (!L.IsEmpty()) L += TEXT(" "); L += ProgramAst.Tokens[i].Lexeme; }
				for (const auto& E : SymbolMap) L.ReplaceInline(*E.Key, *E.Value);
				if (bVerseStyle) Out += FString::Printf(TEXT("set %s %s %s\n"), *L, Op != INDEX_NONE ? *ProgramAst.Tokens[Op].Lexeme : TEXT("="), *Expression(S.ExpressionNodeIndex, ProgramAst, SymbolMap));
				else Out += FString::Printf(TEXT("%s %s %s;\n"), *L, Op != INDEX_NONE ? *ProgramAst.Tokens[Op].Lexeme : TEXT("="), *Expression(S.ExpressionNodeIndex, ProgramAst, SymbolMap));
				break;
			}
			case EVexStatementKind::Expression: AddInd(Ind); Out += Expression(S.ExpressionNodeIndex, ProgramAst, SymbolMap) + (bVerseStyle ? TEXT("\n") : TEXT(";\n")); break;
			}
		}
		return Out;
	}
}

FString LowerToHLSL(const FVexProgramAst& Program, const TMap<FString, FString>& HlslBySymbol)
{
	TMap<FString, FString> Symbols = HlslBySymbol;
	Symbols.Add(TEXT("@dt"), TEXT("DeltaTime"));
	Symbols.Add(TEXT("@frame"), TEXT("FrameIndex"));
	Symbols.Add(TEXT("@time"), TEXT("(FrameIndex * DeltaTime)"));
	return Lower::Program(Program, Symbols, false);
}

FString LowerToVerse(const FVexProgramAst& Program, const TMap<FString, FString>& VerseBySymbol)
{
	TMap<FString, FString> Symbols = VerseBySymbol;
	Symbols.Add(TEXT("@dt"), TEXT("Sim.DeltaTime"));
	Symbols.Add(TEXT("@frame"), TEXT("Sim.Frame"));
	Symbols.Add(TEXT("@time"), TEXT("Sim.TotalTime"));
	bool bIsAsync = false;
	for (const auto& S : Program.Statements) { if (S.Kind == EVexStatementKind::TargetDirective && S.SourceSpan == TEXT("@async")) { bIsAsync = true; break; } }
	FString Body = Lower::Program(Program, Symbols, true);
	FString Effects = bIsAsync ? TEXT("<transacts><async>") : TEXT("<transacts>");
	return FString::Printf(TEXT("ProcessDroid(Droid:droid_state, Sim:sim_context)%s:void =\n%s"), *Effects, *Body);
}

FString LowerMegaKernel(const TMap<uint32, FVexProgramAst>& Scripts, const TMap<FString, FString>& SymbolMap)
{
    FString Res = TEXT("// SCSL Unified Mega-Kernel\n");
    FString Hoist = TEXT("// --- Global Attribute Hoisting ---\n");
    FString Dispatch = TEXT("// --- Behavior Dispatch ---\n");
    TMap<FString, FString> LocalMap = SymbolMap;
    for (const auto& KV : Scripts)
    {
        Dispatch += FString::Printf(TEXT("        case %u:\n        {\n"), KV.Key);
        Dispatch += Lower::Program(KV.Value, LocalMap, false);
        Dispatch += TEXT("            break;\n        }\n");
    }
    Dispatch += TEXT("    }\n");
    return Res + Hoist + Dispatch;
}

} // namespace Flight::Vex
