// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/FlightHlslReflection.h"
#include "Core/FlightReflection.h"
#include "Vex/FlightVexSymbolRegistry.h"

#if WITH_AUTOMATION_TESTS

using namespace Flight::Reflection;

namespace Flight::Reflection::ComplexTest
{

struct FMessageHeader
{
    int32 Id = 0;
    int32 RuntimeScratch = 0;

    FLIGHT_REFLECT_BODY(FMessageHeader);

    bool operator==(const FMessageHeader& Other) const
    {
        return Id == Other.Id && RuntimeScratch == Other.RuntimeScratch;
    }
};

struct FMessageEnvelope
{
    FMessageHeader Header;
    FString Payload;
    int32 Sequence = 0;

    FLIGHT_REFLECT_BODY(FMessageEnvelope);

    bool operator==(const FMessageEnvelope& Other) const
    {
        return Header == Other.Header
            && Payload == Other.Payload
            && Sequence == Other.Sequence;
    }
};

struct FShaderContractData
{
    float Temperature = 0.0f;
    bool bEnabled = true;

    FLIGHT_REFLECT_BODY(FShaderContractData);
};

struct FGpuContractData
{
    FVector4f Position = FVector4f::Zero();
    float Weight = 0.0f;

    FLIGHT_REFLECT_BODY(FGpuContractData);
};

} // namespace Flight::Reflection::ComplexTest

namespace Flight::Reflection
{

using namespace Flight::Reflection::Attr;
using namespace Flight::Reflection::ComplexTest;

FLIGHT_REFLECT_FIELDS_ATTR(FMessageHeader,
    FLIGHT_FIELD_ATTR(int32, Id, EditAnywhere),
    FLIGHT_FIELD_ATTR(int32, RuntimeScratch, Transient)
)

FLIGHT_REFLECT_FIELDS_ATTR(FMessageEnvelope,
    FLIGHT_FIELD_ATTR(FMessageHeader, Header, EditAnywhere),
    FLIGHT_FIELD_ATTR(FString, Payload, EditAnywhere),
    FLIGHT_FIELD_ATTR(int32, Sequence, EditAnywhere)
)

FLIGHT_REFLECT_FIELDS_ATTR(FShaderContractData,
    FLIGHT_FIELD_ATTR(
        float,
        Temperature,
        EditAnywhere,
        VexSymbol<"@temperature">,
        HlslIdentifier<"temperature_local">,
        VerseIdentifier<"temperature">,
        VexResidency<EFlightVexSymbolResidency::Shared>,
        ThreadAffinity<EFlightVexSymbolAffinity::Any>,
        SimdReadAllowed<true>,
        SimdWriteAllowed<false>,
        GpuTier1Allowed<true>,
        VexAlignment<EFlightVexAlignmentRequirement::Align16>,
        MathDeterminism<EFlightVexMathDeterminismProfile::Precise>
    ),
    FLIGHT_FIELD_ATTR(
        bool,
        bEnabled,
        EditAnywhere,
        VexSymbol<"@enabled">,
        HlslIdentifier<"enabled_flag">,
        VerseIdentifier<"enabled">,
        VexResidency<EFlightVexSymbolResidency::CpuOnly>,
        ThreadAffinity<EFlightVexSymbolAffinity::GameThread>
    )
)

using FGpuContractDataReflectAttrs = ::Flight::Reflection::TAttributeSet<
    ::Flight::Reflection::Attr::GpuResourceId<"Reflection.TestBuffer">,
    ::Flight::Reflection::Attr::GpuResourceKind<EFlightGpuResourceKind::StorageBuffer>,
    ::Flight::Reflection::Attr::GpuResourceLifetime<EFlightGpuResourceLifetime::Persistent>,
    ::Flight::Reflection::Attr::GpuBindingName<"ReflectionTestBuffer">,
    ::Flight::Reflection::Attr::PreferUnrealRdg<true>,
    ::Flight::Reflection::Attr::RawVulkanInteropRequired<false>,
    ::Flight::Reflection::Attr::GpuAccessRule<EFlightGpuExecutionDomain::RenderGraph, EFlightGpuAccessClass::TransferDestination>,
    ::Flight::Reflection::Attr::GpuAccessRule<EFlightGpuExecutionDomain::GpuCompute, EFlightGpuAccessClass::ShaderRead>,
    ::Flight::Reflection::Attr::GpuAccessRule<EFlightGpuExecutionDomain::GpuCompute, EFlightGpuAccessClass::ShaderWrite>
>;

FLIGHT_REFLECT_FIELDS_VEX(
    FGpuContractData,
    FGpuContractDataReflectAttrs,
    FLIGHT_FIELD_ATTR(FVector4f, Position, EditAnywhere),
    FLIGHT_FIELD_ATTR(float, Weight, EditAnywhere)
)

} // namespace Flight::Reflection

