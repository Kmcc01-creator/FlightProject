// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexNativeRegistry.h"

namespace Flight::Vex
{

FVexNativeRegistry& FVexNativeRegistry::Get()
{
	static FVexNativeRegistry Instance;
	return Instance;
}

void FVexNativeRegistry::RegisterFunction(TSharedPtr<IVexNativeFunction> Function)
{
	if (Function.IsValid())
	{
		FunctionMap.Add(Function->GetName(), Function);
	}
}

TSharedPtr<IVexNativeFunction> FVexNativeRegistry::FindFunction(const FString& Name) const
{
	if (const TSharedPtr<IVexNativeFunction>* Found = FunctionMap.Find(Name))
	{
		return *Found;
	}
	return nullptr;
}

} // namespace Flight::Vex
