// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexIr.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
namespace Verse
{
	struct VProcedure;
	struct FAllocationContext;
}
#endif

namespace Flight::Vex
{

/**
 * FVexVvmAssembler: Translates FVexIrProgram into Verse VM bytecode.
 */
class FLIGHTPROJECT_API FVexVvmAssembler
{
public:
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	/**
	 * Assemble a VEX IR program into a VVM procedure.
	 * @param IrProgram The IR program to assemble.
	 * @param AllocContext Allocation context for the VVM.
	 * @param ProcedureName Name for the resulting procedure.
	 * @return The assembled VVM procedure, or nullptr on failure.
	 */
	static Verse::VProcedure* Assemble(
		const FVexIrProgram& IrProgram,
		Verse::FAllocationContext& AllocContext,
		const FString& ProcedureName);
#endif
};

} // namespace Flight::Vex
