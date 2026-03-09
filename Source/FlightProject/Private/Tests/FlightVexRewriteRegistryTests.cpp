// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vex/FlightVexOptics.h"
#include "Vex/FlightVexRewriteRegistry.h"
#include "Vex/FlightVexTreeTraits.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexAstRewriteRegistryDefaultRulesTest,
	"FlightProject.Vex.RewriteRegistry.AST.DefaultRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVexAstRewriteRegistryDefaultRulesTest::RunTest(const FString&)
{
	using namespace Flight::Vex;

	FVexProgramAst Program;
	Program.Expressions.SetNum(3);

	Program.Expressions[0].Kind = EVexExprKind::NumberLiteral;
	Program.Expressions[0].Lexeme = TEXT("1.0");
	Program.Expressions[0].InferredType = TEXT("float");

	Program.Expressions[1].Kind = EVexExprKind::NumberLiteral;
	Program.Expressions[1].Lexeme = TEXT("2.0");
	Program.Expressions[1].InferredType = TEXT("float");

	Program.Expressions[2].Kind = EVexExprKind::BinaryOp;
	Program.Expressions[2].Lexeme = TEXT("+");
	Program.Expressions[2].LeftNodeIndex = 0;
	Program.Expressions[2].RightNodeIndex = 1;
	Program.Expressions[2].InferredType = TEXT("float");

	FVexStatementAst Statement;
	Statement.Kind = EVexStatementKind::Expression;
	Statement.ExpressionNodeIndex = 2;
	Program.Statements.Add(Statement);

	const FVexAstRewriteRegistry Registry = FVexAstRewriteRegistry::MakeDefault();
	TestTrue(TEXT("Default AST rewrite registry should include at least one rule"), Registry.Num() > 0);

	FVexExpressionAst Replacement;
	const bool bMatched = Registry.ApplyFirstMatch(
		Program.Expressions[2],
		Program,
		EVexRewriteDomain::Core,
		Replacement,
		nullptr);

	TestTrue(TEXT("Default rule set should fold numeric binary expression"), bMatched);
	TestEqual(TEXT("Replacement should be number literal"), Replacement.Kind, EVexExprKind::NumberLiteral);
	TestTrue(TEXT("Replacement value should be approximately 3.0"), FMath::IsNearlyEqual(FCString::Atof(*Replacement.Lexeme), 3.0f));

	FVexOptimizer::Optimize(Program);
	const FVexExpressionAst& Root = Program.Expressions[2];
	TestEqual(TEXT("Optimizer should rewrite expression root to literal"), Root.Kind, EVexExprKind::NumberLiteral);
	TestTrue(TEXT("Optimizer result should be approximately 3.0"), FMath::IsNearlyEqual(FCString::Atof(*Root.Lexeme), 3.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVexIrTreeTraitsPostOrderTest,
	"FlightProject.Vex.TreeTraits.IR.PostOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVexIrTreeTraitsPostOrderTest::RunTest(const FString&)
{
	using namespace Flight::Vex;

	FVexIrProgram Program;
	Program.Constants.Add(2.0f);
	Program.SymbolNames.Add(TEXT("@shield"));

	FVexIrInstruction LoadSymbol;
	LoadSymbol.Op = EVexIrOp::LoadSymbol;
	LoadSymbol.Dest.Kind = FVexIrValue::EKind::Register;
	LoadSymbol.Dest.Type = EVexValueType::Float;
	LoadSymbol.Dest.Index = 0;
	FVexIrValue ShieldSymbol;
	ShieldSymbol.Kind = FVexIrValue::EKind::Symbol;
	ShieldSymbol.Type = EVexValueType::Float;
	ShieldSymbol.Index = 0;
	LoadSymbol.Args.Add(ShieldSymbol);

	FVexIrInstruction Add;
	Add.Op = EVexIrOp::Add;
	Add.Dest.Kind = FVexIrValue::EKind::Register;
	Add.Dest.Type = EVexValueType::Float;
	Add.Dest.Index = 1;
	FVexIrValue Reg0;
	Reg0.Kind = FVexIrValue::EKind::Register;
	Reg0.Type = EVexValueType::Float;
	Reg0.Index = 0;
	FVexIrValue Const2;
	Const2.Kind = FVexIrValue::EKind::Constant;
	Const2.Type = EVexValueType::Float;
	Const2.Index = 0;
	Add.Args.Add(Reg0);
	Add.Args.Add(Const2);

	FVexIrInstruction StoreShield;
	StoreShield.Op = EVexIrOp::StoreSymbol;
	StoreShield.Dest.Kind = FVexIrValue::EKind::Symbol;
	StoreShield.Dest.Type = EVexValueType::Float;
	StoreShield.Dest.Index = 0;
	FVexIrValue Reg1;
	Reg1.Kind = FVexIrValue::EKind::Register;
	Reg1.Type = EVexValueType::Float;
	Reg1.Index = 1;
	StoreShield.Args.Add(Reg1);

	Program.Instructions.Add(LoadSymbol);
	Program.Instructions.Add(Add);
	Program.Instructions.Add(StoreShield);

	const FVexIrTreeView Tree(Program);
	TArray<int32> Order;
	TraversePostOrder(Tree, [&](const int32 InstructionIndex)
	{
		Order.Add(InstructionIndex);
	});

	TestEqual(TEXT("Expected dependency traversal to visit 3 instructions"), Order.Num(), 3);
	if (Order.Num() == 3)
	{
		TestEqual(TEXT("Load should execute before Add"), Order[0], 0);
		TestEqual(TEXT("Add should execute before Store"), Order[1], 1);
		TestEqual(TEXT("Store should be the root visitation"), Order[2], 2);
	}
	else
	{
		AddError(TEXT("Unexpected traversal order length; cannot validate expected instruction ordering."));
		return false;
	}

	return true;
}
