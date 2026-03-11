// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Orchestration/FlightMegaKernelSubsystem.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "Orchestration/FlightExecutionPlan.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightCompileArtifacts.h"
#include "Vex/Frontend/VexParser.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "FlightProject.h"
#include "FlightLogCategories.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Containers/Set.h"
#include "ShaderCore.h"

void UFlightMegaKernelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	Collection.InitializeDependency<UFlightOrchestrationSubsystem>();
	Collection.InitializeDependency<UFlightVerseSubsystem>();

	// Register the virtual shader directory for generated source
	const FString GeneratedDir = FPaths::ProjectIntermediateDir() / TEXT("Shaders/Generated/");
	IFileManager::Get().MakeDirectory(*GeneratedDir, true);

	if (!IsRunningCommandlet())
	{
		if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/FlightProject/Generated")))
		{
			AddShaderSourceDirectoryMapping(TEXT("/FlightProject/Generated"), GeneratedDir);
		}
	}

	if (UFlightOrchestrationSubsystem* Orchestration = GetWorld()->GetSubsystem<UFlightOrchestrationSubsystem>())
	{
		Orchestration->OnExecutionPlanUpdated.AddUObject(this, &UFlightMegaKernelSubsystem::OnExecutionPlanUpdated);
	}
}

void UFlightMegaKernelSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>()) 
		{
			Orchestration->OnExecutionPlanUpdated.RemoveAll(this);
		}
	}
	Super::Deinitialize();
}

