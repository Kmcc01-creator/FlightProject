// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexTypes.h"
#include "Vex/FlightVexOptics.h"

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
			else if (FString(TEXT("+-*/%=<>!|&")).Contains(FString(1, &Ch))) {
				int32 End = Index + 1;
				// Maximal Munch: Sort multi-char operators by descending length
				static const TCHAR* MultiOps[] = { TEXT("||"), TEXT("&&"), TEXT("<|"), TEXT("|>"), TEXT("+="), TEXT("-="), TEXT("*="), TEXT("/="), TEXT("=="), TEXT("!="), TEXT("<="), TEXT(">=") };
				if (Index + 1 < Source.Len())
				{
					const FString Sub = Source.Mid(Index, 2);
					for (const TCHAR* MultiOp : MultiOps)
					{
						if (Sub == MultiOp)
						{
							End = Index + 2;
							break;
						}
					}
				}
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
		int32 StatementStart = INDEX_NONE;
		int32 ParenDepth = 0;
		int32 ExpressionBraceDepth = 0; // Used for vector literals like {1,2,3}.

		const auto EmitStatement = [&](const int32 EndTokenIndex)
		{
			if (StatementStart == INDEX_NONE || EndTokenIndex < StatementStart)
			{
				return;
			}

			FVexStatementAst Statement;
			Statement.StartTokenIndex = StatementStart;
			Statement.EndTokenIndex = EndTokenIndex;
			Statement.SourceSpan = BuildSpanText(Tokens, StatementStart, EndTokenIndex);
			Statement.Kind = EVexStatementKind::Expression;

			for (int32 TokenIndex = StatementStart; TokenIndex <= EndTokenIndex; ++TokenIndex)
			{
				if (Tokens[TokenIndex].Kind == EVexTokenKind::Operator && IsAssignmentOperator(Tokens[TokenIndex].Lexeme))
				{
					Statement.Kind = EVexStatementKind::Assignment;
					break;
				}
			}

			OutStatements.Add(MoveTemp(Statement));
		};

		for (int32 TokenIndex = 0; TokenIndex < Tokens.Num(); ++TokenIndex)
		{
			const FVexToken& Token = Tokens[TokenIndex];
			if (Token.Kind == EVexTokenKind::EndOfFile)
			{
				break;
			}

			if (Token.Kind == EVexTokenKind::TargetDirective && StatementStart == INDEX_NONE && ParenDepth == 0 && ExpressionBraceDepth == 0)
			{
				FVexStatementAst Directive;
				Directive.Kind = EVexStatementKind::TargetDirective;
				Directive.StartTokenIndex = TokenIndex;
				Directive.EndTokenIndex = TokenIndex;

				if (Token.Lexeme == TEXT("@rate") && TokenIndex + 1 < Tokens.Num() && Tokens[TokenIndex + 1].Kind == EVexTokenKind::LParen)
				{
					int32 Depth = 0;
					int32 RightParen = INDEX_NONE;
					for (int32 Cursor = TokenIndex + 1; Cursor < Tokens.Num(); ++Cursor)
					{
						if (Tokens[Cursor].Kind == EVexTokenKind::LParen)
						{
							++Depth;
						}
						else if (Tokens[Cursor].Kind == EVexTokenKind::RParen)
						{
							--Depth;
							if (Depth == 0)
							{
								RightParen = Cursor;
								break;
							}
						}
					}

					if (RightParen != INDEX_NONE)
					{
						Directive.EndTokenIndex = RightParen;
						TokenIndex = RightParen;
					}
				}

				Directive.SourceSpan = BuildSpanText(Tokens, Directive.StartTokenIndex, Directive.EndTokenIndex);
				OutStatements.Add(MoveTemp(Directive));
				continue;
			}

			if (Token.Kind == EVexTokenKind::KeywordIf && StatementStart == INDEX_NONE && ParenDepth == 0 && ExpressionBraceDepth == 0)
			{
				if (TokenIndex + 1 >= Tokens.Num() || Tokens[TokenIndex + 1].Kind != EVexTokenKind::LParen)
				{
					AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("`if` must be followed by '('."), Token.Line, Token.Column);
					continue;
				}

				int32 Depth = 0;
				int32 RightParen = INDEX_NONE;
				for (int32 Cursor = TokenIndex + 1; Cursor < Tokens.Num(); ++Cursor)
				{
					if (Tokens[Cursor].Kind == EVexTokenKind::LParen)
					{
						++Depth;
					}
					else if (Tokens[Cursor].Kind == EVexTokenKind::RParen)
					{
						--Depth;
						if (Depth == 0)
						{
							RightParen = Cursor;
							break;
						}
					}
				}

				if (RightParen != INDEX_NONE)
				{
					FVexStatementAst IfHeader;
					IfHeader.Kind = EVexStatementKind::IfHeader;
					IfHeader.StartTokenIndex = TokenIndex;
					IfHeader.EndTokenIndex = RightParen;
					IfHeader.SourceSpan = BuildSpanText(Tokens, TokenIndex, RightParen);
					OutStatements.Add(MoveTemp(IfHeader));
					TokenIndex = RightParen;
				}

				continue;
			}

			if (Token.Kind == EVexTokenKind::LParen)
			{
				if (StatementStart == INDEX_NONE)
				{
					StatementStart = TokenIndex;
				}
				++ParenDepth;
				continue;
			}

			if (Token.Kind == EVexTokenKind::RParen)
			{
				if (ParenDepth > 0)
				{
					--ParenDepth;
				}
				continue;
			}

			if (Token.Kind == EVexTokenKind::LBrace)
			{
				const bool bTreatAsBlockBrace = (StatementStart == INDEX_NONE && ParenDepth == 0 && ExpressionBraceDepth == 0);
				if (bTreatAsBlockBrace)
				{
					FVexStatementAst BlockOpen;
					BlockOpen.Kind = EVexStatementKind::BlockOpen;
					BlockOpen.StartTokenIndex = TokenIndex;
					BlockOpen.EndTokenIndex = TokenIndex;
					BlockOpen.SourceSpan = TEXT("{");
					OutStatements.Add(MoveTemp(BlockOpen));
				}
				else
				{
					if (StatementStart == INDEX_NONE)
					{
						StatementStart = TokenIndex;
					}
					++ExpressionBraceDepth;
				}
				continue;
			}

			if (Token.Kind == EVexTokenKind::RBrace)
			{
				if (ExpressionBraceDepth > 0)
				{
					--ExpressionBraceDepth;
					continue;
				}

				if (StatementStart != INDEX_NONE)
				{
					EmitStatement(TokenIndex - 1);
					StatementStart = INDEX_NONE;
				}

				FVexStatementAst BlockClose;
				BlockClose.Kind = EVexStatementKind::BlockClose;
				BlockClose.StartTokenIndex = TokenIndex;
				BlockClose.EndTokenIndex = TokenIndex;
				BlockClose.SourceSpan = TEXT("}");
				OutStatements.Add(MoveTemp(BlockClose));
				continue;
			}

			if (Token.Kind == EVexTokenKind::Semicolon && ParenDepth == 0 && ExpressionBraceDepth == 0)
			{
				EmitStatement(TokenIndex - 1);
				StatementStart = INDEX_NONE;
				continue;
			}

			if (StatementStart == INDEX_NONE)
			{
				StatementStart = TokenIndex;
			}
		}

		if (StatementStart != INDEX_NONE)
		{
			EmitStatement(Tokens.Num() - 2); // Exclude EOF.
		}
	}

	EVexValueType TypeFromVectorComponentSelector(const FString& Selector)
	{
		const int32 Width = Selector.Len();
		if (Width <= 1) return EVexValueType::Float;
		if (Width == 2) return EVexValueType::Float2;
		if (Width == 3) return EVexValueType::Float3;
		if (Width == 4) return EVexValueType::Float4;
		return EVexValueType::Unknown;
	}

	bool AreTypesCompatibleForAssignment(const EVexValueType LeftType, const EVexValueType RightType, const FString& Op)
	{
		(void)Op;
		if (LeftType == EVexValueType::Unknown || RightType == EVexValueType::Unknown)
		{
			return true;
		}
		return LeftType == RightType;
	}

	EVexValueType InferExpressionTypeFromAst(int32 NodeIdx, const TArray<FVexExpressionAst>& Expressions, const TMap<FString, FVexSymbolDefinition>& SymbolByName, const TMap<FString, EVexValueType>& Locals)
	{
		if (NodeIdx == INDEX_NONE || !Expressions.IsValidIndex(NodeIdx))
		{
			return EVexValueType::Unknown;
		}

		const FVexExpressionAst& Node = Expressions[NodeIdx];
		switch (Node.Kind)
		{
		case EVexExprKind::NumberLiteral:
			return Node.Lexeme.Contains(TEXT(".")) ? EVexValueType::Float : EVexValueType::Int;
		case EVexExprKind::SymbolRef:
			if (const FVexSymbolDefinition* Definition = SymbolByName.Find(Node.Lexeme))
			{
				return ParseValueType(Definition->ValueType);
			}
			return EVexValueType::Unknown;
		case EVexExprKind::Identifier:
			if (const EVexValueType* LocalType = Locals.Find(Node.Lexeme))
			{
				return *LocalType;
			}
			return EVexValueType::Unknown;
		case EVexExprKind::UnaryOp:
			if (Node.Lexeme == TEXT("!"))
			{
				return EVexValueType::Bool;
			}
			return InferExpressionTypeFromAst(Node.LeftNodeIndex, Expressions, SymbolByName, Locals);
		case EVexExprKind::BinaryOp:
		{
			const EVexValueType LeftType = InferExpressionTypeFromAst(Node.LeftNodeIndex, Expressions, SymbolByName, Locals);
			const EVexValueType RightType = InferExpressionTypeFromAst(Node.RightNodeIndex, Expressions, SymbolByName, Locals);

			if (Node.Lexeme == TEXT("."))
			{
				if (IsVectorType(LeftType) && Expressions.IsValidIndex(Node.RightNodeIndex))
				{
					return TypeFromVectorComponentSelector(Expressions[Node.RightNodeIndex].Lexeme);
				}
				return EVexValueType::Unknown;
			}

			if (Node.Lexeme == TEXT("<") || Node.Lexeme == TEXT(">") || Node.Lexeme == TEXT("<=") || Node.Lexeme == TEXT(">=")
				|| Node.Lexeme == TEXT("==") || Node.Lexeme == TEXT("!=") || Node.Lexeme == TEXT("&&") || Node.Lexeme == TEXT("||"))
			{
				return EVexValueType::Bool;
			}

			if (LeftType == RightType)
			{
				return LeftType;
			}

			if (IsVectorType(LeftType) && (RightType == EVexValueType::Float || RightType == EVexValueType::Int))
			{
				return LeftType;
			}
			if (IsVectorType(RightType) && (LeftType == EVexValueType::Float || LeftType == EVexValueType::Int))
			{
				return RightType;
			}
			if ((LeftType == EVexValueType::Int && RightType == EVexValueType::Float)
				|| (LeftType == EVexValueType::Float && RightType == EVexValueType::Int))
			{
				return EVexValueType::Float;
			}

			return LeftType != EVexValueType::Unknown ? LeftType : RightType;
		}
		case EVexExprKind::FunctionCall:
		{
			const FString& Name = Node.Lexeme;
			if (Name == TEXT("normalize"))
			{
				return Node.ArgumentNodeIndices.Num() > 0
					? InferExpressionTypeFromAst(Node.ArgumentNodeIndices[0], Expressions, SymbolByName, Locals)
					: EVexValueType::Unknown;
			}
			if (Name == TEXT("curlnoise")) return EVexValueType::Float3;
			if (Name == TEXT("dot")) return EVexValueType::Float;
			if (Name == TEXT("sample_sdf")) return EVexValueType::Float;
			if (Name == TEXT("gradient")) return EVexValueType::Float3;
			if (Name == TEXT("sample_lattice")) return EVexValueType::Float3;
			if (Name == TEXT("sample_cloud")) return EVexValueType::Float;
			if (Name == TEXT("smoothstep")) return EVexValueType::Float;
			if (Name == TEXT("clamp"))
			{
				return Node.ArgumentNodeIndices.Num() > 0
					? InferExpressionTypeFromAst(Node.ArgumentNodeIndices[0], Expressions, SymbolByName, Locals)
					: EVexValueType::Unknown;
			}
			return EVexValueType::Unknown;
		}
		case EVexExprKind::Pipe:
		case EVexExprKind::PipeIn:
		case EVexExprKind::PipeOut:
		{
			const EVexValueType PipedType = InferExpressionTypeFromAst(Node.LeftNodeIndex, Expressions, SymbolByName, Locals);
			if (Expressions.IsValidIndex(Node.RightNodeIndex))
			{
				const FVexExpressionAst& RightNode = Expressions[Node.RightNodeIndex];
				if (RightNode.Kind == EVexExprKind::FunctionCall)
				{
					if (RightNode.Lexeme == TEXT("sample_sdf")) return EVexValueType::Float;
					if (RightNode.Lexeme == TEXT("gradient")) return EVexValueType::Float3;
					if (RightNode.Lexeme == TEXT("smoothstep")) return EVexValueType::Float;
					if (RightNode.Lexeme == TEXT("count_hits")) return EVexValueType::Int;
					if (RightNode.Lexeme == TEXT("clamp") || RightNode.Lexeme == TEXT("normalize")) return PipedType;
				}
				return InferExpressionTypeFromAst(Node.RightNodeIndex, Expressions, SymbolByName, Locals);
			}
			return PipedType;
		}
		default:
			return EVexValueType::Unknown;
		}
	}

	void AnnotateInferredTypes(FVexProgramAst& Program, const TMap<FString, FVexSymbolDefinition>& SymbolByName, const TMap<FString, EVexValueType>& Locals)
	{
		for (int32 NodeIndex = 0; NodeIndex < Program.Expressions.Num(); ++NodeIndex)
		{
			Program.Expressions[NodeIndex].InferredType =
				ToTypeString(InferExpressionTypeFromAst(NodeIndex, Program.Expressions, SymbolByName, Locals));
		}
	}

	TMap<FString, EVexValueType> BuildLocalVariableTypeMap(const FVexProgramAst& Program)
	{
		TMap<FString, EVexValueType> Locals;
		for (const FVexStatementAst& Statement : Program.Statements)
		{
			if (Statement.Kind != EVexStatementKind::Assignment
				|| !Program.Tokens.IsValidIndex(Statement.StartTokenIndex)
				|| !Program.Tokens.IsValidIndex(Statement.StartTokenIndex + 1))
			{
				continue;
			}

			const FVexToken& TypeToken = Program.Tokens[Statement.StartTokenIndex];
			const FVexToken& NameToken = Program.Tokens[Statement.StartTokenIndex + 1];
			if (TypeToken.Kind == EVexTokenKind::Identifier
				&& NameToken.Kind == EVexTokenKind::Identifier
				&& IsDeclarationTypeToken(TypeToken.Lexeme))
			{
				Locals.Add(NameToken.Lexeme, ParseValueType(TypeToken.Lexeme));
			}
		}
		return Locals;
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

			if (S.ExpressionNodeIndex != INDEX_NONE)
			{
				TArray<int32> NodeStack;
				NodeStack.Add(S.ExpressionNodeIndex);
				while (NodeStack.Num() > 0)
				{
					const int32 NodeIndex = NodeStack.Pop(EAllowShrinking::No);
					if (!Program.Expressions.IsValidIndex(NodeIndex))
					{
						continue;
					}

					const FVexExpressionAst& Node = Program.Expressions[NodeIndex];
					if (Node.Kind == EVexExprKind::PipeIn)
					{
						if (CurrentContext != EFlightVexSymbolResidency::GpuOnly)
						{
							const int32 TLine = Program.Tokens.IsValidIndex(Node.TokenIndex) ? Program.Tokens[Node.TokenIndex].Line : 1;
							const int32 TCol = Program.Tokens.IsValidIndex(Node.TokenIndex) ? Program.Tokens[Node.TokenIndex].Column : 1;
							AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("Inject pipe `<|` can only be used within a `@gpu` context."), TLine, TCol);
						}
					}
					else if (Node.Kind == EVexExprKind::PipeOut)
					{
						if (CurrentContext != EFlightVexSymbolResidency::CpuOnly)
						{
							const int32 TLine = Program.Tokens.IsValidIndex(Node.TokenIndex) ? Program.Tokens[Node.TokenIndex].Line : 1;
							const int32 TCol = Program.Tokens.IsValidIndex(Node.TokenIndex) ? Program.Tokens[Node.TokenIndex].Column : 1;
							AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("Extract pipe `|>` can only be used within a `@cpu` context."), TLine, TCol);
						}
					}

					if (Node.LeftNodeIndex != INDEX_NONE) NodeStack.Add(Node.LeftNodeIndex);
					if (Node.RightNodeIndex != INDEX_NONE) NodeStack.Add(Node.RightNodeIndex);
					for (const int32 ArgIndex : Node.ArgumentNodeIndices)
					{
						if (ArgIndex != INDEX_NONE)
						{
							NodeStack.Add(ArgIndex);
						}
					}
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
		for (const FVexStatementAst& S : Program.Statements)
		{
			if (S.Kind == EVexStatementKind::IfHeader && S.ExpressionNodeIndex != INDEX_NONE)
			{
				if (InferExpressionTypeFromAst(S.ExpressionNodeIndex, Program.Expressions, SymbolByName, Locals) != EVexValueType::Bool)
				{
					AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("`if` condition must be bool-like."),
						Tokens.IsValidIndex(S.StartTokenIndex) ? Tokens[S.StartTokenIndex].Line : 1,
						Tokens.IsValidIndex(S.StartTokenIndex) ? Tokens[S.StartTokenIndex].Column : 1);
				}
			}
		}

		AnnotateInferredTypes(Program, SymbolByName, Locals);
	}

	class FExpressionParser
	{
	public:
		FExpressionParser(const TArray<FVexToken>& InTokens, const int32 InStart, const int32 InEnd, TArray<FVexExpressionAst>& InOutNodes)
			: Tokens(InTokens)
			, Cursor(InStart)
			, End(InEnd)
			, OutNodes(InOutNodes)
		{
		}

		int32 ParseExpression()
		{
			return ParsePipe();
		}

		int32 GetCursor() const
		{
			return Cursor;
		}

	private:
		const TArray<FVexToken>& Tokens;
		int32 Cursor;
		int32 End;
		TArray<FVexExpressionAst>& OutNodes;

		bool IsAtEnd() const
		{
			return Cursor > End;
		}

		const FVexToken* Peek(const int32 Offset = 0) const
		{
			const int32 Index = Cursor + Offset;
			return (Index >= 0 && Index <= End && Tokens.IsValidIndex(Index)) ? &Tokens[Index] : nullptr;
		}

		bool MatchKind(const EVexTokenKind Kind)
		{
			const FVexToken* Token = Peek();
			if (!Token || Token->Kind != Kind)
			{
				return false;
			}
			++Cursor;
			return true;
		}

		bool MatchOperator(const FString& OpLexeme)
		{
			const FVexToken* Token = Peek();
			if (!Token || Token->Kind != EVexTokenKind::Operator || Token->Lexeme != OpLexeme)
			{
				return false;
			}
			++Cursor;
			return true;
		}

		int32 AddNode(FVexExpressionAst&& Node)
		{
			return OutNodes.Add(MoveTemp(Node));
		}

		int32 ParsePipe()
		{
			int32 Left = ParseLogicalOr();
			for (;;)
			{
				const FVexToken* Token = Peek();
				if (Token && Token->Kind == EVexTokenKind::Operator && (Token->Lexeme == TEXT("|") || Token->Lexeme == TEXT("<|") || Token->Lexeme == TEXT("|>")))
				{
					const FString OpLex = Token->Lexeme;
					const int32 OpTokenIndex = Cursor;
					++Cursor;
					const int32 Right = ParseLogicalOr();

					FVexExpressionAst PipeNode;
					if (OpLex == TEXT("<|")) PipeNode.Kind = EVexExprKind::PipeIn;
					else if (OpLex == TEXT("|>")) PipeNode.Kind = EVexExprKind::PipeOut;
					else PipeNode.Kind = EVexExprKind::Pipe;

					PipeNode.TokenIndex = OpTokenIndex;
					PipeNode.Lexeme = OpLex;
					PipeNode.LeftNodeIndex = Left;
					PipeNode.RightNodeIndex = Right;

					if (OutNodes.IsValidIndex(Right) &&
						(OutNodes[Right].Kind == EVexExprKind::FunctionCall || OutNodes[Right].Kind == EVexExprKind::Identifier))
					{
						PipeNode.Lexeme = OutNodes[Right].Lexeme;
					}

					Left = AddNode(MoveTemp(PipeNode));
					continue;
				}
				break;
			}
			return Left;
		}

		int32 ParseLogicalOr()
		{
			int32 Left = ParseLogicalAnd();
			for (;;)
			{
				const FVexToken* Token = Peek();
				if (Token && Token->Kind == EVexTokenKind::Operator && Token->Lexeme == TEXT("||"))
				{
					const int32 OpTokenIndex = Cursor;
					++Cursor;
					const int32 Right = ParseLogicalAnd();
					FVexExpressionAst Node;
					Node.Kind = EVexExprKind::BinaryOp;
					Node.TokenIndex = OpTokenIndex;
					Node.Lexeme = TEXT("||");
					Node.LeftNodeIndex = Left;
					Node.RightNodeIndex = Right;
					Left = AddNode(MoveTemp(Node));
					continue;
				}
				break;
			}
			return Left;
		}

		int32 ParseLogicalAnd()
		{
			int32 Left = ParseEquality();
			for (;;)
			{
				const FVexToken* Token = Peek();
				if (Token && Token->Kind == EVexTokenKind::Operator && Token->Lexeme == TEXT("&&"))
				{
					const int32 OpTokenIndex = Cursor;
					++Cursor;
					const int32 Right = ParseEquality();
					FVexExpressionAst Node;
					Node.Kind = EVexExprKind::BinaryOp;
					Node.TokenIndex = OpTokenIndex;
					Node.Lexeme = TEXT("&&");
					Node.LeftNodeIndex = Left;
					Node.RightNodeIndex = Right;
					Left = AddNode(MoveTemp(Node));
					continue;
				}
				break;
			}
			return Left;
		}

		int32 ParseEquality()
		{
			int32 Left = ParseComparison();
			for (;;)
			{
				if (MatchOperator(TEXT("==")) || MatchOperator(TEXT("!=")))
				{
					const FString OperatorLexeme = Tokens[Cursor - 1].Lexeme;
					const int32 OpTokenIndex = Cursor - 1;
					const int32 Right = ParseComparison();
					FVexExpressionAst Node;
					Node.Kind = EVexExprKind::BinaryOp;
					Node.TokenIndex = OpTokenIndex;
					Node.Lexeme = OperatorLexeme;
					Node.LeftNodeIndex = Left;
					Node.RightNodeIndex = Right;
					Left = AddNode(MoveTemp(Node));
					continue;
				}
				break;
			}
			return Left;
		}

		int32 ParseComparison()
		{
			int32 Left = ParseAdditive();
			for (;;)
			{
				if (MatchOperator(TEXT("<=")) || MatchOperator(TEXT(">=")) || MatchOperator(TEXT("<")) || MatchOperator(TEXT(">")))
				{
					const FString OperatorLexeme = Tokens[Cursor - 1].Lexeme;
					const int32 OpTokenIndex = Cursor - 1;
					const int32 Right = ParseAdditive();
					FVexExpressionAst Node;
					Node.Kind = EVexExprKind::BinaryOp;
					Node.TokenIndex = OpTokenIndex;
					Node.Lexeme = OperatorLexeme;
					Node.LeftNodeIndex = Left;
					Node.RightNodeIndex = Right;
					Left = AddNode(MoveTemp(Node));
					continue;
				}
				break;
			}
			return Left;
		}

		int32 ParseAdditive()
		{
			int32 Left = ParseMultiplicative();
			for (;;)
			{
				if (MatchOperator(TEXT("+")) || MatchOperator(TEXT("-")))
				{
					const FString OperatorLexeme = Tokens[Cursor - 1].Lexeme;
					const int32 OpTokenIndex = Cursor - 1;
					const int32 Right = ParseMultiplicative();
					FVexExpressionAst Node;
					Node.Kind = EVexExprKind::BinaryOp;
					Node.TokenIndex = OpTokenIndex;
					Node.Lexeme = OperatorLexeme;
					Node.LeftNodeIndex = Left;
					Node.RightNodeIndex = Right;
					Left = AddNode(MoveTemp(Node));
					continue;
				}
				break;
			}
			return Left;
		}

		int32 ParseMultiplicative()
		{
			int32 Left = ParseUnary();
			for (;;)
			{
				if (MatchOperator(TEXT("*")) || MatchOperator(TEXT("/")) || MatchOperator(TEXT("%")))
				{
					const FString OperatorLexeme = Tokens[Cursor - 1].Lexeme;
					const int32 OpTokenIndex = Cursor - 1;
					const int32 Right = ParseUnary();
					FVexExpressionAst Node;
					Node.Kind = EVexExprKind::BinaryOp;
					Node.TokenIndex = OpTokenIndex;
					Node.Lexeme = OperatorLexeme;
					Node.LeftNodeIndex = Left;
					Node.RightNodeIndex = Right;
					Left = AddNode(MoveTemp(Node));
					continue;
				}
				break;
			}
			return Left;
		}

		int32 ParseUnary()
		{
			if (MatchOperator(TEXT("!")) || MatchOperator(TEXT("-")) || MatchOperator(TEXT("+")))
			{
				const FString OperatorLexeme = Tokens[Cursor - 1].Lexeme;
				const int32 OpTokenIndex = Cursor - 1;
				const int32 Operand = ParseUnary();
				FVexExpressionAst Node;
				Node.Kind = EVexExprKind::UnaryOp;
				Node.TokenIndex = OpTokenIndex;
				Node.Lexeme = OperatorLexeme;
				Node.LeftNodeIndex = Operand;
				return AddNode(MoveTemp(Node));
			}
			return ParsePostfix();
		}

		int32 ParsePostfix()
		{
			int32 Base = ParsePrimary();
			while (!IsAtEnd())
			{
				if (MatchKind(EVexTokenKind::LParen))
				{
					const int32 CallTokenIndex = Cursor - 1;
					TArray<int32> Arguments;

					if (!MatchKind(EVexTokenKind::RParen))
					{
						while (!IsAtEnd())
						{
							const int32 ArgumentNode = ParseExpression();
							if (ArgumentNode != INDEX_NONE)
							{
								Arguments.Add(ArgumentNode);
							}

							if (MatchKind(EVexTokenKind::Comma))
							{
								continue;
							}

							MatchKind(EVexTokenKind::RParen);
							break;
						}
					}

					FVexExpressionAst CallNode;
					CallNode.Kind = EVexExprKind::FunctionCall;
					CallNode.TokenIndex = CallTokenIndex;
					CallNode.LeftNodeIndex = Base;
					CallNode.ArgumentNodeIndices = MoveTemp(Arguments);
					CallNode.Lexeme = OutNodes.IsValidIndex(Base) ? OutNodes[Base].Lexeme : TEXT("call");
					Base = AddNode(MoveTemp(CallNode));
					continue;
				}

				if (MatchKind(EVexTokenKind::Dot))
				{
					const int32 DotTokenIndex = Cursor - 1;
					const int32 Member = ParsePrimary();
					FVexExpressionAst MemberAccess;
					MemberAccess.Kind = EVexExprKind::BinaryOp;
					MemberAccess.TokenIndex = DotTokenIndex;
					MemberAccess.Lexeme = TEXT(".");
					MemberAccess.LeftNodeIndex = Base;
					MemberAccess.RightNodeIndex = Member;
					Base = AddNode(MoveTemp(MemberAccess));
					continue;
				}

				break;
			}

			return Base;
		}

		int32 ParsePrimary()
		{
			const FVexToken* Token = Peek();
			if (!Token)
			{
				return INDEX_NONE;
			}

			if (Token->Kind == EVexTokenKind::Number || Token->Kind == EVexTokenKind::Symbol || Token->Kind == EVexTokenKind::Identifier)
			{
				FVexExpressionAst Node;
				Node.TokenIndex = Cursor;
				Node.Lexeme = Token->Lexeme;
				Node.Kind = Token->Kind == EVexTokenKind::Number
					? EVexExprKind::NumberLiteral
					: (Token->Kind == EVexTokenKind::Symbol ? EVexExprKind::SymbolRef : EVexExprKind::Identifier);
				++Cursor;
				return AddNode(MoveTemp(Node));
			}

			if (MatchKind(EVexTokenKind::LParen))
			{
				const int32 Inner = ParseExpression();
				MatchKind(EVexTokenKind::RParen);
				return Inner;
			}

			if (MatchKind(EVexTokenKind::LBrace))
			{
				const int32 BraceTokenIndex = Cursor - 1;
				TArray<int32> Components;
				while (!IsAtEnd())
				{
					if (MatchKind(EVexTokenKind::RBrace))
					{
						break;
					}

					const int32 Component = ParseExpression();
					if (Component != INDEX_NONE)
					{
						Components.Add(Component);
					}

					if (MatchKind(EVexTokenKind::Comma))
					{
						continue;
					}

					MatchKind(EVexTokenKind::RBrace);
					break;
				}

				FVexExpressionAst VecNode;
				VecNode.Kind = EVexExprKind::FunctionCall;
				VecNode.TokenIndex = BraceTokenIndex;
				VecNode.Lexeme = FString::Printf(TEXT("vec%d"), Components.Num());
				VecNode.ArgumentNodeIndices = MoveTemp(Components);
				return AddNode(MoveTemp(VecNode));
			}

			++Cursor; // Skip unsupported token to avoid parser stalls.
			return INDEX_NONE;
		}
	};

	int32 BuildExpressionAst(const TArray<FVexToken>& Tokens, const int32 Start, const int32 End, TArray<FVexExpressionAst>& OutNodes, int32* OutCursor = nullptr)
	{
		if (Start > End || !Tokens.IsValidIndex(Start) || !Tokens.IsValidIndex(End))
		{
			if (OutCursor)
			{
				*OutCursor = Start;
			}
			return INDEX_NONE;
		}

		FExpressionParser Parser(Tokens, Start, End, OutNodes);
		const int32 RootIndex = Parser.ParseExpression();
		if (OutCursor)
		{
			*OutCursor = Parser.GetCursor();
		}
		return RootIndex;
	}

	void ValidateExpressionShapes(const TArray<FVexToken>& Tokens, const FVexProgramAst& Program, TArray<FVexIssue>& OutIssues)
	{
		for (const FVexStatementAst& Statement : Program.Statements)
		{
			if (Statement.Kind != EVexStatementKind::Expression
				&& Statement.Kind != EVexStatementKind::Assignment
				&& Statement.Kind != EVexStatementKind::IfHeader)
			{
				continue;
			}

			if (Statement.ExpressionNodeIndex == INDEX_NONE)
			{
				const int32 Line = Tokens.IsValidIndex(Statement.StartTokenIndex) ? Tokens[Statement.StartTokenIndex].Line : 1;
				const int32 Column = Tokens.IsValidIndex(Statement.StartTokenIndex) ? Tokens[Statement.StartTokenIndex].Column : 1;
				AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("Malformed expression."), Line, Column);
				continue;
			}

			TSet<int32> VisitedNodes;
			TArray<int32> NodeStack;
			NodeStack.Add(Statement.ExpressionNodeIndex);
			while (NodeStack.Num() > 0)
			{
				const int32 NodeIndex = NodeStack.Pop(EAllowShrinking::No);
				if (!Program.Expressions.IsValidIndex(NodeIndex) || VisitedNodes.Contains(NodeIndex))
				{
					continue;
				}
				VisitedNodes.Add(NodeIndex);

				const FVexExpressionAst& Node = Program.Expressions[NodeIndex];
				const int32 Line = Tokens.IsValidIndex(Node.TokenIndex) ? Tokens[Node.TokenIndex].Line : 1;
				const int32 Column = Tokens.IsValidIndex(Node.TokenIndex) ? Tokens[Node.TokenIndex].Column : 1;

				if (Node.Kind == EVexExprKind::UnaryOp && Node.LeftNodeIndex == INDEX_NONE)
				{
					AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("Malformed unary expression."), Line, Column);
				}

				if ((Node.Kind == EVexExprKind::BinaryOp || Node.Kind == EVexExprKind::Pipe || Node.Kind == EVexExprKind::PipeIn || Node.Kind == EVexExprKind::PipeOut)
					&& (Node.LeftNodeIndex == INDEX_NONE || Node.RightNodeIndex == INDEX_NONE))
				{
					AddIssue(OutIssues, EVexIssueSeverity::Error, FString::Printf(TEXT("Malformed expression near operator '%s'."), *Node.Lexeme), Line, Column);
				}

				if (Node.LeftNodeIndex != INDEX_NONE) NodeStack.Add(Node.LeftNodeIndex);
				if (Node.RightNodeIndex != INDEX_NONE) NodeStack.Add(Node.RightNodeIndex);
				for (const int32 ArgIndex : Node.ArgumentNodeIndices)
				{
					if (ArgIndex != INDEX_NONE)
					{
						NodeStack.Add(ArgIndex);
					}
				}
			}
		}
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

