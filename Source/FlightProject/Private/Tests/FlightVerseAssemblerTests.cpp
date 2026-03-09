// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "uLang/CompilerPasses/IAssemblerPass.h"
#include "uLang/Toolchain/ModularFeatureManager.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVerseBytecodeLiteralTest,
	"FlightProject.Verse.Bytecode.Literal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVerseBytecodeLiteralTest::RunTest(const FString& Parameters)
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	using namespace Flight::Verse;

	// Use a dummy behavior ID for testing
	const uint32 TestBehaviorID = 7777;
	const FString TestSource = TEXT("Behavior_7777() : int = { return 42 }");

	// We need a subsystem to run the build context
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			UFlightVerseSubsystem* Subsystem = Context.World()->GetSubsystem<UFlightVerseSubsystem>();
			if (!Subsystem) continue;

			FString Errors;
			UFlightVerseSubsystem::FVerseBehavior Behavior;
			
			// TryCreateVmProcedure will trigger the assembler
			// We manually validate the source first
			uLang::TSRef<uLang::CDiagnostics> Diagnostics = uLang::TSRef<uLang::CDiagnostics>::New();
			uLang::SBuildManagerParams BuildManagerParams;
			uLang::CProgramBuildManager BuildManager(BuildManagerParams);
			BuildManager.AddSourceSnippet(
				uLang::TSRef<uLang::CSourceDataSnippet>::New(
					uLang::CUTF8String("Test.verse"), 
					uLang::CUTF8String(TCHAR_TO_UTF8(*TestSource))),
				uLang::CUTF8String("FlightProjectGenerated"),
				uLang::CUTF8String("/FlightProjectGenerated"));

			uLang::SBuildParams BuildParams;
			BuildParams._TargetVM = uLang::SBuildParams::EWhichVM::VerseVM;
			
			// Run build inside EnterVM so assembler can allocate
			Subsystem->VerseContext.EnterVM([&]() {
				FFlightVerseAssemblerPass::ClearRegistry();
				BuildManager.Build(BuildParams, Diagnostics);
			});

			const FString DecoratedName = TEXT("(/FlightProjectGenerated:)Behavior_7777");
			Verse::VProcedure* Procedure = FFlightVerseAssemblerPass::GetCompiledProcedure(DecoratedName);
			
			TestNotNull(TEXT("Assembler should have produced a VProcedure"), Procedure);
			if (Procedure)
			{
				TestFalse(TEXT("Should NOT use VM entrypoint (thunk) for assembled procedure"), Behavior.bUsesVmEntryPoint);
				
				// Execute and verify the return value
				Verse::FOpResult Result(Verse::FOpResult::Error);
				Subsystem->VerseContext.EnterVM([&]() {
					Verse::FAllocationContext AllocContext(Subsystem->VerseContext);
					Verse::VFunction& Function = Verse::VFunction::New(AllocContext, *Procedure, Verse::VValue(Subsystem));
					Result = Function.Invoke(Subsystem->VerseContext, Verse::VFunction::Args());
				});

				TestTrue(TEXT("Execution should return a value"), Result.IsReturn());
				if (Result.IsReturn())
				{
					TestTrue(TEXT("Return value should be an int"), Result.Value.IsInt32());
					TestEqual(TEXT("Return value should be 42"), Result.Value.AsInt32(), 42);
				}
			}

			return true;
		}
	}
#endif

	return true;
}