void UFlightMegaKernelSubsystem::Synthesize()
{
	using namespace Flight::Orchestration;
	using namespace Flight::Vex;

	UWorld* World = GetWorld();
	UFlightOrchestrationSubsystem* Orchestration = World ? World->GetSubsystem<UFlightOrchestrationSubsystem>() : nullptr;
	UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;

	if (!Orchestration || !VerseSubsystem)
	{
		return;
	}

	const FFlightExecutionPlan& Plan = Orchestration->GetExecutionPlan();
	LastPlanGeneration = Plan.Generation;

	TMap<uint32, FVexProgramAst> GpuScripts;
	TMap<uint32, TSet<FString>> GpuWrittenSymbols;
	TArray<FString> BehaviorMetadataLines;
	
	// Identify GPU-bound behaviors from the execution plan
	for (const FFlightExecutionPlanStep& Step : Plan.Steps)
	{
		if (Step.ExecutionDomain == EFlightExecutionDomain::Gpu)
		{
			const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(Step.BehaviorID);
			if (Behavior)
			{
				GpuScripts.Add(Step.BehaviorID, Behavior->NativeProgram);
				if (Behavior->SchemaBinding.IsSet())
				{
					GpuWrittenSymbols.Add(Step.BehaviorID, Behavior->SchemaBinding->WrittenSymbols);

					TArray<FString> ReadSymbols = Behavior->SchemaBinding->ReadSymbols.Array();
					ReadSymbols.Sort();
					TArray<FString> WrittenSymbols = Behavior->SchemaBinding->WrittenSymbols.Array();
					WrittenSymbols.Sort();

					TSet<FString> StorageKinds;
					for (const Flight::Vex::FVexSchemaSymbolUse& Use : Behavior->SchemaBinding->SymbolUses)
					{
						if (!Behavior->SchemaBinding->BoundSymbols.IsValidIndex(Use.BoundSymbolIndex))
						{
							continue;
						}

						const Flight::Vex::FVexSchemaBoundSymbol& BoundSymbol = Behavior->SchemaBinding->BoundSymbols[Use.BoundSymbolIndex];
						StorageKinds.Add(Flight::Vex::VexStorageKindToString(BoundSymbol.LogicalSymbol.Storage.Kind));
					}

					TArray<FString> OrderedStorageKinds = StorageKinds.Array();
					OrderedStorageKinds.Sort();
					TArray<FString> FragmentBindings;
					for (const Flight::Vex::FVexSchemaFragmentBinding& FragmentBinding : Behavior->SchemaBinding->FragmentBindings)
					{
						FragmentBindings.Add(FragmentBinding.FragmentType.IsNone()
							? TEXT("<none>")
							: FragmentBinding.FragmentType.ToString());
					}
					FragmentBindings.Sort();
					TArray<FString> ImportedSymbols = Behavior->SchemaBinding->ImportedSymbols.Array();
					ImportedSymbols.Sort();
					TArray<FString> ExportedSymbols = Behavior->SchemaBinding->ExportedSymbols.Array();
					ExportedSymbols.Sort();
					BehaviorMetadataLines.Add(FString::Printf(
						TEXT("// Behavior %u Type=%s LayoutHash=%u Reads=[%s] Writes=[%s] Storage=[%s] Fragments=[%s] BoundaryOps=%d Imports=[%s] Exports=[%s]"),
						Step.BehaviorID,
						Behavior->BoundTypeStableName.IsNone() ? TEXT("<unknown>") : *Behavior->BoundTypeStableName.ToString(),
						Behavior->BoundSchemaLayoutHash,
						*FString::Join(ReadSymbols, TEXT(", ")),
						*FString::Join(WrittenSymbols, TEXT(", ")),
						*FString::Join(OrderedStorageKinds, TEXT(", ")),
						*FString::Join(FragmentBindings, TEXT(", ")),
						Behavior->SchemaBinding->BoundaryUses.Num(),
						*FString::Join(ImportedSymbols, TEXT(", ")),
						*FString::Join(ExportedSymbols, TEXT(", "))));

					if (Behavior->SchemaBinding->bHasBoundaryOperators)
					{
						UE_LOG(
							LogFlightProject,
							Warning,
							TEXT("MegaKernel: Skipping behavior %u because boundary operators are not yet lowered into mega-kernel contracts."),
							Step.BehaviorID);
						GpuScripts.Remove(Step.BehaviorID);
						GpuWrittenSymbols.Remove(Step.BehaviorID);
						continue;
					}
				}
				else
				{
					BehaviorMetadataLines.Add(FString::Printf(
						TEXT("// Behavior %u Type=%s LayoutHash=%u SchemaBinding=<unavailable>"),
						Step.BehaviorID,
						Behavior->BoundTypeStableName.IsNone() ? TEXT("<unknown>") : *Behavior->BoundTypeStableName.ToString(),
						Behavior->BoundSchemaLayoutHash));
				}
			}
		}
	}

	if (GpuScripts.Num() == 0)
	{
		MegaKernelSource = TEXT("// No GPU-bound behaviors in current execution plan.\n");
		return;
	}

	UE_LOG(LogFlightProject, Log, TEXT("MegaKernel: Synthesizing unified source for %d behaviors (Plan Gen %u)"), GpuScripts.Num(), LastPlanGeneration);

	// Phase 3: Bridge to Virtual Shader File
	// We map the logical VEX symbols to the stable parameter names in our Call Contract.
	TMap<FString, FString> SymbolMap;
	SymbolMap.Add(TEXT("@position"), TEXT("pos"));
	SymbolMap.Add(TEXT("@velocity"), TEXT("vel"));
	SymbolMap.Add(TEXT("@shield"), TEXT("shield"));
	SymbolMap.Add(TEXT("@status"), TEXT("status"));
	SymbolMap.Add(TEXT("@dt"), TEXT("dt"));
	SymbolMap.Add(TEXT("@frame"), TEXT("frame"));

	// Call the lowering logic with our stable function name
	MegaKernelSource = FString::Printf(TEXT("// Mega-Kernel PlanGeneration=%u\n"), LastPlanGeneration);
	for (const FString& MetadataLine : BehaviorMetadataLines)
	{
		MegaKernelSource += MetadataLine + TEXT("\n");
	}
	MegaKernelSource += LowerMegaKernel(
		GpuScripts,
		TArray<FVexSymbolDefinition>(),
		SymbolMap,
		TEXT("ExecuteVexBehavior"),
		&GpuWrittenSymbols);

	// Write the synthesized source to the Intermediate generated directory
	const FString GeneratedDir = FPaths::ProjectIntermediateDir() / TEXT("Shaders/Generated/");
	IFileManager::Get().MakeDirectory(*GeneratedDir, true);
	
	const FString FilePath = GeneratedDir / TEXT("VexMegaKernel.ush");
	if (FFileHelper::SaveStringToFile(MegaKernelSource, *FilePath))
	{
		UE_LOG(LogFlightProject, Log, TEXT("MegaKernel: Exported synthesized source to %s"), *FilePath);
	}
	else
	{
		UE_LOG(LogFlightProject, Error, TEXT("MegaKernel: Failed to write synthesized source to %s"), *FilePath);
	}
}

void UFlightMegaKernelSubsystem::OnExecutionPlanUpdated()
{
	Synthesize();
}
