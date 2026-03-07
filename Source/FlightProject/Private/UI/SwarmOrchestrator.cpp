// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "UI/SwarmOrchestrator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "UI/FlightReflectedUI.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "SwarmOrchestrator"

void SSwarmOrchestrator::Construct(const FArguments& InArgs)
{
	FString DefaultScript = TEXT("// Flight VEX Interpreter\n")
							TEXT("// Variables: @position, @velocity, @shield, @status\n\n")
							TEXT("float3 noise = curlnoise(@position * 0.01);\n")
							TEXT("@velocity += noise * 50.0 * DeltaTime;\n\n")
							TEXT("if (@shield < 0.2)\n")
							TEXT("{\n")
							TEXT("    @status = 1; // Infected\n")
							TEXT("}\n");

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		
		// Left Panel: The Code Editor
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(5)
			[
				SNew(STextBlock).Text(LOCTEXT("VexEditorTitle", "VEX Source (Houdini-style)"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(5)
			[
				SAssignNew(CodeEditor, SMultiLineEditableTextBox)
				.Text(FText::FromString(DefaultScript))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(5)
			[
				SNew(SButton).Text(LOCTEXT("CompileBtn", "Update Previews"))
				.OnClicked(this, &SSwarmOrchestrator::OnCompileClicked)
			]
		]

		// Right Panel: The Previews (HLSL vs Verse)
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			SNew(SVerticalBox)
			
			// HLSL Preview (GPU)
			+ SVerticalBox::Slot().FillHeight(0.5f).Padding(5)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[ SNew(STextBlock).Text(LOCTEXT("HLSLTitle", "Generated HLSL (GPU Path)")) ]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(HLSLPreview, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				]
			]

			// Verse Preview (Logic)
			+ SVerticalBox::Slot().FillHeight(0.5f).Padding(5)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[ SNew(STextBlock).Text(LOCTEXT("VerseTitle", "Generated Verse (Functional VM Path)")) ]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(VersePreview, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				]
			]
		]
	];

	// Initial parse
	OnCompileClicked();
}

FReply SSwarmOrchestrator::OnCompileClicked()
{
	if (CodeEditor.IsValid())
	{
		FString Source = CodeEditor->GetText().ToString();
		FString GeneratedHLSL = ParseVexToHLSL(Source);
		
		HLSLPreview->SetText(FText::FromString(GeneratedHLSL));
		VersePreview->SetText(FText::FromString(ParseVexToVerse(Source)));

		// Write to the injected USH file
		FString ShaderPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders/Private/Generated/OrchestratorInjected.ush"));
		FFileHelper::SaveStringToFile(GeneratedHLSL, *ShaderPath);

		// Trigger Unreal shader recompile
		if (GEngine)
		{
			// Enable shader development mode if not already on
			GEngine->Exec(nullptr, TEXT("r.ShaderDevelopmentMode 1"));
			// Recompile changed shaders (this will pick up our .ush change)
			GEngine->Exec(nullptr, TEXT("recompileshaders changed"));
			
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("Swarm Orchestrator: HLSL Injected and Recompile Triggered"));
		}
	}
	return FReply::Handled();
}

FString SSwarmOrchestrator::ParseVexToHLSL(const FString& InVexCode)
{
	FString HLSL = InVexCode;
	// Map VEX attributes to our local USF variables in ForceMain
	HLSL.ReplaceInline(TEXT("@position"), TEXT("PosI"));
	HLSL.ReplaceInline(TEXT("@velocity"), TEXT("VelI"));
	HLSL.ReplaceInline(TEXT("@shield"), TEXT("ShieldI"));
	HLSL.ReplaceInline(TEXT("@status"), TEXT("StatusI"));
	return HLSL;
}

FString SSwarmOrchestrator::ParseVexToVerse(const FString& InVexCode)
{
	FString Verse = InVexCode;
	
	// Convert to Verse-style functional syntax
	Verse.ReplaceInline(TEXT("{"), TEXT(":"));
	Verse.ReplaceInline(TEXT("}"), TEXT(""));
	Verse.ReplaceInline(TEXT("if"), TEXT("if"));
	Verse.ReplaceInline(TEXT("@position"), TEXT("Droid.Position"));
	Verse.ReplaceInline(TEXT("@velocity"), TEXT("Droid.Velocity"));
	Verse.ReplaceInline(TEXT("@shield"), TEXT("Droid.Shield"));
	Verse.ReplaceInline(TEXT("@status"), TEXT("Droid.Status"));
	
	// Add Verse function wrapper boilerplate
	return FString::Printf(TEXT("ProcessDroid(Droid:droid_state)<transacts>:void =\n    %s"), *Verse.Replace(TEXT("\n"), TEXT("\n    ")));
}

#undef LOCTEXT_NAMESPACE
