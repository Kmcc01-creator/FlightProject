// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Swarm/FlightSwarmSubsystem.h"

class FLIGHTPROJECT_API SSwarmOrchestrator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSwarmOrchestrator) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// The VEX-like code editor
	TSharedPtr<SMultiLineEditableTextBox> CodeEditor;

	// Previews
	TSharedPtr<SMultiLineEditableTextBox> HLSLPreview;
	TSharedPtr<SMultiLineEditableTextBox> VersePreview;
	
	// Handles compiling the user's script into HLSL and Verse
	FReply OnCompileClicked();
	
	// Parsing logic
	FString ParseVexToHLSL(const FString& InVexCode);
	FString ParseVexToVerse(const FString& InVexCode);
};
