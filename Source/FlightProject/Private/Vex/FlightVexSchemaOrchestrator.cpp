// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexSchemaOrchestrator.h"

namespace Flight::Vex
{

namespace
{

EVexValueType ToSchemaValueType(const EFlightVexSymbolValueType ValueType)
{
	switch (ValueType)
	{
	case EFlightVexSymbolValueType::Float: return EVexValueType::Float;
	case EFlightVexSymbolValueType::Float2: return EVexValueType::Float2;
	case EFlightVexSymbolValueType::Float3: return EVexValueType::Float3;
	case EFlightVexSymbolValueType::Float4: return EVexValueType::Float4;
	case EFlightVexSymbolValueType::Int: return EVexValueType::Int;
	case EFlightVexSymbolValueType::Bool: return EVexValueType::Bool;
	default: return EVexValueType::Unknown;
	}
}

} // namespace

FVexTypeId FVexSchemaOrchestrator::MakeTypeId(const void* RuntimeKey, const FName StableName)
{
	FVexTypeId TypeId;
	TypeId.RuntimeKey = RuntimeKey;
	TypeId.StableName = StableName;
	return TypeId;
}

FVexTypeSchema FVexSchemaOrchestrator::MergeManifestRequirements(FVexTypeSchema BaseSchema, const TArray<FFlightVexSymbolRow>& Rows)
{
	for (const FFlightVexSymbolRow& Row : Rows)
	{
		FVexLogicalSymbolSchema& Logical = BaseSchema.LogicalSymbols.FindOrAdd(Row.SymbolName);
		Logical.SymbolName = Row.SymbolName;
		Logical.ValueType = ToSchemaValueType(Row.ValueType);
		Logical.bWritable = Row.bWritable;
		Logical.bRequired = Row.bRequired;
		Logical.bSimdReadAllowed = Row.bSimdReadAllowed;
		Logical.bSimdWriteAllowed = Row.bSimdWriteAllowed;
		Logical.bGpuTier1Allowed = Row.bGpuTier1Allowed;
		Logical.Residency = Row.Residency;
		Logical.Affinity = Row.Affinity;
		Logical.AlignmentRequirement = Row.AlignmentRequirement;
		Logical.MathDeterminismProfile = Row.MathDeterminismProfile;
		Logical.BackendBinding.HlslIdentifier = Row.HlslIdentifier;
		Logical.BackendBinding.VerseIdentifier = Row.VerseIdentifier;

		if (Logical.Storage.Kind == EVexStorageKind::None)
		{
			Logical.Storage.Kind = EVexStorageKind::ExternalProvider;
		}

		if (FVexSymbolAccessor* Accessor = BaseSchema.Symbols.Find(Row.SymbolName))
		{
			Accessor->Residency = Row.Residency;
			if (!Row.HlslIdentifier.IsEmpty())
			{
				Accessor->HlslIdentifier = Row.HlslIdentifier;
			}
			if (!Row.VerseIdentifier.IsEmpty())
			{
				Accessor->VerseIdentifier = Row.VerseIdentifier;
			}
		}
	}

	BaseSchema.LayoutHash = ComputeSchemaLayoutHash(BaseSchema);
	return BaseSchema;
}

uint32 FVexSchemaOrchestrator::ComputeSchemaLayoutHash(const FVexTypeSchema& Schema)
{
	TArray<FString> SymbolNames;
	Schema.LogicalSymbols.GetKeys(SymbolNames);
	SymbolNames.Sort();

	FString LayoutText = Schema.TypeName;
	LayoutText += TEXT("|");
	LayoutText += FString::Printf(TEXT("%u|%u|"), Schema.Size, Schema.Alignment);

	for (const FString& SymbolName : SymbolNames)
	{
		const FVexLogicalSymbolSchema* Logical = Schema.LogicalSymbols.Find(SymbolName);
		if (!Logical)
		{
			continue;
		}

		LayoutText += SymbolName;
		LayoutText += TEXT("|");
		LayoutText += ToTypeString(Logical->ValueType);
		LayoutText += TEXT("|");
		LayoutText += FString::FromInt(static_cast<int32>(Logical->Storage.Kind));
		LayoutText += TEXT("|");
		LayoutText += FString::FromInt(Logical->Storage.MemberOffset);
		LayoutText += TEXT("|");
		LayoutText += Logical->BackendBinding.HlslIdentifier;
		LayoutText += TEXT("|");
		LayoutText += Logical->BackendBinding.VerseIdentifier;
		LayoutText += TEXT("|");
	}

	return FCrc::StrCrc32(*LayoutText);
}

} // namespace Flight::Vex
