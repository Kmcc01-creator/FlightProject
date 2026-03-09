// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "uLang/CompilerPasses/IAssemblerPass.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMWriteBarrier.h"
#endif

namespace Flight::Verse
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
/**
 * Experimental VerseVM assembler scaffold.
 */
class FFlightVerseAssemblerPass final : public uLang::IAssemblerPass
{
public:
	FFlightVerseAssemblerPass() = default;

	virtual void TranslateExpressions(
		const uLang::TSRef<uLang::CSemanticProgram>& SemanticResult,
		const uLang::SBuildContext& BuildContext,
		const uLang::SProgramContext& ProgramContext) const override;

	virtual uLang::ELinkerResult Link(
		const uLang::SBuildContext& BuildContext,
		const uLang::SProgramContext& ProgramContext) const override;

	/** Retrieves a compiled procedure by its decorated Verse name. */
	static Verse::VProcedure* GetCompiledProcedure(const FString& DecoratedName);

	/** Clears all cached procedures. */
	static void ClearRegistry();

private:
	static TMap<FString, Verse::TWriteBarrier<Verse::VProcedure>> CompiledProcedures;
};
#endif
} // namespace Flight::Verse
