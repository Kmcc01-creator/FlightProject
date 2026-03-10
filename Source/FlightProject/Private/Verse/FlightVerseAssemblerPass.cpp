// Copyright Kelly Rey Wilson. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMBytecodeEmitter.h"
#include "VerseVM/VVMUniqueString.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMFalse.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/Expression.h"
#endif

namespace Flight::Verse
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
TMap<FString, Verse::TWriteBarrier<Verse::VProcedure>> FFlightVerseAssemblerPass::CompiledProcedures;

Verse::VProcedure* FFlightVerseAssemblerPass::GetCompiledProcedure(const FString& DecoratedName)
{
	if (const auto* Procedure = CompiledProcedures.Find(DecoratedName))
	{
		return Procedure->Get();
	}
	return nullptr;
}

void FFlightVerseAssemblerPass::ClearRegistry()
{
	CompiledProcedures.Empty();
}

namespace
{
	/** Minimal Phase B visitor for emitting bytecode from uLang IR */
	struct FBytecodeAssemblerVisitor
	{
		Verse::FAllocationContext AllocContext;
		Verse::FOpEmitter& Emitter;

		FBytecodeAssemblerVisitor(Verse::FAllocationContext InAllocContext, Verse::FOpEmitter& InEmitter)
			: AllocContext(InAllocContext), Emitter(InEmitter)
		{}

		Verse::FValueOperand EmitExpression(const uLang::CExpressionBase* Expression)
		{
			if (!Expression) return Verse::FValueOperand(Emitter.AllocateConstant(Verse::GlobalFalse()));

			const uLang::EAstNodeType Type = Expression->GetNodeType();

			// 1. Literal Handler
			if (Type == uLang::EAstNodeType::Expression_Literal)
			{
				const auto* Literal = static_cast<const uLang::CExprLiteral*>(Expression);
				const uLang::CTypeBase* ValueType = Literal->GetValueType();
				const uLang::ETypeKind Kind = ValueType->GetKind();

				Verse::VValue Value = Verse::GlobalFalse();
				if (Kind == uLang::ETypeKind::Int)
				{
					Value = Verse::VValue::FromInt32(static_cast<int32>(Literal->GetValueAsInt().GetSafeValue()));
				}
				else if (Kind == uLang::ETypeKind::Float)
				{
					Value = Verse::VValue::FromFloat(static_cast<float>(Literal->GetValueAsFloat()));
				}
				else if (Kind == uLang::ETypeKind::Logic)
				{
					Value = Verse::VValue::FromBool(Literal->GetValueAsBool());
				}

				return Verse::FValueOperand(Emitter.AllocateConstant(Value));
			}

			// 2. Block/Sequence Handler (Minimal Phase B)
			if (Type == uLang::EAstNodeType::Expression_Block)
			{
				const auto* Block = static_cast<const uLang::CExprBlock*>(Expression);
				Verse::FValueOperand LastOperand = Verse::FValueOperand(Emitter.AllocateConstant(Verse::GlobalFalse()));
				for (const uLang::TSRef<uLang::CExpressionBase>& Element : Block->Elements())
				{
					LastOperand = EmitExpression(&Element.Get());
				}
				return LastOperand;
			}

			// Unsupported fallback
			return Verse::FValueOperand(Emitter.AllocateConstant(Verse::GlobalFalse()));
		}
	};
}

void FFlightVerseAssemblerPass::TranslateExpressions(
	const uLang::TSRef<uLang::CSemanticProgram>& SemanticResult,
	const uLang::SBuildContext& BuildContext,
	const uLang::SProgramContext& ProgramContext) const
{
	(void)BuildContext;
	(void)ProgramContext;

	// In Phase B, we iterate through the IR Project and emit bytecode for every function.
	const uLang::TSRef<uLang::CAstProject>& IrProject = SemanticResult->GetIrProject();
	for (const auto& CompilationUnit : IrProject->OrderedCompilationUnits())
	{
		for (const auto& Package : CompilationUnit->Packages())
		{
			// Need a way to find all functions in a package. 
			// For Phase B, we'll scan the top-level definitions if they are functions.
		}
	}

	// HACK: For Phase B, we scan the entire program's symbol table for functions.
	// This is slightly inefficient but ensures we find our VEX-generated behaviors.
	Verse::FRunningContext* CurrentVerseContext = static_cast<Verse::FRunningContext*>(FAsyncTaskNotification::GetThreadContext());
	if (!CurrentVerseContext)
	{
		return;
	}

	Verse::FAllocationContext AllocContext(*CurrentVerseContext);

	// Instead of a global scan, we'll look for specific module paths we know we generate.
	uLang::CModule* GeneratedModule = SemanticResult->FindDefinitionByVersePath<uLang::CModule>("/FlightProjectGenerated");
	if (!GeneratedModule) return;

	// Visit all definitions in the module
	GeneratedModule->VisitDefinitionsLambda([&](uLang::CDefinition& Definition)
	{
		if (Definition.GetKind() == uLang::CDefinition::EKind::Function)
		{
			uLang::CFunction& Function = static_cast<uLang::CFunction&>(Definition);
			const uLang::CExpressionBase* Body = Function.GetBodyIr();
			if (!Body) return;

			const FString DecoratedName = UTF8_TO_TCHAR(Function.GetDecoratedName().AsCString());
			Verse::VUniqueString& VmName = Verse::VUniqueString::New(AllocContext, TCHAR_TO_UTF8(*DecoratedName));
			Verse::VUniqueString& VmPath = Verse::VUniqueString::New(AllocContext, "Behavior.generated.verse");

			// Initialize emitter for this function
			Verse::FOpEmitter Emitter(AllocContext, VmPath, VmName, 0, 0);
			FBytecodeAssemblerVisitor Visitor(AllocContext, Emitter);

			// Emit the body and a return
			Verse::FValueOperand Result = Visitor.EmitExpression(Body);
			Emitter.Return(Verse::EmptyLocation(), Result);

			// Finalize and register
			Verse::VProcedure& Procedure = Emitter.MakeProcedure(AllocContext);
			CompiledProcedures.Add(DecoratedName, Verse::TWriteBarrier<Verse::VProcedure>(AllocContext, &Procedure));

			UE_LOG(LogFlightProject, Log, TEXT("Assembled VerseVM bytecode for behavior: %s"), *DecoratedName);
		}
	});
}

uLang::ELinkerResult FFlightVerseAssemblerPass::Link(
	const uLang::SBuildContext& BuildContext,
	const uLang::SProgramContext& ProgramContext) const
{
	(void)BuildContext;
	(void)ProgramContext;
	return uLang::ELinkerResult::Link_Success;
}
#endif
} // namespace Flight::Verse
