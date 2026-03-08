// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Swarm/FlightSwarmSubsystem.h"
#include "Vex/FlightVexParser.h"

class FLIGHTPROJECT_API SSwarmOrchestrator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSwarmOrchestrator) {}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<SMultiLineEditableTextBox> CodeEditor;
	TSharedPtr<SMultiLineEditableTextBox> HLSLPreview;
	TSharedPtr<SMultiLineEditableTextBox> VersePreview;
	
	// Handles compiling the user's script into HLSL and Verse
	FReply OnCompileClicked();
	
	// Simulation, Lattice, and Cloud params for the UI panels
	Flight::Swarm::FSwarmSimulationParams SimParams;
	Flight::Swarm::FSwarmLatticeParams LatticeParams;
	Flight::Swarm::FSwarmCloudParams CloudParams;

	TSharedPtr<class SReflectedStatePanel> SimParamsPanel;
	TSharedPtr<class SReflectedStatePanel> LatticeParamsPanel;
	TSharedPtr<class SReflectedStatePanel> CloudParamsPanel;

	// Helper to build replacement maps from schema
	void BuildVexReplacementMaps(
		TMap<FString, FString>& OutHlslMap, 
		TMap<FString, FString>& OutVerseMap,
		TArray<Flight::Vex::FVexSymbolDefinition>& OutDefinitions);

	// Helper to build HLSL include from reflection
	FString BuildReflectedTypeInclude(uint32& OutLayoutHash);

	// Helper to validate the contract between CPU and GPU
	bool ValidateReflectedLayoutContract(const FString& InSource, uint32 InExpectedHash);

	// Parsing logic
	FString ParseVexToHLSL(const FString& InVexCode);
	FString ParseVexToVerse(const FString& InVexCode);
};