static_assert(CReflectable<const Flight::Reflection::ComplexTest::FMessageEnvelope&>);
static_assert(CHasFields<const Flight::Reflection::ComplexTest::FMessageEnvelope&>);

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FFlightReflectionComplexTest,
    "FlightProject.Reflection.Complex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightReflectionComplexTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("CvRefNormalization"));
    OutTestCommands.Add(TEXT("CvRefNormalization"));

    OutBeautifiedNames.Add(TEXT("NestedSerializationPolicy"));
    OutTestCommands.Add(TEXT("NestedSerializationPolicy"));

    OutBeautifiedNames.Add(TEXT("VisitorTraversal"));
    OutTestCommands.Add(TEXT("VisitorTraversal"));

    OutBeautifiedNames.Add(TEXT("NestedDiffApplyPolicy"));
    OutTestCommands.Add(TEXT("NestedDiffApplyPolicy"));

    OutBeautifiedNames.Add(TEXT("CodegenContract"));
    OutTestCommands.Add(TEXT("CodegenContract"));

    OutBeautifiedNames.Add(TEXT("GpuResourceContract"));
    OutTestCommands.Add(TEXT("GpuResourceContract"));
}

bool FFlightReflectionComplexTest::RunTest(const FString& Parameters)
{
    using namespace Flight::Reflection::ComplexTest;

    if (Parameters == TEXT("CvRefNormalization"))
    {
        TestTrue("Const-ref types should remain reflectable", CReflectable<const FMessageEnvelope&>);
        TestTrue("Const-ref types should still expose fields", CHasFields<const FMessageEnvelope&>);
        TestEqual("Trait normalization should preserve the field count", static_cast<int32>(TReflectTraits<const FMessageEnvelope&>::Fields::Count), 3);

        FMessageEnvelope Envelope;
        Envelope.Header.Id = 7;
        Envelope.Header.RuntimeScratch = 99;
        Envelope.Payload = TEXT("Ping");
        Envelope.Sequence = 3;

        TArray<FString> FieldNames;
        ForEachField(Envelope, [&FieldNames](const auto&, const char* Name)
        {
            FieldNames.Add(UTF8_TO_TCHAR(Name));
        });

        TestEqual("Const-ref iteration should visit the top-level fields", FieldNames.Num(), 3);
        TestEqual("Top-level field order should remain stable", FieldNames[0], TEXT("Header"));
        TestEqual("Top-level field order should remain stable", FieldNames[1], TEXT("Payload"));
        TestEqual("Top-level field order should remain stable", FieldNames[2], TEXT("Sequence"));
    }
    else if (Parameters == TEXT("NestedSerializationPolicy"))
    {
        FMessageEnvelope Original;
        Original.Header.Id = 17;
        Original.Header.RuntimeScratch = 42;
        Original.Payload = TEXT("Telemetry");
        Original.Sequence = 8;

        TArray<uint8> Buffer;
        FMemoryWriter Writer(Buffer);
        Serialize(Original, Writer);

        FMessageEnvelope Loaded;
        Loaded.Header.Id = -1;
        Loaded.Header.RuntimeScratch = 1234;
        Loaded.Payload = TEXT("Unset");
        Loaded.Sequence = -1;

        FMemoryReader Reader(Buffer);
        Serialize(Loaded, Reader);

        TestEqual("Nested persistent field should serialize", Loaded.Header.Id, 17);
        TestEqual("Nested transient field should not serialize", Loaded.Header.RuntimeScratch, 1234);
        TestEqual("Payload should serialize", Loaded.Payload, TEXT("Telemetry"));
        TestEqual("Sequence should serialize", Loaded.Sequence, 8);
    }
    else if (Parameters == TEXT("VisitorTraversal"))
    {
        FMessageEnvelope Envelope;
        Envelope.Header.Id = 5;
        Envelope.Header.RuntimeScratch = 55;
        Envelope.Payload = TEXT("Visitor");
        Envelope.Sequence = 1;

        TArray<FString> Names;
        ForEachFieldDeep(Envelope, [&Names](const auto&, const char* Name)
        {
            Names.Add(UTF8_TO_TCHAR(Name));
        });

        TestEqual("Deep traversal should visit both top-level and nested fields", Names.Num(), 5);
        TestTrue("Deep traversal should include the nested field container", Names.Contains(TEXT("Header")));
        TestTrue("Deep traversal should include nested persistent fields", Names.Contains(TEXT("Id")));
        TestTrue("Deep traversal should include nested transient fields", Names.Contains(TEXT("RuntimeScratch")));
        TestTrue("Deep traversal should include sibling scalar fields", Names.Contains(TEXT("Payload")));
        TestTrue("Deep traversal should include sibling scalar fields", Names.Contains(TEXT("Sequence")));
    }
    else if (Parameters == TEXT("NestedDiffApplyPolicy"))
    {
        FMessageEnvelope Before;
        Before.Header.Id = 1;
        Before.Header.RuntimeScratch = 10;
        Before.Payload = TEXT("Alpha");
        Before.Sequence = 100;

        FMessageEnvelope After = Before;
        After.Header.Id = 2;
        After.Header.RuntimeScratch = 900;
        After.Payload = TEXT("Beta");

        const TStructPatch<FMessageEnvelope> Patch = Diff(Before, After);
        TestEqual("Patch should include the changed nested field and changed scalar field only", Patch.ChangedFields.Num(), 2);

        FMessageEnvelope Target = Before;
        Apply(Target, Patch);

        TestEqual("Nested persistent state should patch through reflection serialization", Target.Header.Id, 2);
        TestEqual("Nested transient state should remain owned by the target instance", Target.Header.RuntimeScratch, 10);
        TestEqual("Changed scalar field should patch", Target.Payload, TEXT("Beta"));
        TestEqual("Unchanged scalar field should remain stable", Target.Sequence, 100);
    }
    else if (Parameters == TEXT("CodegenContract"))
    {
        const FString Hlsl = HLSL::GenerateStructHLSL<const FShaderContractData&>(TEXT("FGeneratedShaderContract"));
        TestTrue("Generated HLSL should include the float field", Hlsl.Contains(TEXT("float Temperature;")));
        TestTrue("Generated HLSL should include the bool field", Hlsl.Contains(TEXT("bool bEnabled;")));

        const FString Contract = HLSL::GenerateStructContractHLSL<FShaderContractData>(TEXT("FGeneratedShaderContract"), TEXT("FLIGHT_SHADER_CONTRACT"));
        uint32 ExtractedHash = 0;
        TestTrue("Generated contract HLSL should contain an extractable layout hash", HLSL::ExtractLayoutHashFromContract(Contract, TEXT("FLIGHT_SHADER_CONTRACT"), ExtractedHash));
        TestEqual("Extracted hash should match the computed layout hash", ExtractedHash, HLSL::ComputeLayoutHash<FShaderContractData>());

        const TArray<FFlightVexSymbolRow> Rows = HLSL::GenerateVexSymbolsFromStruct<FShaderContractData>(TEXT("ReflectionComplexTest"));
        TestEqual("Codegen should emit one schema row per VEX-annotated field", Rows.Num(), 2);
        if (Rows.Num() == 2)
        {
            TestEqual("Temperature symbol should preserve the authored symbol name", Rows[0].SymbolName, TEXT("@temperature"));
            TestEqual("Temperature symbol should preserve the authored HLSL identifier", Rows[0].HlslIdentifier, TEXT("temperature_local"));
            TestEqual("Temperature symbol should preserve residency policy", Rows[0].Residency, EFlightVexSymbolResidency::Shared);
            TestEqual("Temperature symbol should preserve affinity policy", Rows[0].Affinity, EFlightVexSymbolAffinity::Any);
            TestFalse("Temperature symbol should preserve SIMD write restrictions", Rows[0].bSimdWriteAllowed);
            TestEqual("Enabled symbol should preserve CPU residency", Rows[1].Residency, EFlightVexSymbolResidency::CpuOnly);
            TestEqual("Enabled symbol should preserve game-thread affinity", Rows[1].Affinity, EFlightVexSymbolAffinity::GameThread);
        }
    }
    else if (Parameters == TEXT("GpuResourceContract"))
    {
        const TOptional<FFlightGpuResourceContractRow> Contract =
            HLSL::GenerateGpuResourceContractFromStruct<FGpuContractData>(TEXT("ReflectionComplexTest"));
        TestTrue("GPU resource contract should be generated for attributed types", Contract.IsSet());
        if (Contract.IsSet())
        {
            TestEqual("Resource contract should preserve the authored resource id", Contract->ResourceId, TEXT("Reflection.TestBuffer"));
            TestEqual("Resource contract should preserve the authored resource kind", Contract->ResourceKind, EFlightGpuResourceKind::StorageBuffer);
            TestEqual("Resource contract should preserve the authored binding name", Contract->BindingName, TEXT("ReflectionTestBuffer"));
            TestEqual("Resource contract should preserve stride", Contract->ElementStrideBytes, static_cast<int32>(sizeof(FGpuContractData)));
            TestEqual("Resource contract should preserve layout hash", static_cast<uint32>(Contract->LayoutHash), HLSL::ComputeLayoutHash<FGpuContractData>());
        }

        const TArray<FFlightGpuAccessRuleRow> AccessRules =
            HLSL::GenerateGpuAccessRulesFromStruct<FGpuContractData>(TEXT("ReflectionComplexTest"));
        TestEqual("GPU access rules should be emitted for every authored access attribute", AccessRules.Num(), 3);

        bool bFoundTransferDestination = false;
        bool bFoundShaderRead = false;
        bool bFoundShaderWrite = false;
        for (const FFlightGpuAccessRuleRow& Rule : AccessRules)
        {
            bFoundTransferDestination |= Rule.ExecutionDomain == EFlightGpuExecutionDomain::RenderGraph
                && Rule.AccessClass == EFlightGpuAccessClass::TransferDestination;
            bFoundShaderRead |= Rule.ExecutionDomain == EFlightGpuExecutionDomain::GpuCompute
                && Rule.AccessClass == EFlightGpuAccessClass::ShaderRead;
            bFoundShaderWrite |= Rule.ExecutionDomain == EFlightGpuExecutionDomain::GpuCompute
                && Rule.AccessClass == EFlightGpuAccessClass::ShaderWrite;
        }

        TestTrue("GPU access rules should preserve transfer destination access", bFoundTransferDestination);
        TestTrue("GPU access rules should preserve shader read access", bFoundShaderRead);
        TestTrue("GPU access rules should preserve shader write access", bFoundShaderWrite);

        const FString Include = HLSL::GenerateGpuResourceContractInclude<FGpuContractData>(TEXT("REFLECTION_TEST_BUFFER"));
        TestTrue("GPU resource include should emit a layout hash macro", Include.Contains(TEXT("#define REFLECTION_TEST_BUFFER_RESOURCE_LAYOUT_HASH")));
        TestTrue("GPU resource include should emit an access-rule-count macro", Include.Contains(TEXT("#define REFLECTION_TEST_BUFFER_RESOURCE_ACCESS_RULE_COUNT 3")));
        TestTrue("GPU resource include should include the authored binding comment", Include.Contains(TEXT("// BindingName: ReflectionTestBuffer")));
    }

    return true;
}

#endif // WITH_AUTOMATION_TESTS