FVexParseResult ParseAndValidate(const FString& Source, const TArray<FVexSymbolDefinition>& SymbolDefinitions, bool bRequireAllRequiredSymbols)
{
	FVexParseResult Res;
	Res.Program.Tokens = Tokenize(Source, Res.Issues);
	BuildStatementAst(Res.Program.Tokens, Res.Program.Statements, Res.Issues);

	TMap<FString, FVexSymbolDefinition> SymbolByName;
	for (const FVexSymbolDefinition& Definition : SymbolDefinitions)
	{
		SymbolByName.Add(Definition.SymbolName, Definition);
	}

	TSet<FString> KnownSymbols;
	for (const FVexSymbolDefinition& Definition : SymbolDefinitions)
	{
		KnownSymbols.Add(Definition.SymbolName);
	}

	for (FVexStatementAst& Statement : Res.Program.Statements)
	{
		if (Statement.Kind != EVexStatementKind::Expression
			&& Statement.Kind != EVexStatementKind::Assignment
			&& Statement.Kind != EVexStatementKind::IfHeader)
		{
			continue;
		}

		int32 ExprStart = Statement.StartTokenIndex;
		int32 ExprEnd = Statement.EndTokenIndex;

		if (Statement.Kind == EVexStatementKind::Assignment)
		{
			for (int32 TokenIndex = Statement.StartTokenIndex; TokenIndex <= Statement.EndTokenIndex; ++TokenIndex)
			{
				if (Res.Program.Tokens[TokenIndex].Kind == EVexTokenKind::Operator
					&& IsAssignmentOperator(Res.Program.Tokens[TokenIndex].Lexeme))
				{
					ExprStart = TokenIndex + 1;
					break;
				}
			}
		}
		else if (Statement.Kind == EVexStatementKind::IfHeader)
		{
			int32 LeftParen = INDEX_NONE;
			int32 RightParen = INDEX_NONE;
			for (int32 TokenIndex = Statement.StartTokenIndex; TokenIndex <= Statement.EndTokenIndex; ++TokenIndex)
			{
				if (Res.Program.Tokens[TokenIndex].Kind == EVexTokenKind::LParen && LeftParen == INDEX_NONE)
				{
					LeftParen = TokenIndex;
				}
				else if (Res.Program.Tokens[TokenIndex].Kind == EVexTokenKind::RParen)
				{
					RightParen = TokenIndex;
				}
			}

			if (LeftParen != INDEX_NONE && RightParen != INDEX_NONE && RightParen > LeftParen)
			{
				ExprStart = LeftParen + 1;
				ExprEnd = RightParen - 1;
			}
		}

		int32 ParsedCursor = ExprStart;
		Statement.ExpressionNodeIndex = BuildExpressionAst(Res.Program.Tokens, ExprStart, ExprEnd, Res.Program.Expressions, &ParsedCursor);
		if (ParsedCursor <= ExprEnd && Res.Program.Tokens.IsValidIndex(ParsedCursor))
		{
			const FVexToken& Unexpected = Res.Program.Tokens[ParsedCursor];
			AddIssue(Res.Issues, EVexIssueSeverity::Error, FString::Printf(TEXT("Unexpected token '%s' in expression."), *Unexpected.Lexeme), Unexpected.Line, Unexpected.Column);
		}
	}

	ValidateExpressionShapes(Res.Program.Tokens, Res.Program, Res.Issues);

	TSet<FString> UsedSymbolsSet;
	for (const FVexToken& Token : Res.Program.Tokens)
	{
		if (Token.Kind == EVexTokenKind::Symbol)
		{
			UsedSymbolsSet.Add(Token.Lexeme);
		}
	}

	Res.Program.UsedSymbols = UsedSymbolsSet.Array();
	Res.Program.UsedSymbols.Sort();

	for (const FString& UsedSymbol : Res.Program.UsedSymbols)
	{
		if (KnownSymbols.Num() == 0 || KnownSymbols.Contains(UsedSymbol))
		{
			continue;
		}

		Res.UnknownSymbols.Add(UsedSymbol);

		int32 Line = 1;
		int32 Column = 1;
		for (const FVexToken& Token : Res.Program.Tokens)
		{
			if (Token.Kind == EVexTokenKind::Symbol && Token.Lexeme == UsedSymbol)
			{
				Line = Token.Line;
				Column = Token.Column;
				break;
			}
		}

		AddIssue(Res.Issues, EVexIssueSeverity::Warning, FString::Printf(TEXT("Unknown symbol '%s'."), *UsedSymbol), Line, Column);
	}

	if (bRequireAllRequiredSymbols)
	{
		for (const FVexSymbolDefinition& Definition : SymbolDefinitions)
		{
			if (Definition.bRequired && !UsedSymbolsSet.Contains(Definition.SymbolName))
			{
				Res.MissingRequiredSymbols.Add(Definition.SymbolName);
			}
		}

		Res.MissingRequiredSymbols.Sort();
		if (Res.MissingRequiredSymbols.Num() > 0)
		{
			AddIssue(Res.Issues, EVexIssueSeverity::Error,
				FString::Printf(TEXT("Missing required symbols: %s"), *FString::Join(Res.MissingRequiredSymbols, TEXT(", "))),
				1, 1);
		}
	}

	PerformSemanticValidation(Res.Program.Tokens, Res.Program, SymbolDefinitions, Res.Issues);
	Res.Requirements = AnalyzeRequirements(Res.Program);

	// Apply m2-inspired algebraic optimizations.
	FVexOptimizer::Optimize(Res.Program);

	const TMap<FString, EVexValueType> LocalTypes = BuildLocalVariableTypeMap(Res.Program);
	AnnotateInferredTypes(Res.Program, SymbolByName, LocalTypes);

	Res.bSuccess = true;
	for (const FVexIssue& Issue : Res.Issues)
	{
		if (Issue.Severity == EVexIssueSeverity::Error)
		{
			Res.bSuccess = false;
		}
	}
	return Res;
}

} // namespace Flight::Vex
