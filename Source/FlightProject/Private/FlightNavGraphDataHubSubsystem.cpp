#include "FlightNavGraphDataHubSubsystem.h"

#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightNavGraphHub, Log, All);

namespace FlightNavGraphDataHubConstants
{
    static const TCHAR* InvalidNodeWarning = TEXT("FlightNavGraphDataHubSubsystem: Attempted to register edge with missing node(s).");
}

void UFlightNavGraphDataHubSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogFlightNavGraphHub, Log, TEXT("Nav graph data hub initialized for world %s"), *GetNameSafe(GetWorld()));
}

void UFlightNavGraphDataHubSubsystem::Deinitialize()
{
    ResetGraph();
    Super::Deinitialize();
}

void UFlightNavGraphDataHubSubsystem::ResetGraph()
{
    NodeMap.Reset();
    EdgeMap.Reset();
    NotifyGraphChanged();
}

FGuid UFlightNavGraphDataHubSubsystem::RegisterNode(const FFlightNavGraphNodeDescriptor& Descriptor)
{
    FGuid NodeId = Descriptor.NodeId.IsValid() ? Descriptor.NodeId : FGuid::NewGuid();

    FNavGraphNodeRecord& Record = NodeMap.FindOrAdd(NodeId);
    Record.Descriptor = Descriptor;
    Record.Descriptor.NodeId = NodeId;

    NotifyGraphChanged();
    return NodeId;
}

bool UFlightNavGraphDataHubSubsystem::RemoveNode(const FGuid& NodeId)
{
    if (FNavGraphNodeRecord* Record = NodeMap.Find(NodeId))
    {
        // Remove associated edges first.
        TArray<FGuid> EdgesToRemove = Record->ConnectedEdges.Array();
        for (const FGuid& EdgeId : EdgesToRemove)
        {
            RemoveEdge(EdgeId);
        }

        NodeMap.Remove(NodeId);
        NotifyGraphChanged();
        return true;
    }
    return false;
}

FGuid UFlightNavGraphDataHubSubsystem::RegisterEdge(const FFlightNavGraphEdgeDescriptor& Descriptor)
{
    if (!Descriptor.FromNodeId.IsValid() || !Descriptor.ToNodeId.IsValid())
    {
        UE_LOG(LogFlightNavGraphHub, Warning, TEXT("%s (invalid node ids)"), FlightNavGraphDataHubConstants::InvalidNodeWarning);
        return FGuid();
    }

    FNavGraphNodeRecord* FromNode = NodeMap.Find(Descriptor.FromNodeId);
    FNavGraphNodeRecord* ToNode = NodeMap.Find(Descriptor.ToNodeId);

    if (!FromNode || !ToNode)
    {
        UE_LOG(LogFlightNavGraphHub, Warning, TEXT("%s (missing node records)"), FlightNavGraphDataHubConstants::InvalidNodeWarning);
        return FGuid();
    }

    FGuid EdgeId = Descriptor.EdgeId.IsValid() ? Descriptor.EdgeId : FGuid::NewGuid();

    if (FNavGraphEdgeRecord* ExistingEdge = EdgeMap.Find(EdgeId))
    {
        const FGuid PreviousFrom = ExistingEdge->Descriptor.FromNodeId;
        const FGuid PreviousTo = ExistingEdge->Descriptor.ToNodeId;

        if (FNavGraphNodeRecord* PreviousFromNode = NodeMap.Find(PreviousFrom))
        {
            PreviousFromNode->ConnectedEdges.Remove(EdgeId);
        }
        if (FNavGraphNodeRecord* PreviousToNode = NodeMap.Find(PreviousTo))
        {
            PreviousToNode->ConnectedEdges.Remove(EdgeId);
        }
    }

    FNavGraphEdgeRecord& EdgeRecord = EdgeMap.FindOrAdd(EdgeId);
    EdgeRecord.Descriptor = Descriptor;
    EdgeRecord.Descriptor.EdgeId = EdgeId;

    // Track adjacency on the nodes.
    FromNode->ConnectedEdges.Add(EdgeId);
    ToNode->ConnectedEdges.Add(EdgeId);

    NotifyGraphChanged();
    return EdgeId;
}

bool UFlightNavGraphDataHubSubsystem::RemoveEdge(const FGuid& EdgeId)
{
    if (FNavGraphEdgeRecord* EdgeRecord = EdgeMap.Find(EdgeId))
    {
        const FGuid FromNodeId = EdgeRecord->Descriptor.FromNodeId;
        const FGuid ToNodeId = EdgeRecord->Descriptor.ToNodeId;

        if (FNavGraphNodeRecord* FromNode = NodeMap.Find(FromNodeId))
        {
            FromNode->ConnectedEdges.Remove(EdgeId);
        }
        if (FNavGraphNodeRecord* ToNode = NodeMap.Find(ToNodeId))
        {
            ToNode->ConnectedEdges.Remove(EdgeId);
        }

        EdgeMap.Remove(EdgeId);
        NotifyGraphChanged();
        return true;
    }
    return false;
}

FFlightNavGraphSnapshot UFlightNavGraphDataHubSubsystem::BuildSnapshot() const
{
    FFlightNavGraphSnapshot Snapshot;
    Snapshot.Nodes.Reserve(NodeMap.Num());
    Snapshot.Edges.Reserve(EdgeMap.Num());

    // Build node snapshots and bounds
    for (const TPair<FGuid, FNavGraphNodeRecord>& Pair : NodeMap)
    {
        const FFlightNavGraphNodeDescriptor& Desc = Pair.Value.Descriptor;
        FFlightNavGraphNodeSnapshot NodeSnapshot;
        NodeSnapshot.NodeId = Desc.NodeId;
        NodeSnapshot.DisplayName = Desc.DisplayName;
        NodeSnapshot.NetworkId = Desc.NetworkId;
        NodeSnapshot.SubNetworkId = Desc.SubNetworkId;
        NodeSnapshot.Location = Desc.Location;
        NodeSnapshot.Tags = Desc.Tags;
        Snapshot.Nodes.Add(MoveTemp(NodeSnapshot));

        Snapshot.Bounds += Desc.Location;
    }

    for (const TPair<FGuid, FNavGraphEdgeRecord>& Pair : EdgeMap)
    {
        const FFlightNavGraphEdgeDescriptor& Desc = Pair.Value.Descriptor;
        FFlightNavGraphEdgeSnapshot EdgeSnapshot;
        EdgeSnapshot.EdgeId = Desc.EdgeId;
        EdgeSnapshot.FromNodeId = Desc.FromNodeId;
        EdgeSnapshot.ToNodeId = Desc.ToNodeId;
        EdgeSnapshot.BaseCost = Desc.BaseCost;
        EdgeSnapshot.bBidirectional = Desc.bBidirectional;
        EdgeSnapshot.Tags = Desc.Tags;

        if (const FNavGraphNodeRecord* FromNode = NodeMap.Find(Desc.FromNodeId))
        {
            EdgeSnapshot.FromLocation = FromNode->Descriptor.Location;
        }
        if (const FNavGraphNodeRecord* ToNode = NodeMap.Find(Desc.ToNodeId))
        {
            EdgeSnapshot.ToLocation = ToNode->Descriptor.Location;
        }

        Snapshot.Edges.Add(MoveTemp(EdgeSnapshot));
    }

    return Snapshot;
}

void UFlightNavGraphDataHubSubsystem::NotifyGraphChanged()
{
    GraphChangedEvent.Broadcast();
}
