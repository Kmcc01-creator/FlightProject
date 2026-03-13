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

void EnsureCanonicalRecords(FVexTypeSchema& Schema)
{
	if (!Schema.SymbolRecords.IsEmpty())
	{
		return;
	}

	for (const TPair<FString, FVexLogicalSymbolSchema>& Pair : Schema.LogicalSymbols)
	{
		FVexSymbolRecord Record;
		Record.SymbolName = Pair.Key;
		Record.ValueType = Pair.Value.ValueType;
		Record.bReadable = Pair.Value.bReadable;
		Record.bWritable = Pair.Value.bWritable;
		Record.bRequired = Pair.Value.bRequired;
		Record.bSimdReadAllowed = Pair.Value.bSimdReadAllowed;
		Record.bSimdWriteAllowed = Pair.Value.bSimdWriteAllowed;
		Record.bGpuTier1Allowed = Pair.Value.bGpuTier1Allowed;
		Record.Residency = Pair.Value.Residency;
		Record.Affinity = Pair.Value.Affinity;
		Record.AlignmentRequirement = Pair.Value.AlignmentRequirement;
		Record.MathDeterminismProfile = Pair.Value.MathDeterminismProfile;
		Record.BackendBinding = Pair.Value.BackendBinding;
		Record.Storage = Pair.Value.Storage;
		Record.VectorPack = Pair.Value.VectorPack.StorageClass == EVexVectorStorageClass::None
			? BuildDefaultVectorPackContract(
				Record.Storage,
				Record.ValueType,
				Record.AlignmentRequirement,
				Record.bSimdReadAllowed,
				Record.bSimdWriteAllowed,
				Record.bGpuTier1Allowed)
			: Pair.Value.VectorPack;

		if (const FVexSymbolAccessor* Accessor = Schema.Symbols.Find(Pair.Key))
		{
			if (Accessor->Getter)
			{
				Record.ReadValue = [LegacyGetter = Accessor->Getter](const void* Ptr) -> FVexRuntimeValue
				{
					return FVexRuntimeValue::FromFloat(LegacyGetter(Ptr));
				};
			}

			if (Accessor->Setter)
			{
				Record.WriteValue = [LegacySetter = Accessor->Setter](void* Ptr, const FVexRuntimeValue& Value) -> bool
				{
					LegacySetter(Ptr, Value.AsFloat());
					return true;
				};
			}

			Record.Getter = Accessor->Getter;
			Record.Setter = Accessor->Setter;
			if (Record.Storage.MemberOffset == INDEX_NONE)
			{
				Record.Storage.MemberOffset = Accessor->MemberOffset;
			}
		}

		Schema.SymbolRecords.Add(Pair.Key, MoveTemp(Record));
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
	EnsureCanonicalRecords(BaseSchema);

	for (const FFlightVexSymbolRow& Row : Rows)
	{
		FVexSymbolRecord& Record = BaseSchema.SymbolRecords.FindOrAdd(Row.SymbolName);
		Record.SymbolName = Row.SymbolName;
		Record.ValueType = ToSchemaValueType(Row.ValueType);
		Record.bWritable = Row.bWritable;
		Record.bRequired = Row.bRequired;
		Record.bSimdReadAllowed = Row.bSimdReadAllowed;
		Record.bSimdWriteAllowed = Row.bSimdWriteAllowed;
		Record.bGpuTier1Allowed = Row.bGpuTier1Allowed;
		Record.Residency = Row.Residency;
		Record.Affinity = Row.Affinity;
		Record.AlignmentRequirement = Row.AlignmentRequirement;
		Record.MathDeterminismProfile = Row.MathDeterminismProfile;
		if (!Row.HlslIdentifier.IsEmpty())
		{
			Record.BackendBinding.HlslIdentifier = Row.HlslIdentifier;
		}
		if (!Row.VerseIdentifier.IsEmpty())
		{
			Record.BackendBinding.VerseIdentifier = Row.VerseIdentifier;
		}

		if (Record.Storage.Kind == EVexStorageKind::None)
		{
			Record.Storage.Kind = EVexStorageKind::ExternalProvider;
		}

		Record.VectorPack = BuildDefaultVectorPackContract(
			Record.Storage,
			Record.ValueType,
			Record.AlignmentRequirement,
			Record.bSimdReadAllowed,
			Record.bSimdWriteAllowed,
			Record.bGpuTier1Allowed);
	}

	BaseSchema.RebuildLegacyViews();
	BaseSchema.LayoutHash = ComputeSchemaLayoutHash(BaseSchema);
	return BaseSchema;
}

uint32 FVexSchemaOrchestrator::ComputeSchemaLayoutHash(const FVexTypeSchema& Schema)
{
	TArray<FString> SymbolNames;
	if (!Schema.SymbolRecords.IsEmpty())
	{
		Schema.SymbolRecords.GetKeys(SymbolNames);
	}
	else
	{
		Schema.LogicalSymbols.GetKeys(SymbolNames);
	}
	SymbolNames.Sort();

	FString LayoutText = Schema.TypeName;
	LayoutText += TEXT("|");
	LayoutText += FString::Printf(TEXT("%u|%u|"), Schema.Size, Schema.Alignment);

	for (const FString& SymbolName : SymbolNames)
	{
		const FVexSymbolRecord* Record = Schema.FindSymbolRecord(SymbolName);
		const FVexLogicalSymbolSchema* Logical = Record ? nullptr : Schema.LogicalSymbols.Find(SymbolName);
		if (!Record && !Logical)
		{
			continue;
		}

		LayoutText += SymbolName;
		LayoutText += TEXT("|");
		LayoutText += ToTypeString(Record ? Record->ValueType : Logical->ValueType);
		LayoutText += TEXT("|");
		LayoutText += FString::FromInt(static_cast<int32>(Record ? Record->Storage.Kind : Logical->Storage.Kind));
		LayoutText += TEXT("|");
		LayoutText += FString::FromInt(Record ? Record->Storage.MemberOffset : Logical->Storage.MemberOffset);
		LayoutText += TEXT("|");
		LayoutText += FString::FromInt(static_cast<int32>(Record ? Record->VectorPack.StorageClass : Logical->VectorPack.StorageClass));
		LayoutText += TEXT("|");
		LayoutText += FString::FromInt((Record ? Record->VectorPack.bSupportsDirectSimdRead : Logical->VectorPack.bSupportsDirectSimdRead) ? 1 : 0);
		LayoutText += TEXT("|");
		LayoutText += FString::FromInt((Record ? Record->VectorPack.bSupportsDirectSimdWrite : Logical->VectorPack.bSupportsDirectSimdWrite) ? 1 : 0);
		LayoutText += TEXT("|");
		LayoutText += Record ? Record->BackendBinding.HlslIdentifier : Logical->BackendBinding.HlslIdentifier;
		LayoutText += TEXT("|");
		LayoutText += Record ? Record->BackendBinding.VerseIdentifier : Logical->BackendBinding.VerseIdentifier;
		LayoutText += TEXT("|");
	}

	return FCrc::StrCrc32(*LayoutText);
}

} // namespace Flight::Vex
