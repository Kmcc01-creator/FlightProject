#include "Engine/World.h"
#include "Engine/Engine.h"
#include "FlightNavGraphDataHubSubsystem.h"
#include "FlightNavGraphTypes.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Logging/StructuredLog.h"

namespace FlightDebugConsole
{
    namespace
    {
        UWorld* ResolveDebugWorld()
        {
            if (!GEngine)
            {
                return nullptr;
            }

            // Prefer the first PIE or game world.
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::Editor)
                {
                    if (UWorld* ContextWorld = Context.World())
                    {
                        return ContextWorld;
                    }
                }
            }

            return nullptr;
        }

        void DumpNavGraph(const TArray<FString>& Args, UWorld* World)
        {
            if (!World)
            {
                World = ResolveDebugWorld();
            }

            if (!World)
            {
                UE_LOGFMT(LogTemp, Warning, "Flight.Debug.DumpNavGraph: No active world context available.");
                return;
            }

            UFlightNavGraphDataHubSubsystem* Hub = World->GetSubsystem<UFlightNavGraphDataHubSubsystem>();
            if (!Hub)
            {
                UE_LOG(LogTemp, Warning, TEXT("Flight.Debug.DumpNavGraph: UFlightNavGraphDataHubSubsystem not found on world '%s'."), *World->GetName());
                return;
            }

            const FFlightNavGraphSnapshot Snapshot = Hub->BuildSnapshot();
            UE_LOG(LogTemp, Display, TEXT("Flight.NavGraph Snapshot: Nodes=%d, Edges=%d, Bounds=(%s -> %s)"),
                Snapshot.Nodes.Num(),
                Snapshot.Edges.Num(),
                *Snapshot.Bounds.Min.ToString(),
                *Snapshot.Bounds.Max.ToString());

            const bool bVerbose = Args.Contains(TEXT("verbose"));

            if (!bVerbose)
            {
                UE_LOG(LogTemp, Display, TEXT("  Use 'Flight.Debug.DumpNavGraph verbose' for per-node details."));
                return;
            }

            for (const FFlightNavGraphNodeSnapshot& Node : Snapshot.Nodes)
            {
                FString TagString;
                for (int32 TagIndex = 0; TagIndex < Node.Tags.Num(); ++TagIndex)
                {
                    TagString += Node.Tags[TagIndex].ToString();
                    if (TagIndex < Node.Tags.Num() - 1)
                    {
                        TagString += TEXT(",");
                    }
                }

                UE_LOG(LogTemp, Display,
                    TEXT("    Node %s: Name=%s, Network=%s, SubNetwork=%s, Location=%s, Tags=[%s]"),
                    *Node.NodeId.ToString(EGuidFormats::Digits),
                    *Node.DisplayName.ToString(),
                    *Node.NetworkId.ToString(),
                    *Node.SubNetworkId.ToString(),
                    *Node.Location.ToString(),
                    *TagString);
            }

            for (const FFlightNavGraphEdgeSnapshot& Edge : Snapshot.Edges)
            {
                FString TagString;
                for (int32 TagIndex = 0; TagIndex < Edge.Tags.Num(); ++TagIndex)
                {
                    TagString += Edge.Tags[TagIndex].ToString();
                    if (TagIndex < Edge.Tags.Num() - 1)
                    {
                        TagString += TEXT(",");
                    }
                }

                UE_LOG(LogTemp, Display,
                    TEXT("    Edge %s: %s -> %s  Cost=%.2f  Bidirectional=%s  Tags=[%s]"),
                    *Edge.EdgeId.ToString(EGuidFormats::Digits),
                    *Edge.FromNodeId.ToString(EGuidFormats::Digits),
                    *Edge.ToNodeId.ToString(EGuidFormats::Digits),
                    Edge.BaseCost,
                    Edge.bBidirectional ? TEXT("true") : TEXT("false"),
                    *TagString);
            }
        }

        void DumpOrchestrationReport(const TArray<FString>& Args, UWorld* World)
        {
            if (!World)
            {
                World = ResolveDebugWorld();
            }

            if (!World)
            {
                UE_LOGFMT(LogTemp, Warning, "Flight.Debug.DumpOrchestrationReport: No active world context available.");
                return;
            }

            UFlightOrchestrationSubsystem* OrchestrationSubsystem = World->GetSubsystem<UFlightOrchestrationSubsystem>();
            if (!OrchestrationSubsystem)
            {
                UE_LOG(LogTemp, Warning, TEXT("Flight.Debug.DumpOrchestrationReport: UFlightOrchestrationSubsystem not found on world '%s'."), *World->GetName());
                return;
            }

            OrchestrationSubsystem->Rebuild();
            OrchestrationSubsystem->LogReport(Args.Contains(TEXT("verbose")));
        }
    }

    static FAutoConsoleCommandWithWorldAndArgs GDumpNavGraphCommand(
        TEXT("Flight.Debug.DumpNavGraph"),
        TEXT("Dump the current nav graph snapshot. Pass 'verbose' for per-node output."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpNavGraph));

    static FAutoConsoleCommandWithWorldAndArgs GDumpOrchestrationReportCommand(
        TEXT("Flight.Debug.DumpOrchestrationReport"),
        TEXT("Dump the current orchestration report. Pass 'verbose' to emit the full JSON report."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpOrchestrationReport));
}
