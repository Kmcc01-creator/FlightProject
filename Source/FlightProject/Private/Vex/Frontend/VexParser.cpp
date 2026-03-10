// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/Frontend/VexParser.h"
#include "Vex/Frontend/VexLexer.h"
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
		FVexIssue Issue; Issue.Severity = Severity; Issue.Message = Message; Issue.Line = Line; Issue.Column = Column; Issues.Add(Issue);
	}

	FString BuildSpanText(const TArray<FVexToken>& Tokens, int32 Start, int32 End) {
		FString S; for (int32 i = Start; i <= End; ++i) { if (!S.IsEmpty()) S += TEXT(" "); S += Tokens[i].Lexeme; } return S;
	}

	bool IsAssignmentOperator(const FString& Op) { return Op == TEXT("=") || Op == TEXT("+=") || Op == TEXT("-=") || Op == TEXT("*=") || Op == TEXT("/="); }

	void BuildStatementAst(const TArray<FVexToken>& Tokens, TArray<FVexStatementAst>& OutStatements, TArray<FVexIssue>& OutIssues)
	{
		int32 StatementStart = INDEX_NONE; int32 ParenDepth = 0; int32 BraceDepth = 0;
		const auto EmitStatement = [&](const int32 EndIdx) {
			if (StatementStart == INDEX_NONE || EndIdx < StatementStart) return;
			FVexStatementAst S; S.StartTokenIndex = StatementStart; S.EndTokenIndex = EndIdx; S.SourceSpan = BuildSpanText(Tokens, StatementStart, EndIdx); S.Kind = EVexStatementKind::Expression;
			for (int32 i = StatementStart; i <= EndIdx; ++i) if (Tokens[i].Kind == EVexTokenKind::Operator && IsAssignmentOperator(Tokens[i].Lexeme)) { S.Kind = EVexStatementKind::Assignment; break; }
			OutStatements.Add(MoveTemp(S));
		};
		for (int32 i = 0; i < Tokens.Num(); ++i) {
			const auto& T = Tokens[i]; if (T.Kind == EVexTokenKind::EndOfFile) break;
			if (T.Kind == EVexTokenKind::TargetDirective && ParenDepth == 0 && BraceDepth == 0) {
				if (StatementStart != INDEX_NONE) { EmitStatement(i - 1); StatementStart = INDEX_NONE; }
				FVexStatementAst D; D.Kind = EVexStatementKind::TargetDirective; D.StartTokenIndex = i; D.EndTokenIndex = i;
				if (T.Lexeme == TEXT("@rate") && i + 1 < Tokens.Num() && Tokens[i + 1].Kind == EVexTokenKind::LParen) {
					int32 Dp = 0; for (int32 c = i + 1; c < Tokens.Num(); ++c) { if (Tokens[c].Kind == EVexTokenKind::LParen) ++Dp; else if (Tokens[c].Kind == EVexTokenKind::RParen) { --Dp; if (Dp == 0) { D.EndTokenIndex = c; i = c; break; } } }
				}
				D.SourceSpan = BuildSpanText(Tokens, D.StartTokenIndex, D.EndTokenIndex); OutStatements.Add(MoveTemp(D)); continue;
			}
			if (T.Kind == EVexTokenKind::LBrace && StatementStart == INDEX_NONE && ParenDepth == 0 && BraceDepth == 0) {
				FVexStatementAst B; B.Kind = EVexStatementKind::BlockOpen; B.StartTokenIndex = i; B.EndTokenIndex = i; B.SourceSpan = TEXT("{"); OutStatements.Add(MoveTemp(B));
				continue;
			}
			if (T.Kind == EVexTokenKind::KeywordIf && StatementStart == INDEX_NONE && ParenDepth == 0 && BraceDepth == 0) {
				if (i + 1 >= Tokens.Num() || Tokens[i + 1].Kind != EVexTokenKind::LParen) { AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("`if` must be followed by '('."), T.Line, T.Column); continue; }
				int32 Dp = 0; for (int32 c = i + 1; c < Tokens.Num(); ++c) { if (Tokens[c].Kind == EVexTokenKind::LParen) ++Dp; else if (Tokens[c].Kind == EVexTokenKind::RParen) { --Dp; if (Dp == 0) { FVexStatementAst H; H.Kind = EVexStatementKind::IfHeader; H.StartTokenIndex = i; H.EndTokenIndex = c; H.SourceSpan = BuildSpanText(Tokens, i, c); OutStatements.Add(MoveTemp(H)); i = c; break; } } }
				continue;
			}
			if (T.Kind == EVexTokenKind::KeywordElse && StatementStart == INDEX_NONE && ParenDepth == 0 && BraceDepth == 0) {
				FVexStatementAst E; E.Kind = EVexStatementKind::Else; E.StartTokenIndex = i; E.EndTokenIndex = i; E.SourceSpan = TEXT("else"); OutStatements.Add(MoveTemp(E));
				continue;
			}
			if (T.Kind == EVexTokenKind::KeywordWhile && StatementStart == INDEX_NONE && ParenDepth == 0 && BraceDepth == 0) {
				if (i + 1 >= Tokens.Num() || Tokens[i + 1].Kind != EVexTokenKind::LParen) { AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("`while` must be followed by '('."), T.Line, T.Column); continue; }
				int32 Dp = 0; for (int32 c = i + 1; c < Tokens.Num(); ++c) { if (Tokens[c].Kind == EVexTokenKind::LParen) ++Dp; else if (Tokens[c].Kind == EVexTokenKind::RParen) { --Dp; if (Dp == 0) { FVexStatementAst H; H.Kind = EVexStatementKind::WhileHeader; H.StartTokenIndex = i; H.EndTokenIndex = c; H.SourceSpan = BuildSpanText(Tokens, i, c); OutStatements.Add(MoveTemp(H)); i = c; break; } } }
				continue;
			}
			if (T.Kind == EVexTokenKind::KeywordFor && StatementStart == INDEX_NONE && ParenDepth == 0 && BraceDepth == 0) {
				if (i + 1 >= Tokens.Num() || Tokens[i + 1].Kind != EVexTokenKind::LParen) { AddIssue(OutIssues, EVexIssueSeverity::Error, TEXT("`for` must be followed by '('."), T.Line, T.Column); continue; }
				int32 Dp = 0; for (int32 c = i + 1; c < Tokens.Num(); ++c) { if (Tokens[c].Kind == EVexTokenKind::LParen) ++Dp; else if (Tokens[c].Kind == EVexTokenKind::RParen) { --Dp; if (Dp == 0) { FVexStatementAst H; H.Kind = EVexStatementKind::ForHeader; H.StartTokenIndex = i; H.EndTokenIndex = c; H.SourceSpan = BuildSpanText(Tokens, i, c); OutStatements.Add(MoveTemp(H)); i = c; break; } } }
				continue;
			}
			if (T.Kind == EVexTokenKind::LParen) { if (StatementStart == INDEX_NONE) StatementStart = i; ++ParenDepth; continue; }
			if (T.Kind == EVexTokenKind::RParen) { if (ParenDepth > 0) --ParenDepth; continue; }
			if (T.Kind == EVexTokenKind::LBrace) {
				if (StatementStart == INDEX_NONE && ParenDepth == 0 && BraceDepth == 0) {
					// Top-level block (e.g. after a directive), already handled above
					continue;
				}
				if (StatementStart == INDEX_NONE) StatementStart = i; ++BraceDepth;
				continue;
			}
			if (T.Kind == EVexTokenKind::RBrace) {
				if (BraceDepth > 0) { --BraceDepth; continue; }
				if (StatementStart != INDEX_NONE) { EmitStatement(i - 1); StatementStart = INDEX_NONE; }
				FVexStatementAst B; B.Kind = EVexStatementKind::BlockClose; B.StartTokenIndex = i; B.EndTokenIndex = i; B.SourceSpan = TEXT("}"); OutStatements.Add(MoveTemp(B)); continue;
			}
			if (T.Kind == EVexTokenKind::Semicolon && ParenDepth == 0 && BraceDepth == 0) { EmitStatement(i - 1); StatementStart = INDEX_NONE; continue; }
			if (StatementStart == INDEX_NONE) StatementStart = i;
		}
		if (StatementStart != INDEX_NONE) EmitStatement(Tokens.Num() - 2);
	}

	bool AreTypesCompatibleForAssignment(const EVexValueType LeftType, const EVexValueType RightType, const FString& Op) { (void)Op; if (LeftType == EVexValueType::Unknown || RightType == EVexValueType::Unknown) return true; return LeftType == RightType; }

	EVexValueType InferExpressionTypeFromAst(int32 Idx, const TArray<FVexExpressionAst>& Exprs, const TMap<FString, FVexSymbolDefinition>& Syms, const TMap<FString, EVexValueType>& Locs)
	{
		if (Idx == INDEX_NONE || !Exprs.IsValidIndex(Idx)) return EVexValueType::Unknown;
		const auto& N = Exprs[Idx];
		switch (N.Kind) {
		case EVexExprKind::NumberLiteral: return N.Lexeme.Contains(TEXT(".")) ? EVexValueType::Float : EVexValueType::Int;
		case EVexExprKind::SymbolRef: if (const auto* D = Syms.Find(N.Lexeme)) return ParseValueType(D->ValueType); return EVexValueType::Unknown;
		case EVexExprKind::Identifier: if (const auto* T = Locs.Find(N.Lexeme)) return *T; return EVexValueType::Unknown;
		case EVexExprKind::UnaryOp: if (N.Lexeme == TEXT("!")) return EVexValueType::Bool; return InferExpressionTypeFromAst(N.LeftNodeIndex, Exprs, Syms, Locs);
		case EVexExprKind::BinaryOp: {
			const auto L = InferExpressionTypeFromAst(N.LeftNodeIndex, Exprs, Syms, Locs); const auto R = InferExpressionTypeFromAst(N.RightNodeIndex, Exprs, Syms, Locs);
			if (N.Lexeme == TEXT(".")) { if (IsVectorType(L) && Exprs.IsValidIndex(N.RightNodeIndex)) { const int32 W = Exprs[N.RightNodeIndex].Lexeme.Len(); if (W <= 1) return EVexValueType::Float; if (W == 2) return EVexValueType::Float2; if (W == 3) return EVexValueType::Float3; if (W == 4) return EVexValueType::Float4; } return EVexValueType::Unknown; }
			if (N.Lexeme == TEXT("<") || N.Lexeme == TEXT(">") || N.Lexeme == TEXT("<=") || N.Lexeme == TEXT(">=") || N.Lexeme == TEXT("==") || N.Lexeme == TEXT("!=") || N.Lexeme == TEXT("&&") || N.Lexeme == TEXT("||")) return EVexValueType::Bool; 
			if (L == R) return L; if (IsVectorType(L) && (R == EVexValueType::Float || R == EVexValueType::Int)) return L; if (IsVectorType(R) && (L == EVexValueType::Float || L == EVexValueType::Int)) return R; if ((L == EVexValueType::Int && R == EVexValueType::Float) || (L == EVexValueType::Float && R == EVexValueType::Int)) return EVexValueType::Float; return L != EVexValueType::Unknown ? L : R;
		}
		case EVexExprKind::FunctionCall: {
			const auto& Nm = N.Lexeme;
			if (Nm == TEXT("normalize")) return N.ArgumentNodeIndices.Num() > 0 ? InferExpressionTypeFromAst(N.ArgumentNodeIndices[0], Exprs, Syms, Locs) : EVexValueType::Unknown;
			if (Nm == TEXT("curlnoise") || Nm == TEXT("gradient") || Nm == TEXT("sample_lattice")) return EVexValueType::Float3;
			if (Nm == TEXT("dot") || Nm == TEXT("sample_sdf") || Nm == TEXT("sample_cloud") || Nm == TEXT("smoothstep")) return EVexValueType::Float;
			if (Nm == TEXT("clamp")) return N.ArgumentNodeIndices.Num() > 0 ? InferExpressionTypeFromAst(N.ArgumentNodeIndices[0], Exprs, Syms, Locs) : EVexValueType::Unknown;
			return EVexValueType::Unknown;
		}
		case EVexExprKind::Pipe: case EVexExprKind::PipeIn: case EVexExprKind::PipeOut: {
			const auto P = InferExpressionTypeFromAst(N.LeftNodeIndex, Exprs, Syms, Locs);
			if (Exprs.IsValidIndex(N.RightNodeIndex)) {
				const auto& R = Exprs[N.RightNodeIndex];
				if (R.Kind == EVexExprKind::FunctionCall) {
					if (R.Lexeme == TEXT("sample_sdf") || R.Lexeme == TEXT("smoothstep")) return EVexValueType::Float;
					if (R.Lexeme == TEXT("gradient")) return EVexValueType::Float3;
					if (R.Lexeme == TEXT("count_hits")) return EVexValueType::Int;
					if (R.Lexeme == TEXT("clamp") || R.Lexeme == TEXT("normalize")) return P;
				}
				return InferExpressionTypeFromAst(N.RightNodeIndex, Exprs, Syms, Locs);
			}
			return P;
		}
		default: return EVexValueType::Unknown;
		}
	}

	void AnnotateInferredTypes(FVexProgramAst& Program, const TMap<FString, FVexSymbolDefinition>& Syms, const TMap<FString, EVexValueType>& Locs) { for (int32 i = 0; i < Program.Expressions.Num(); ++i) Program.Expressions[i].InferredType = ToTypeString(InferExpressionTypeFromAst(i, Program.Expressions, Syms, Locs)); }

	TMap<FString, EVexValueType> BuildLocalVariableTypeMap(const FVexProgramAst& Program) {
		TMap<FString, EVexValueType> Locs;
		for (const auto& S : Program.Statements) {
			if (S.Kind != EVexStatementKind::Assignment || !Program.Tokens.IsValidIndex(S.StartTokenIndex) || !Program.Tokens.IsValidIndex(S.StartTokenIndex + 1)) continue;
			const auto& T1 = Program.Tokens[S.StartTokenIndex]; const auto& T2 = Program.Tokens[S.StartTokenIndex + 1];
			if (T1.Kind == EVexTokenKind::Identifier && T2.Kind == EVexTokenKind::Identifier && IsDeclarationTypeToken(T1.Lexeme)) Locs.Add(T2.Lexeme, ParseValueType(T1.Lexeme));
		}
		return Locs;
	}

	void PerformSemanticValidation(const TArray<FVexToken>& Tokens, FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefs, TArray<FVexIssue>& Issues)
	{
		TMap<FString, FVexSymbolDefinition> Syms; for (const auto& D : SymbolDefs) Syms.Add(D.SymbolName, D);
		TMap<FString, EVexValueType> Locs; auto Context = EFlightVexSymbolResidency::Shared; auto Affinity = EFlightVexSymbolAffinity::Any; FVexTaskDescriptor* Task = nullptr;
		for (const auto& S : Program.Statements) {
			if (S.Kind == EVexStatementKind::TargetDirective) {
				if (S.SourceSpan == TEXT("@cpu")) Context = EFlightVexSymbolResidency::CpuOnly;
				else if (S.SourceSpan == TEXT("@gpu")) Context = EFlightVexSymbolResidency::GpuOnly;
				else if (S.SourceSpan == TEXT("@job") || S.SourceSpan == TEXT("@thread") || S.SourceSpan == TEXT("@async")) {
					FVexTaskDescriptor T; T.Type = S.SourceSpan; T.StartTokenIndex = S.StartTokenIndex;
					Affinity = (S.SourceSpan == TEXT("@async")) ? EFlightVexSymbolAffinity::Any : EFlightVexSymbolAffinity::WorkerThread;
					Program.Tasks.Add(MoveTemp(T)); Task = &Program.Tasks.Last();
				}
				else if (S.SourceSpan.StartsWith(TEXT("@rate"))) {
					FString A = S.SourceSpan; int32 LP, RP; if (A.FindChar(TEXT('('), LP) && A.FindChar(TEXT(')'), RP) && RP > LP + 1) {
						FString I = A.Mid(LP + 1, RP - LP - 1).TrimStartAndEnd();
						if (I.EndsWith(TEXT("Hz"), ESearchCase::IgnoreCase)) { Program.ExecutionRateHz = FCString::Atof(*I.LeftChop(2)); Program.FrameInterval = 0; }
						else { Program.FrameInterval = FCString::Atoi(*I); Program.ExecutionRateHz = 0.0f; }
					}
				}
				continue;
			}
			if (S.Kind == EVexStatementKind::BlockClose) { if (Task) { Task->EndTokenIndex = S.EndTokenIndex; Task = nullptr; Affinity = EFlightVexSymbolAffinity::Any; } }
			if (S.ExpressionNodeIndex != INDEX_NONE) {
				TArray<int32> Stk; Stk.Add(S.ExpressionNodeIndex);
				while (Stk.Num() > 0) {
					const int32 NodeIndex = Stk.Pop(EAllowShrinking::No); if (!Program.Expressions.IsValidIndex(NodeIndex)) continue;
					const auto& N = Program.Expressions[NodeIndex];
					if (N.Kind == EVexExprKind::PipeIn) { if (Context != EFlightVexSymbolResidency::GpuOnly) AddIssue(Issues, EVexIssueSeverity::Error, TEXT("Inject pipe `<|` can only be used within a `@gpu` context."), Tokens.IsValidIndex(N.TokenIndex) ? Tokens[N.TokenIndex].Line : 1, Tokens.IsValidIndex(N.TokenIndex) ? Tokens[N.TokenIndex].Column : 1); }
					else if (N.Kind == EVexExprKind::PipeOut) { if (Context != EFlightVexSymbolResidency::CpuOnly) AddIssue(Issues, EVexIssueSeverity::Error, TEXT("Extract pipe `|>` can only be used within a `@cpu` context."), Tokens.IsValidIndex(N.TokenIndex) ? Tokens[N.TokenIndex].Line : 1, Tokens.IsValidIndex(N.TokenIndex) ? Tokens[N.TokenIndex].Column : 1); }
					if (N.LeftNodeIndex != INDEX_NONE) Stk.Add(N.LeftNodeIndex); if (N.RightNodeIndex != INDEX_NONE) Stk.Add(N.RightNodeIndex); for (const int32 A : N.ArgumentNodeIndices) if (A != INDEX_NONE) Stk.Add(A);
				}
			}
			if (S.StartTokenIndex == INDEX_NONE || S.EndTokenIndex == INDEX_NONE) continue;
			for (int32 i = S.StartTokenIndex; i <= S.EndTokenIndex; ++i) {
				if (Tokens[i].Kind == EVexTokenKind::Symbol) {
					if (const auto* D = Syms.Find(Tokens[i].Lexeme))
					{
						if (Task) { bool W = (S.Kind == EVexStatementKind::Assignment && i == S.StartTokenIndex); if (W) { Task->WriteSymbols.Add(D->SymbolName); if (!D->bWritable) AddIssue(Issues, EVexIssueSeverity::Error, FString::Printf(TEXT("read-only symbol '%s'"), *D->SymbolName), Tokens[i].Line, Tokens[i].Column); } else Task->ReadSymbols.Add(D->SymbolName); }
						if (D->Residency != EFlightVexSymbolResidency::Shared && Context != EFlightVexSymbolResidency::Shared) if (D->Residency != Context) AddIssue(Issues, EVexIssueSeverity::Error, FString::Printf(TEXT("Symbol '%s' residency error: cannot be used in %s context."), *D->SymbolName, (Context == EFlightVexSymbolResidency::CpuOnly ? TEXT("@cpu") : TEXT("@gpu"))), Tokens[i].Line, Tokens[i].Column);
						if (D->Affinity == EFlightVexSymbolAffinity::GameThread && Affinity == EFlightVexSymbolAffinity::WorkerThread) AddIssue(Issues, EVexIssueSeverity::Error, FString::Printf(TEXT("Symbol '%s' affinity error: Game Thread symbols cannot be used in a @job context."), *D->SymbolName), Tokens[i].Line, Tokens[i].Column);
						if (Context == EFlightVexSymbolResidency::GpuOnly && !D->bGpuTier1Allowed) AddIssue(Issues, EVexIssueSeverity::Warning, FString::Printf(TEXT("Symbol '%s' is not officially supported in GPU Tier 1 kernels."), *D->SymbolName), Tokens[i].Line, Tokens[i].Column);
						bool W = (S.Kind == EVexStatementKind::Assignment && i == S.StartTokenIndex);
						if (W) { if (!D->bSimdWriteAllowed) AddIssue(Issues, EVexIssueSeverity::Warning, FString::Printf(TEXT("Symbol '%s' does not allow SIMD-accelerated writes; may trigger scalar fallback."), *D->SymbolName), Tokens[i].Line, Tokens[i].Column); } 
						else { if (!D->bSimdReadAllowed) AddIssue(Issues, EVexIssueSeverity::Warning, FString::Printf(TEXT("Symbol '%s' does not allow SIMD-accelerated reads; may trigger scalar fallback."), *D->SymbolName), Tokens[i].Line, Tokens[i].Column); }
					}
				}
			}
			if (S.Kind == EVexStatementKind::Assignment || S.Kind == EVexStatementKind::Expression) {
				int32 OpIdx = INDEX_NONE; for (int32 i = S.StartTokenIndex; i <= S.EndTokenIndex; ++i) { if (Tokens[i].Kind == EVexTokenKind::Operator && IsAssignmentOperator(Tokens[i].Lexeme)) { OpIdx = i; break; } }
				if (S.Kind == EVexStatementKind::Assignment && OpIdx != INDEX_NONE) {
					const auto& LTok = Tokens[S.StartTokenIndex]; const auto& OpLex = Tokens[OpIdx].Lexeme;
					if (LTok.Kind == EVexTokenKind::Identifier && IsDeclarationTypeToken(LTok.Lexeme)) {
						const int32 NameIdx = S.StartTokenIndex + 1; if (NameIdx < OpIdx && Tokens[NameIdx].Kind == EVexTokenKind::Identifier) { const auto DT = ParseValueType(LTok.Lexeme); const auto& VN = Tokens[NameIdx].Lexeme; Locs.Add(VN, DT); auto RT = (S.ExpressionNodeIndex != INDEX_NONE) ? InferExpressionTypeFromAst(S.ExpressionNodeIndex, Program.Expressions, Syms, Locs) : EVexValueType::Unknown; if (!AreTypesCompatibleForAssignment(DT, RT, OpLex)) AddIssue(Issues, EVexIssueSeverity::Error, TEXT("Type mismatch for assignment"), Tokens[NameIdx].Line, Tokens[NameIdx].Column); }
					}
					else {
						auto LT = EVexValueType::Unknown; const auto& LN = LTok.Lexeme;
						if (LTok.Kind == EVexTokenKind::Symbol) { if (const auto* D = Syms.Find(LTok.Lexeme)) { LT = ParseValueType(D->ValueType); if (!D->bWritable) AddIssue(Issues, EVexIssueSeverity::Error, FString::Printf(TEXT("read-only symbol '%s'"), *LTok.Lexeme), LTok.Line, LTok.Column); } } else if (LTok.Kind == EVexTokenKind::Identifier) { if (const auto* T = Locs.Find(LTok.Lexeme)) LT = *T; }
						auto RT = (S.ExpressionNodeIndex != INDEX_NONE) ? InferExpressionTypeFromAst(S.ExpressionNodeIndex, Program.Expressions, Syms, Locs) : EVexValueType::Unknown;
						if (!AreTypesCompatibleForAssignment(LT, RT, OpLex)) AddIssue(Issues, EVexIssueSeverity::Error, TEXT("Type mismatch for assignment"), LTok.Line, LTok.Column);
					}
				}
			}
		}
		for (const auto& S : Program.Statements) if ((S.Kind == EVexStatementKind::IfHeader || S.Kind == EVexStatementKind::WhileHeader) && S.ExpressionNodeIndex != INDEX_NONE) if (InferExpressionTypeFromAst(S.ExpressionNodeIndex, Program.Expressions, Syms, Locs) != EVexValueType::Bool) AddIssue(Issues, EVexIssueSeverity::Error, TEXT("condition must be bool-like"), Tokens.IsValidIndex(S.StartTokenIndex) ? Tokens[S.StartTokenIndex].Line : 1, Tokens.IsValidIndex(S.StartTokenIndex) ? Tokens[S.StartTokenIndex].Column : 1);
		AnnotateInferredTypes(Program, Syms, Locs);
	}

	class FExpressionParser
	{
	public:
		FExpressionParser(const TArray<FVexToken>& InT, int32 InS, int32 InE, TArray<FVexExpressionAst>& InO) : T(InT), C(InS), E(InE), O(InO) {}
		int32 Parse() { return ParsePipe(); }
		int32 GetC() const { return C; }
	private:
		const TArray<FVexToken>& T; int32 C, E; TArray<FVexExpressionAst>& O;
		bool AtEnd() const { return C > E; }
		const FVexToken* Pk(int32 Off = 0) const { int32 Idx = C + Off; return (Idx >= 0 && Idx <= E && T.IsValidIndex(Idx)) ? &T[Idx] : nullptr; }
		bool MKind(EVexTokenKind K) { const auto* P = Pk(); if (!P || P->Kind != K) return false; ++C; return true; }
		bool MOp(const FString& Op) { const auto* P = Pk(); if (!P || P->Kind != EVexTokenKind::Operator || P->Lexeme != Op) return false; ++C; return true; }
		int32 Add(FVexExpressionAst&& N) { return O.Add(MoveTemp(N)); }
		int32 ParsePipe() {
			int32 L = ParseOr(); for (;;) {
				const auto* P = Pk(); if (P && P->Kind == EVexTokenKind::Operator && (P->Lexeme == TEXT("|") || P->Lexeme == TEXT("<|") || P->Lexeme == TEXT("|>"))) {
					const auto Op = P->Lexeme; const auto Idx = C; ++C; int32 R = ParseOr(); FVexExpressionAst N; if (Op == TEXT("<|")) N.Kind = EVexExprKind::PipeIn; else if (Op == TEXT("|>")) N.Kind = EVexExprKind::PipeOut; else N.Kind = EVexExprKind::Pipe;
					N.TokenIndex = Idx; N.Lexeme = Op; N.LeftNodeIndex = L; N.RightNodeIndex = R; if (O.IsValidIndex(R) && (O[R].Kind == EVexExprKind::FunctionCall || O[R].Kind == EVexExprKind::Identifier)) N.Lexeme = O[R].Lexeme;
					L = Add(MoveTemp(N)); continue;
				}
				break;
			}
			return L;
		}
		int32 ParseOr() { int32 L = ParseAnd(); for (;;) { if (MOp(TEXT("||"))) { int32 Idx = C - 1; int32 R = ParseAnd(); FVexExpressionAst N; N.Kind = EVexExprKind::BinaryOp; N.TokenIndex = Idx; N.Lexeme = TEXT("||"); N.LeftNodeIndex = L; N.RightNodeIndex = R; L = Add(MoveTemp(N)); continue; } break; } return L; }
		int32 ParseAnd() { int32 L = ParseEq(); for (;;) { if (MOp(TEXT("&&"))) { int32 Idx = C - 1; int32 R = ParseEq(); FVexExpressionAst N; N.Kind = EVexExprKind::BinaryOp; N.TokenIndex = Idx; N.Lexeme = TEXT("&&"); N.LeftNodeIndex = L; N.RightNodeIndex = R; L = Add(MoveTemp(N)); continue; } break; } return L; }
		int32 ParseEq() { int32 L = ParseCmp(); for (;;) { if (MOp(TEXT("==")) || MOp(TEXT("!="))) { auto Op = T[C-1].Lexeme; int32 Idx = C-1; int32 R = ParseCmp(); FVexExpressionAst N; N.Kind = EVexExprKind::BinaryOp; N.TokenIndex = Idx; N.Lexeme = Op; N.LeftNodeIndex = L; N.RightNodeIndex = R; L = Add(MoveTemp(N)); continue; } break; } return L; }
		int32 ParseCmp() { int32 L = ParseAdd(); for (;;) { if (MOp(TEXT("<=")) || MOp(TEXT(">=")) || MOp(TEXT("<")) || MOp(TEXT(">"))) { auto Op = T[C-1].Lexeme; int32 Idx = C-1; int32 R = ParseAdd(); FVexExpressionAst N; N.Kind = EVexExprKind::BinaryOp; N.TokenIndex = Idx; N.Lexeme = Op; N.LeftNodeIndex = L; N.RightNodeIndex = R; L = Add(MoveTemp(N)); continue; } break; } return L; }
		int32 ParseAdd() { int32 L = ParseMul(); for (;;) { if (MOp(TEXT("+")) || MOp(TEXT("-"))) { auto Op = T[C-1].Lexeme; int32 Idx = C-1; int32 R = ParseMul(); FVexExpressionAst N; N.Kind = EVexExprKind::BinaryOp; N.TokenIndex = Idx; N.Lexeme = Op; N.LeftNodeIndex = L; N.RightNodeIndex = R; L = Add(MoveTemp(N)); continue; } break; } return L; }
		int32 ParseMul() { int32 L = ParseUn(); for (;;) { if (MOp(TEXT("*")) || MOp(TEXT("/")) || MOp(TEXT("%"))) { auto Op = T[C-1].Lexeme; int32 Idx = C-1; int32 R = ParseUn(); FVexExpressionAst N; N.Kind = EVexExprKind::BinaryOp; N.TokenIndex = Idx; N.Lexeme = Op; N.LeftNodeIndex = L; N.RightNodeIndex = R; L = Add(MoveTemp(N)); continue; } break; } return L; }
		int32 ParseUn() { if (MOp(TEXT("!")) || MOp(TEXT("-")) || MOp(TEXT("+"))) { auto Op = T[C-1].Lexeme; int32 Idx = C-1; int32 R = ParseUn(); FVexExpressionAst N; N.Kind = EVexExprKind::UnaryOp; N.TokenIndex = Idx; N.Lexeme = Op; N.LeftNodeIndex = R; return Add(MoveTemp(N)); } return ParsePost(); }
		int32 ParsePost() {
			int32 B = ParsePrim(); while (!AtEnd()) {
				if (MKind(EVexTokenKind::LParen)) { int32 Idx = C - 1; TArray<int32> Args; if (!MKind(EVexTokenKind::RParen)) { while (!AtEnd()) { int32 A = Parse(); if (A != INDEX_NONE) Args.Add(A); if (MKind(EVexTokenKind::Comma)) continue; MKind(EVexTokenKind::RParen); break; } } FVexExpressionAst N; N.Kind = EVexExprKind::FunctionCall; N.TokenIndex = Idx; N.LeftNodeIndex = B; N.ArgumentNodeIndices = MoveTemp(Args); N.Lexeme = O.IsValidIndex(B) ? O[B].Lexeme : TEXT("call"); B = Add(MoveTemp(N)); continue; }
				if (MKind(EVexTokenKind::Dot)) { int32 Idx = C - 1; int32 M = ParsePrim(); FVexExpressionAst N; N.Kind = EVexExprKind::BinaryOp; N.TokenIndex = Idx; N.Lexeme = TEXT("."); N.LeftNodeIndex = B; N.RightNodeIndex = M; B = Add(MoveTemp(N)); continue; } break;
			} return B;
		}
		int32 ParsePrim() {
			const auto* P = Pk(); if (!P) return INDEX_NONE;
			if (P->Kind == EVexTokenKind::Number || P->Kind == EVexTokenKind::Symbol || P->Kind == EVexTokenKind::Identifier) { FVexExpressionAst N; N.TokenIndex = C; N.Lexeme = P->Lexeme; N.Kind = P->Kind == EVexTokenKind::Number ? EVexExprKind::NumberLiteral : (P->Kind == EVexTokenKind::Symbol ? EVexExprKind::SymbolRef : EVexExprKind::Identifier); ++C; return Add(MoveTemp(N)); }
			if (MKind(EVexTokenKind::LParen)) { int32 I = Parse(); MKind(EVexTokenKind::RParen); return I; }
			if (MKind(EVexTokenKind::LBrace)) { int32 Idx = C - 1; TArray<int32> Comps; while (!AtEnd()) { if (MKind(EVexTokenKind::RBrace)) break; int32 Co = Parse(); if (Co != INDEX_NONE) Comps.Add(Co); if (MKind(EVexTokenKind::Comma)) continue; MKind(EVexTokenKind::RBrace); break; } FVexExpressionAst N; N.Kind = EVexExprKind::FunctionCall; N.TokenIndex = Idx; N.Lexeme = FString::Printf(TEXT("vec%d"), Comps.Num()); N.ArgumentNodeIndices = MoveTemp(Comps); return Add(MoveTemp(N)); }
			++C; return INDEX_NONE;
		}
	};

	int32 BuildExpressionAst(const TArray<FVexToken>& T, int32 S, int32 E, TArray<FVexExpressionAst>& O, int32* C = nullptr) {
		if (S > E || !T.IsValidIndex(S) || !T.IsValidIndex(E)) { if (C) *C = S; return INDEX_NONE; }
		FExpressionParser P(T, S, E, O);
		int32 R = P.Parse();
		if (C) *C = P.GetC();
		return R;
	}

	void ValidateExpressionShapes(const TArray<FVexToken>& T, const FVexProgramAst& P, TArray<FVexIssue>& Issues) {
		for (const auto& S : P.Statements) {
			if (S.Kind != EVexStatementKind::Expression && S.Kind != EVexStatementKind::Assignment && S.Kind != EVexStatementKind::IfHeader && S.Kind != EVexStatementKind::WhileHeader) continue;
			if (S.ExpressionNodeIndex == INDEX_NONE) { AddIssue(Issues, EVexIssueSeverity::Error, TEXT("Malformed expression"), T.IsValidIndex(S.StartTokenIndex) ? T[S.StartTokenIndex].Line : 1, T.IsValidIndex(S.StartTokenIndex) ? T[S.StartTokenIndex].Column : 1); continue; }
			TSet<int32> V; TArray<int32> Stk; Stk.Add(S.ExpressionNodeIndex);
			while (Stk.Num() > 0) {
				int32 Idx = Stk.Pop(EAllowShrinking::No); if (!P.Expressions.IsValidIndex(Idx) || V.Contains(Idx)) continue; V.Add(Idx); const auto& N = P.Expressions[Idx];
				if (N.Kind == EVexExprKind::UnaryOp && N.LeftNodeIndex == INDEX_NONE) AddIssue(Issues, EVexIssueSeverity::Error, TEXT("Malformed unary"), T.IsValidIndex(N.TokenIndex) ? T[N.TokenIndex].Line : 1, T.IsValidIndex(N.TokenIndex) ? T[N.TokenIndex].Column : 1);
				if ((N.Kind == EVexExprKind::BinaryOp || N.Kind == EVexExprKind::Pipe || N.Kind == EVexExprKind::PipeIn || N.Kind == EVexExprKind::PipeOut) && (N.LeftNodeIndex == INDEX_NONE || N.RightNodeIndex == INDEX_NONE)) AddIssue(Issues, EVexIssueSeverity::Error, TEXT("Malformed binary"), T.IsValidIndex(N.TokenIndex) ? T[N.TokenIndex].Line : 1, T.IsValidIndex(N.TokenIndex) ? T[N.TokenIndex].Column : 1);
				if (N.LeftNodeIndex != INDEX_NONE) Stk.Add(N.LeftNodeIndex); if (N.RightNodeIndex != INDEX_NONE) Stk.Add(N.RightNodeIndex); for (const int32 A : N.ArgumentNodeIndices) if (A != INDEX_NONE) Stk.Add(A);
			}
		}
	}
}

