// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Core/FlightReflectionDebug.h"

#include "UObject/UnrealType.h"

namespace Flight::Reflection::Debug
{

TArray<FString> DumpNativeStructFields(const UStruct& StructType, const void* StructMemory)
{
	TArray<FString> Lines;
	if (!StructMemory)
	{
		return Lines;
	}

	for (TFieldIterator<FProperty> It(&StructType); It; ++It)
	{
		const FProperty* Property = *It;
		FString ValueText;
		Property->ExportTextItem_Direct(
			ValueText,
			Property->ContainerPtrToValuePtr<void>(const_cast<void*>(StructMemory)),
			nullptr,
			nullptr,
			PPF_None);
		Lines.Add(FString::Printf(TEXT("%s=%s"), *Property->GetName(), *ValueText));
	}

	return Lines;
}

FString DumpNativeStructFieldsToString(
	const UStruct& StructType,
	const void* StructMemory,
	const FString& Delimiter)
{
	return FString::Join(DumpNativeStructFields(StructType, StructMemory), *Delimiter);
}

}
