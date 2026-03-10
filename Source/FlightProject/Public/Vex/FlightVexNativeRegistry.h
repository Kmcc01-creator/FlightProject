// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexTypes.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMOpResult.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMNativeFunction.h"
#endif

namespace Flight::Vex
{

/**
 * IVexNativeFunction: Interface for C++ thunks exposed to VerseVM.
 */
class IVexNativeFunction
{
public:
	virtual ~IVexNativeFunction() = default;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	virtual Verse::FOpResult Execute(
		Verse::FRunningContext Context,
		Verse::VValue Self,
		Verse::VNativeFunction::Args Arguments) = 0;
#endif

	virtual FString GetName() const = 0;
	virtual int32 GetNumArgs() const = 0;
};

/**
 * FVexNativeRegistry: Central registry for VEX native functions and symbols.
 */
class FLIGHTPROJECT_API FVexNativeRegistry
{
public:
	static FVexNativeRegistry& Get();

	/** Register a new native function thunk. */
	void RegisterFunction(TSharedPtr<IVexNativeFunction> Function);

	/** Find a registered function by name. */
	TSharedPtr<IVexNativeFunction> FindFunction(const FString& Name) const;

	/** Get all registered functions. */
	const TMap<FString, TSharedPtr<IVexNativeFunction>>& GetFunctions() const { return FunctionMap; }

private:
	TMap<FString, TSharedPtr<IVexNativeFunction>> FunctionMap;
};

} // namespace Flight::Vex
