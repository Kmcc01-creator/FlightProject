#pragma once

#include "CoreMinimal.h"
#include "FlightNavGraphTypes.generated.h"

/**
 * Descriptor used when registering or updating a navigation node within the data hub.
 */
USTRUCT(BlueprintType)
struct FFlightNavGraphNodeDescriptor
{
    GENERATED_BODY()

    /** Optional explicit identifier. If unset a new GUID is generated when registering the node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FGuid NodeId;

    /** Designer-friendly label used for debug overlays or UI widgets. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FName DisplayName = NAME_None;

    /** High-level area the node belongs to (e.g. macro network). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FName NetworkId = NAME_None;

    /** Optional subnetwork grouping for fine-grained routing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FName SubNetworkId = NAME_None;

    /** World-space location of the node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FVector Location = FVector::ZeroVector;

    /** Arbitrary semantic tags (e.g. Refuel, Depot, AvoidLowAltitude). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    TArray<FName> Tags;
};

/**
 * Descriptor supplied when creating or updating an edge between two nodes.
 */
USTRUCT(BlueprintType)
struct FFlightNavGraphEdgeDescriptor
{
    GENERATED_BODY()

    /** Optional explicit identifier for the edge. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FGuid EdgeId;

    /** The node the edge originates from. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FGuid FromNodeId;

    /** The node the edge terminates on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FGuid ToNodeId;

    /** Base traversal cost (seconds, energy, etc.). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    float BaseCost = 1.f;

    /** If true the edge can be traversed in both directions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    bool bBidirectional = true;

    /** Optional semantic tags (e.g. WeatherSensitive, Restricted). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    TArray<FName> Tags;
};

/**
 * Lightweight snapshot representing a node for visualization or analytics.
 */
USTRUCT(BlueprintType)
struct FFlightNavGraphNodeSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FGuid NodeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FName DisplayName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FName NetworkId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FName SubNetworkId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FVector Location = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    TArray<FName> Tags;
};

/**
 * Lightweight snapshot representing an edge, including resolved node positions to make rendering easy.
 */
USTRUCT(BlueprintType)
struct FFlightNavGraphEdgeSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FGuid EdgeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FGuid FromNodeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FGuid ToNodeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FVector FromLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FVector ToLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    float BaseCost = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    bool bBidirectional = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    TArray<FName> Tags;
};

/**
 * Aggregate snapshot returned to visualization systems.
 */
USTRUCT(BlueprintType)
struct FFlightNavGraphSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    TArray<FFlightNavGraphNodeSnapshot> Nodes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    TArray<FFlightNavGraphEdgeSnapshot> Edges;

    /** Bounding box that encloses all nodes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|NavGraph")
    FBox Bounds = FBox(ForceInit);
};