EVexRequirement AnalyzeRequirements(const FVexProgramAst& P) {
	EVexRequirement R = EVexRequirement::None;
	for (const auto& N : P.Expressions) { if (N.Kind == EVexExprKind::SymbolRef) R |= EVexRequirement::SwarmState; if (N.Kind == EVexExprKind::FunctionCall || N.Kind == EVexExprKind::Pipe) { const auto& Nm = N.Lexeme; if (Nm == TEXT("sample_lattice") || Nm == TEXT("inject_light")) R |= EVexRequirement::LightLattice; else if (Nm == TEXT("sample_cloud") || Nm == TEXT("inject_cloud")) R |= EVexRequirement::VolumetricCloud; else if (Nm == TEXT("sample_sdf") || Nm.StartsWith(TEXT("sd")) || Nm.StartsWith(TEXT("op"))) R |= EVexRequirement::StructureSDF; } }
	for (const auto& S : P.Statements) { if (S.Kind == EVexStatementKind::TargetDirective) { if (S.SourceSpan == TEXT("@async")) R |= EVexRequirement::Async; } if (S.Kind == EVexStatementKind::Assignment) R |= EVexRequirement::WriteState; } return R;
}

FVexParseResult ParseAndValidate(const FString& Src, const TArray<FVexSymbolDefinition>& SymbolDefs, bool bReq) {
	FVexParseResult R; R.Program.Tokens = FVexLexer::Tokenize(Src, R.Issues); BuildStatementAst(R.Program.Tokens, R.Program.Statements, R.Issues);
	TMap<FString, FVexSymbolDefinition> Syms; for (const auto& D : SymbolDefs) Syms.Add(D.SymbolName, D);
	TSet<FString> Known; for (const auto& D : SymbolDefs) Known.Add(D.SymbolName);
	for (auto& S : R.Program.Statements) {
		if (S.Kind != EVexStatementKind::Expression && S.Kind != EVexStatementKind::Assignment && S.Kind != EVexStatementKind::IfHeader && S.Kind != EVexStatementKind::WhileHeader) continue;
		int32 ES = S.StartTokenIndex, EE = S.EndTokenIndex;
		if (S.Kind == EVexStatementKind::Assignment) { for (int32 i = S.StartTokenIndex; i <= S.EndTokenIndex; ++i) if (R.Program.Tokens[i].Kind == EVexTokenKind::Operator && IsAssignmentOperator(R.Program.Tokens[i].Lexeme)) { ES = i + 1; break; } }
		else if (S.Kind == EVexStatementKind::IfHeader || S.Kind == EVexStatementKind::WhileHeader) { int32 L = INDEX_NONE, RP = INDEX_NONE; for (int32 i = S.StartTokenIndex; i <= S.EndTokenIndex; ++i) if (R.Program.Tokens[i].Kind == EVexTokenKind::LParen && L == INDEX_NONE) L = i; else if (R.Program.Tokens[i].Kind == EVexTokenKind::RParen) RP = i; if (L != INDEX_NONE && RP != INDEX_NONE && RP > L) { ES = L + 1; EE = RP - 1; } }
		int32 Cr = ES; S.ExpressionNodeIndex = BuildExpressionAst(R.Program.Tokens, ES, EE, R.Program.Expressions, &Cr);
		if (Cr <= EE && R.Program.Tokens.IsValidIndex(Cr)) AddIssue(R.Issues, EVexIssueSeverity::Error, TEXT("Unexpected token"), R.Program.Tokens[Cr].Line, R.Program.Tokens[Cr].Column);
	}
	ValidateExpressionShapes(R.Program.Tokens, R.Program, R.Issues);
	TSet<FString> Used; for (const auto& T : R.Program.Tokens) if (T.Kind == EVexTokenKind::Symbol) Used.Add(T.Lexeme); R.Program.UsedSymbols = Used.Array(); R.Program.UsedSymbols.Sort();
	for (const auto& U : R.Program.UsedSymbols) { if (Known.Contains(U)) continue; R.UnknownSymbols.Add(U); int32 L=1, C=1; for (const auto& T : R.Program.Tokens) if (T.Kind == EVexTokenKind::Symbol && T.Lexeme == U) { L = T.Line; C = T.Column; break; } AddIssue(R.Issues, EVexIssueSeverity::Warning, TEXT("Unknown symbol"), L, C); }
	if (bReq) { for (const auto& D : SymbolDefs) if (D.bRequired && !Used.Contains(D.SymbolName)) R.MissingRequiredSymbols.Add(D.SymbolName); R.MissingRequiredSymbols.Sort(); if (R.MissingRequiredSymbols.Num() > 0) AddIssue(R.Issues, EVexIssueSeverity::Error, TEXT("Missing required symbols"), 1, 1); }
	PerformSemanticValidation(R.Program.Tokens, R.Program, SymbolDefs, R.Issues);
	R.Requirements = AnalyzeRequirements(R.Program); FVexOptimizer::Optimize(R.Program);
	AnnotateInferredTypes(R.Program, Syms, BuildLocalVariableTypeMap(R.Program));
	R.bSuccess = true; for (const auto& I : R.Issues) if (I.Severity == EVexIssueSeverity::Error) R.bSuccess = false; return R;
}

} // namespace Flight::Vex
