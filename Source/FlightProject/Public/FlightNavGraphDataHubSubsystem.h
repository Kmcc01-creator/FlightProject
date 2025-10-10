#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightNavGraphTypes.h"
#include "FlightNavGraphDataHubSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnFlightNavGraphChanged);

/**
 * Runtime data hub that aggregates navigation graph nodes/edges for visualization and analytics.
 * Intended as the bridge between gameplay data and hegetic rendering systems.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightNavGraphDataHubSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Removes all nodes and edges from the current graph. */
    UFUNCTION(BlueprintCallable, Category = "Flight|NavGraph")
    void ResetGraph();

    /**
     * Registers (or updates) a navigation node. When Descriptor.NodeId is unset a GUID is generated.
     * @return The identifier associated with the node after registration.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|NavGraph")
    FGuid RegisterNode(const FFlightNavGraphNodeDescriptor& Descriptor);

    /**
     * Removes a node and any edges that reference it.
     * @return true if the node existed and was removed.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|NavGraph")
    bool RemoveNode(const FGuid& NodeId);

    /**
     * Registers (or updates) an edge connecting two nodes.
     * Edges require valid node identifiers to succeed.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|NavGraph")
    FGuid RegisterEdge(const FFlightNavGraphEdgeDescriptor& Descriptor);

    /** Removes the specified edge if present. */
    UFUNCTION(BlueprintCallable, Category = "Flight|NavGraph")
    bool RemoveEdge(const FGuid& EdgeId);

    /** Builds a snapshot of the current graph for visualization systems. */
    UFUNCTION(BlueprintCallable, Category = "Flight|NavGraph")
    FFlightNavGraphSnapshot BuildSnapshot() const;

    /** Event broadcast whenever the graph topology changes. */
    FOnFlightNavGraphChanged& OnGraphChanged() { return GraphChangedEvent; }

    /** Returns true if the graph currently holds any nodes. */
    UFUNCTION(BlueprintPure, Category = "Flight|NavGraph")
    bool HasNodes() const { return NodeMap.Num() > 0; }

    /** Returns true if the graph currently holds any edges. */
    UFUNCTION(BlueprintPure, Category = "Flight|NavGraph")
    bool HasEdges() const { return EdgeMap.Num() > 0; }

private:
    struct FNavGraphNodeRecord
    {
        FFlightNavGraphNodeDescriptor Descriptor;
        TSet<FGuid> ConnectedEdges;
    };

    struct FNavGraphEdgeRecord
    {
        FFlightNavGraphEdgeDescriptor Descriptor;
    };

    void NotifyGraphChanged();

    TMap<FGuid, FNavGraphNodeRecord> NodeMap;
    TMap<FGuid, FNavGraphEdgeRecord> EdgeMap;
    FOnFlightNavGraphChanged GraphChangedEvent;
};
