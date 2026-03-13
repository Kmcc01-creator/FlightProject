// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightDataTypes.h"
#include "Vex/FlightVexSchema.h"
#include "Vex/FlightVexSchemaIr.h"
#include "Vex/FlightCompileArtifacts.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexOptics.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMVerse.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMVerseNativeTypeDesc.h"
#include "VerseVM/VVMWriteBarrier.h"
#endif

#include "UFlightVerseSubsystem.generated.h"

// Forward declarations in correct namespace
namespace Flight::Vex { class FVexSimdExecutor; }
namespace Flight::Swarm { struct FDroidState; }
struct FMassFragmentHostBundle;
struct FMassFragmentHostView;

UENUM(BlueprintType)
enum class EFlightVerseCompileState : uint8
{
	GeneratedOnly UMETA(DisplayName = "GeneratedOnly"),
	VmCompiled UMETA(DisplayName = "VmCompiled"),
	VmCompileFailed UMETA(DisplayName = "VmCompileFailed")
};

enum class EFlightVerseBehaviorKind : uint8
{
	Atomic,
	Sequence,
	Selector
};

enum class EFlightBehaviorExecutionShape : uint8
{
	Unknown,
	ScalarOnly,
	VectorCapable,
	VectorPreferred,
	ShapeAgnostic
};

enum class EFlightBehaviorGuardComparison : uint8
{
	Less,
	LessEqual,
	Greater,
	GreaterEqual,
	Equal,
	NotEqual,
	IsTrue,
	IsFalse
};

/**
 * UFlightVerseSubsystem
 *
 * Bridges the VEX frontend with the Unreal Verse VM.
 * Handles live-compilation and execution of VEX-generated Verse code.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightVerseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	struct FSelectorBranchGuard
	{
		FString SymbolName;
		EFlightBehaviorGuardComparison Comparison = EFlightBehaviorGuardComparison::IsTrue;
		float ScalarValue = 0.0f;
	};

	struct FCompositeSelectorBranchSpec
	{
		uint32 BehaviorID = 0;
		bool bHasGuard = false;
		FSelectorBranchGuard Guard;
	};

	struct FBehaviorCapabilityEnvelope
	{
		TArray<FString> LegalLanes;
		FString PreferredLane;
		FString CommittedLane;
		EFlightBehaviorExecutionShape ExecutionShape = EFlightBehaviorExecutionShape::Unknown;
		EFlightBehaviorExecutionShape CommittedExecutionShape = EFlightBehaviorExecutionShape::Unknown;
		bool bAllowsMixedLaneExecution = false;
		bool bRequiresSharedTypeKey = false;
		TArray<FString> DisallowedLaneReasons;
	};

	struct FCompilePolicyContext
	{
		FName CohortName = NAME_None;
		FName ProfileName = NAME_None;
		const FFlightBehaviorCompilePolicyRow* ExplicitPolicy = nullptr;
	};

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Garbage Collection integration for Verse VM closures */
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/**
	 * Compiles VEX source for a specific behavior ID.
	 * Returns true when compilation succeeds; selected policy may allow generated-only output without an executable runtime.
	 */
	bool CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, const void* TypeKey, const FCompilePolicyContext& CompilePolicyContext);

	/**
	 * Compiles VEX source for a specific behavior ID.
	 * Returns true when compilation succeeds; selected policy may allow generated-only output without an executable runtime.
	 */
	bool CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, const void* TypeKey);

	/**
	 * Compatibility overload that accepts an optional reflected native struct bridge.
	 * Prefer the explicit type-key overload for generalized schema-driven compilation.
	 */
	bool CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, UScriptStruct* TargetStruct, const FCompilePolicyContext& CompilePolicyContext);

	/**
	 * Compatibility overload that accepts an optional reflected native struct bridge.
	 * Prefer the explicit type-key overload for generalized schema-driven compilation.
	 */
	bool CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, UScriptStruct* TargetStruct = nullptr);

	/**
	 * Executes the currently loaded Verse behavior on a drone state.
	 * Uses Verse VM when available, otherwise native fallback execution.
	 */
	void ExecuteBehavior(uint32 BehaviorID, Flight::Swarm::FDroidState& DroidState);

	/**
	 * Executes a literal Tier 1 script AST directly on memory using schema offsets.
	 * This is the zero-cost path for non-Verse VM execution.
	 */
	void ExecuteOnSchema(const Flight::Vex::FVexProgramAst& Program, void* StructPtr, const Flight::Vex::FVexTypeSchema& Schema);

	/**
	 * Executes a behavior on an arbitrary C++ struct.
	 */
	void ExecuteBehaviorOnStruct(uint32 BehaviorID, void* StructPtr, const void* TypeKey);

	/**
	 * Executes a behavior on multiple entities in bulk.
	 * Optimized path for Tier 1 (Literal) scripts using SIMD where possible.
	 */
	void ExecuteBehaviorBulk(uint32 BehaviorID, TArrayView<Flight::Swarm::FDroidState> DroidStates);

	/**
	 * Executes a behavior directly on Mass fragments.
	 * Bypasses gather/scatter for eligible Tier 1 scripts.
	 */
	void ExecuteBehaviorDirect(
		uint32 BehaviorID,
		TArrayView<struct FFlightTransformFragment> Transforms,
		TArrayView<struct FFlightDroidStateFragment> DroidStates);

	/**
	 * Executes a behavior directly on processor-provided Mass fragment views.
	 * This is the descriptor-based handoff path for chunk-query-owned fragment views.
	 */
	void ExecuteBehaviorDirect(
		uint32 BehaviorID,
		TConstArrayView<struct FMassFragmentHostView> FragmentViews);

	/** Returns compile-state metadata for a behavior ID. */
	EFlightVerseCompileState GetBehaviorCompileState(uint32 BehaviorID) const;

	/** Returns true when a behavior has an executable runtime path. */
	bool HasExecutableBehavior(uint32 BehaviorID) const;

	/** Returns the most recent compile diagnostics string for a behavior ID. */
	FString GetBehaviorCompileDiagnostics(uint32 BehaviorID) const;

	/** Returns the latest compile artifact report for a behavior ID, if available. */
	const Flight::Vex::FFlightCompileArtifactReport* GetBehaviorCompileArtifactReport(uint32 BehaviorID) const;

	/** Returns the latest compile artifact report as JSON for a behavior ID, if available. */
	FString GetBehaviorCompileArtifactReportJson(uint32 BehaviorID) const;

	/** Returns the currently committed direct-execution backend for this behavior and type key. */
	FString DescribeCommittedExecutionBackend(uint32 BehaviorID, const void* TypeKey = nullptr) const;

	/** Returns the resolved bulk-execution backend for this behavior on FDroidState payloads. */
	FString DescribeBulkExecutionBackend(uint32 BehaviorID) const;

	/** Returns the resolved direct Mass/GPU execution backend for this behavior. */
	FString DescribeDirectExecutionBackend(uint32 BehaviorID) const;

	/** Returns the last GPU submission handle recorded for a behavior, if any. */
	int64 GetLastGpuSubmissionHandle(uint32 BehaviorID) const;

	/** Returns the resolved storage host kind used for this behavior and type key. */
	FString DescribeResolvedStorageHost(uint32 BehaviorID, const void* TypeKey = nullptr) const;

	/** Registers a runtime-owned composite sequence behavior over existing child behaviors. */
	bool RegisterCompositeSequenceBehavior(
		uint32 BehaviorID,
		TConstArrayView<uint32> ChildBehaviorIds,
		FString& OutErrors,
		const void* TypeKey = nullptr);

	/** Registers a runtime-owned composite selector behavior over existing child behaviors. */
	bool RegisterCompositeSelectorBehavior(
		uint32 BehaviorID,
		TConstArrayView<uint32> ChildBehaviorIds,
		FString& OutErrors,
		const void* TypeKey = nullptr);

	/** Registers a runtime-owned guarded selector behavior over existing child behaviors. */
	bool RegisterGuardedCompositeSelectorBehavior(
		uint32 BehaviorID,
		TConstArrayView<FCompositeSelectorBranchSpec> BranchSpecs,
		FString& OutErrors,
		const void* TypeKey = nullptr);

	/** Returns a stable text label for a selector guard comparison. */
	static FString SelectorGuardComparisonToString(EFlightBehaviorGuardComparison Comparison);

	/** Returns a report/debug summary of a selector branch guard. */
	static FString DescribeSelectorGuard(const FSelectorBranchGuard& Guard);

	struct FVerseBehavior
	{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		TWriteBarrier<Verse::VProcedure> Procedure;
#endif
		EFlightVerseBehaviorKind Kind = EFlightVerseBehaviorKind::Atomic;
		TArray<uint32> ChildBehaviorIds;
		TArray<FCompositeSelectorBranchSpec> SelectorBranches;
		Flight::Vex::EVexTier Tier = Flight::Vex::EVexTier::Full;
		TSharedPtr<Flight::Vex::FVexSimdExecutor> SimdPlan;
		FString SimdCompileDiagnostics;
		float ExecutionRateHz = 0.0f;
		uint32 FrameInterval = 1;
		bool bIsAsync = false;
		bool bHasExecutableProcedure = false;
		bool bUsesNativeFallback = false;
		bool bUsesVmEntryPoint = false;
		EFlightVerseCompileState CompileState = EFlightVerseCompileState::VmCompileFailed;
		Flight::Vex::FVexProgramAst NativeProgram;
		const void* BoundTypeKey = nullptr;
		FName BoundTypeStableName = NAME_None;
		uint32 BoundSchemaLayoutHash = 0;
		TOptional<Flight::Vex::FVexSchemaBindingResult> SchemaBinding;
		FString GeneratedVerseCode;
		FString LastCompileDiagnostics;
		FString SelectedBackend;
		FString CommittedBackend;
		FString CommitDetail;
		bool bHasLastExecutionResult = false;
		bool bLastExecutionSucceeded = false;
		bool bLastExecutionCommitted = false;
		uint32 LastSelectedChildBehaviorId = 0;
		TArray<FString> LastBranchEvidence;
		TArray<FString> LastGuardOutcomes;
		FString LastExecutionDetail;
		int64 LastGpuSubmissionHandle = 0;
		FString LastGpuSubmissionDetail;
		bool bGpuExecutionPending = false;
		FName SelectedPolicyRowName = NAME_None;
		EFlightBehaviorCompileDomainPreference PolicyPreferredDomain = EFlightBehaviorCompileDomainPreference::Unspecified;
		bool bPolicyAllowsNativeFallback = true;
		bool bPolicyAllowsGeneratedOnly = false;
		bool bPolicyPrefersAsync = false;
		TArray<FName> RequiredContracts;
		TArray<FString> PolicyRequiredSymbols;
		TArray<FString> ImportedSymbols;
		TArray<FString> ExportedSymbols;
		int32 BoundaryOperatorCount = 0;
		bool bHasBoundarySemantics = false;
		bool bBoundarySemanticsExecutable = true;
		bool bHasAwaitableBoundary = false;
		bool bHasMirrorRequest = false;
		FString BoundaryExecutionDetail;
		FBehaviorCapabilityEnvelope CapabilityEnvelope;
		Flight::Vex::FFlightCompileArtifactReport CompileArtifactReport;
		bool bHasCompileArtifactReport = false;
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		TWriteBarrier<Verse::VNativeFunction> VmEntryPoint;
#endif
	};

	/** Map of BehaviorID to compiled Verse behavior */
	TMap<uint32, FVerseBehavior> Behaviors;

private:
	enum class EBehaviorExecutionFailureKind : uint8
	{
		None,
		MissingBehavior,
		NonExecutable,
		IncompatibleTypeKey,
		UnsupportedBackend,
		UnsupportedStorageHost,
		UnknownCompositeOperator,
		ChildExecutionFailed,
		GuardRejected,
		GuardEvaluationFailed
	};

	struct FBehaviorExecutionResult
	{
		bool bSucceeded = false;
		bool bCommitted = false;
		bool bSemanticFailure = false;
		EBehaviorExecutionFailureKind FailureKind = EBehaviorExecutionFailureKind::None;
		FString SelectedLane;
		TArray<FString> LegalLanes;
		FString CommittedLane;
		EFlightBehaviorExecutionShape ExecutionShape = EFlightBehaviorExecutionShape::Unknown;
		EFlightBehaviorExecutionShape CommittedExecutionShape = EFlightBehaviorExecutionShape::Unknown;
		FString Backend;
		FString Detail;
		uint32 SelectedChildBehaviorId = 0;
		TArray<uint32> ExecutedChildBehaviorIds;
		TArray<FString> BranchEvidence;
		TArray<FString> GuardOutcomes;
	};

	enum class EStorageHostKind : uint8
	{
		None,
		SchemaAos,
		SchemaAccessor,
		MassFragments,
		GpuBuffer,
		LegacyDroidState
	};

	struct FResolvedStorageHost
	{
		EStorageHostKind Kind = EStorageHostKind::None;
		const Flight::Vex::FVexTypeSchema* Schema = nullptr;
		const void* TypeKey = nullptr;
		FName TypeName = NAME_None;

		bool IsValid() const
		{
			return Kind != EStorageHostKind::None;
		}
	};

	/** Builds Verse descriptors for all reflected types */
	void RegisterNativeComponents();

	/** Registers built-in VEX functions as native Verse functions */
	void RegisterNativeVerseFunctions();

	/** Registers VEX @symbols to C++ FDroidState fields */
	void RegisterNativeVerseSymbols();

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	/** Core execution logic that assumes the caller has already called EnterVM() */
	void ExecuteBehaviorInContext(uint32 BehaviorID, const FVerseBehavior& Behavior, void* StructPtr, const void* TypeKey, Verse::FAllocationContext& AllocContext);
#endif

	/** A deferred closure representing a behavior execution pending async completion. */
	struct FVexDeferredBehavior
	{
		uint32 BehaviorID;
		void* StatePtr;
		const void* TypeKey;
	};

	/** Registers a behavior for deferred batch execution (Async Fusion). */
	void DeferBehavior(uint32 BehaviorID, void* StructPtr, const void* TypeKey);

	/** Flushes all deferred behaviors in a single fused VVM transaction. */
	void FlushDeferredBehaviors();

	/** Resolve the concrete storage host used to execute a behavior on a state payload. */
	FResolvedStorageHost ResolveStorageHost(const FVerseBehavior& Behavior, const void* RequestedTypeKey) const;

	/** Resolve the authored compile policy for this compile request. */
	const FFlightBehaviorCompilePolicyRow* ResolveCompilePolicy(uint32 BehaviorID, const FCompilePolicyContext& CompilePolicyContext) const;

	/** Map an authored preferred domain into a concrete backend preference when supported. */
	static TOptional<Flight::Vex::EVexBackendKind> ResolvePreferredBackendFromPolicy(
		const FFlightBehaviorCompilePolicyRow& Policy,
		bool bIsAsync);

	/** Build a structured explanation for selected-vs-committed backend truth. */
	static FString BuildCommitDetail(
		const FString& SelectedBackend,
		const FString& CommittedBackend,
		const FFlightBehaviorCompilePolicyRow* Policy,
		bool bHasExecutableProcedure);

	/** Returns true when the resolved host can execute through the direct scalar/native struct path. */
	bool CanExecuteResolvedStorageHostDirect(const FResolvedStorageHost& Host) const;

	/** Resolve the compile-selected backend string back into an execution kind when available. */
	TOptional<Flight::Vex::EVexBackendKind> ResolveSelectedBackendKind(const FVerseBehavior& Behavior) const;

	/** Resolve the runtime backend for struct/native execution. */
	TOptional<Flight::Vex::EVexBackendKind> ResolveStructExecutionBackendKind(const FVerseBehavior& Behavior, const void* RequestedTypeKey) const;

	/** Resolve the runtime backend for bulk FDroidState execution. */
	TOptional<Flight::Vex::EVexBackendKind> ResolveBulkExecutionBackendKind(const FVerseBehavior& Behavior) const;

	/** Resolve the runtime backend for direct Mass/GPU execution. */
	TOptional<Flight::Vex::EVexBackendKind> ResolveDirectExecutionBackendKind(const FVerseBehavior& Behavior) const;

	/** Convert an optional runtime backend kind into report/debug text. */
	static FString DescribeRuntimeExecutionBackend(const TOptional<Flight::Vex::EVexBackendKind>& BackendKind);

	/** Resolve the direct-execution backend currently committed for this behavior and type key. */
	FString ResolveCommittedExecutionBackend(const FVerseBehavior& Behavior, const void* RequestedTypeKey) const;

	/** Returns the effective bound type key used by a behavior when validating or composing it. */
	const void* ResolveEffectiveBehaviorTypeKey(const FVerseBehavior& Behavior) const;

	/** Returns true when a behavior should execute through the composite path instead of backend dispatch. */
	static bool IsCompositeBehavior(const FVerseBehavior& Behavior);

	/** Recompute capability envelopes from current compile/runtime truth. */
	void RefreshCapabilityEnvelopes();

	/** Build the capability envelope for one behavior, recursively for composites. */
	FBehaviorCapabilityEnvelope BuildCapabilityEnvelope(uint32 BehaviorID, TSet<uint32>& ActiveBehaviorIds) const;

	/** Build the capability envelope for an atomic behavior. */
	FBehaviorCapabilityEnvelope BuildAtomicCapabilityEnvelope(const FVerseBehavior& Behavior) const;

	/** Build the capability envelope for a sequence composite behavior. */
	FBehaviorCapabilityEnvelope BuildSequenceCapabilityEnvelope(const FVerseBehavior& Behavior, TSet<uint32>& ActiveBehaviorIds) const;

	/** Build the capability envelope for a selector composite behavior. */
	FBehaviorCapabilityEnvelope BuildSelectorCapabilityEnvelope(const FVerseBehavior& Behavior, TSet<uint32>& ActiveBehaviorIds) const;

	/** Shared composite registration helper used by sequence and selector. */
	bool RegisterCompositeBehavior(
		uint32 BehaviorID,
		TConstArrayView<uint32> ChildBehaviorIds,
		FString& OutErrors,
		const void* TypeKey,
		EFlightVerseBehaviorKind Kind);

	/** Shared internal execution path that returns structured execution truth for atomic or composite behaviors. */
	FBehaviorExecutionResult ExecuteBehaviorWithResult(uint32 BehaviorID, void* StructPtr, const void* TypeKey);

	/** Persists the latest top-level execution truth onto the behavior and rebuilds orchestration reports. */
	void CommitBehaviorExecutionResult(uint32 BehaviorID, const FBehaviorExecutionResult& Result);

	/** Shared atomic execution path used by public struct/bulk execution and composite children. */
	FBehaviorExecutionResult ExecuteAtomicBehaviorWithResult(uint32 BehaviorID, const FVerseBehavior& Behavior, void* StructPtr, const void* TypeKey);

	/** Shared composite execution path used by public struct/bulk execution. */
	FBehaviorExecutionResult ExecuteCompositeBehaviorWithResult(uint32 BehaviorID, const FVerseBehavior& Behavior, void* StructPtr, const void* TypeKey);

	/** Sequence-only phase-one composite execution path. */
	FBehaviorExecutionResult ExecuteSequenceBehaviorWithResult(uint32 BehaviorID, const FVerseBehavior& Behavior, void* StructPtr, const void* TypeKey);

	/** Selector phase-one composite execution path without rollback guarantees. */
	FBehaviorExecutionResult ExecuteSelectorBehaviorWithResult(uint32 BehaviorID, const FVerseBehavior& Behavior, void* StructPtr, const void* TypeKey);

	/** Returns true when the guard symbol is legal for runtime evaluation on the effective type key. */
	bool ValidateSelectorGuard(const FSelectorBranchGuard& Guard, const void* TypeKey, FString& OutError) const;

	/** Attempts to read one runtime symbol value from the resolved host for guard evaluation. */
	bool TryReadRuntimeSymbolValue(const FResolvedStorageHost& Host, void* StructPtr, const FString& SymbolName, Flight::Vex::FVexRuntimeValue& OutValue, FString& OutError) const;

	/** Evaluates a selector branch guard against the current state host. */
	bool EvaluateSelectorGuard(
		const FSelectorBranchGuard& Guard,
		const FResolvedStorageHost& Host,
		void* StructPtr,
		bool& bOutPassed,
		FString& OutDetail) const;

	/** Execute a behavior on a previously resolved storage host. */
	bool ExecuteResolvedStorageHost(const FResolvedStorageHost& Host, const FVerseBehavior& Behavior, void* StructPtr);

	/** Execute a behavior on a previously resolved Mass/direct storage host. */
	bool ExecuteResolvedDirectStorageHost(
		const FResolvedStorageHost& Host,
		const FVerseBehavior& Behavior,
		Flight::Vex::EVexBackendKind BackendKind,
		TArrayView<struct FFlightTransformFragment> Transforms,
		TArrayView<struct FFlightDroidStateFragment> DroidStates);

	/** Execute a behavior on a previously resolved Mass/direct storage host using a processor-provided bundle. */
	bool ExecuteResolvedDirectStorageHost(
		const FResolvedStorageHost& Host,
		const FVerseBehavior& Behavior,
		Flight::Vex::EVexBackendKind BackendKind,
		FMassFragmentHostBundle& Bundle);

	/** Explicit AoS schema host for direct offset-backed scalar execution. */
	void ExecuteOnAosSchemaHost(const Flight::Vex::FVexProgramAst& Program, void* StructPtr, const Flight::Vex::FVexTypeSchema& Schema);

	/** Explicit accessor schema host for non-offset or provider-backed scalar execution. */
	void ExecuteOnAccessorSchemaHost(const Flight::Vex::FVexProgramAst& Program, void* StructPtr, const Flight::Vex::FVexTypeSchema& Schema);

	/** Explicit Mass/direct host for fragment-backed execution. */
	bool ExecuteOnMassSchemaHost(
		const FVerseBehavior& Behavior,
		Flight::Vex::EVexBackendKind BackendKind,
		TArrayView<struct FFlightTransformFragment> Transforms,
		TArrayView<struct FFlightDroidStateFragment> DroidStates);

	/** Explicit Mass/direct host for processor-provided fragment bundles. */
	bool ExecuteOnMassSchemaHost(
		const FVerseBehavior& Behavior,
		Flight::Vex::EVexBackendKind BackendKind,
		FMassFragmentHostBundle& Bundle);

	/** Explicit GPU host placeholder for GPU-buffer-backed execution. */
	bool ExecuteOnGpuSchemaHost(const FVerseBehavior& Behavior);

	/** Update behavior-level commit truth and mirror it into compile/orchestration reports. */
	void UpdateBehaviorCommitState(uint32 BehaviorID, const FString& CommittedBackend, const FString& CommitDetail);

	/** Resolve a terminal GPU submission result back into behavior commit truth. */
	void ResolveGpuExecutionCommit(uint32 BehaviorID, int64 SubmissionHandle, bool bSuccess, const FString& Detail);

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	bool TryValidateVerseSource(const FString& VerseSource, FString& OutDiagnostics) const;
	bool TryCreateVmProcedure(uint32 BehaviorID, FVerseBehavior& Behavior, TSharedPtr<Flight::Vex::FVexIrProgram> IrProgram, FString& OutDiagnostics);
	static Verse::FOpResult VmBehaviorEntryThunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);
	Verse::FOpResult ExecuteVmEntryThunk(Verse::FRunningContext Context, Verse::VNativeFunction::Args Arguments);

	// VEX IR -> VVM Native Thunks (Now generalized)
	static Verse::FOpResult GetStateValue(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);
	static Verse::FOpResult SetStateValue(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);

	/** Generic dispatcher for registered native thunks */
	static Verse::FOpResult RegistryThunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);

	/** Async completion entrypoint for the native GPU script bridge */
	void CompleteGpuWait(int64 RequestId, bool bSuccess = true);

	// Active execution payload consumed by VM thunk invocation.
	uint32 ActiveVmBehaviorID = 0;
	void* ActiveVmStatePtr = nullptr;
	const void* ActiveVmTypeKey = nullptr;
#endif

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	/** Map of reflected type name to its Verse Native Descriptor */
	TMap<FString, FVniTypeDesc> NativeTypeDescriptors;

	/** Pending GPU readbacks waiting for fulfillment (GC Rooted) */
	TMap<int64, Verse::TWriteBarrier<Verse::VPlaceholder>> PendingReadbacks;

	/** The Verse VM context for this world */
	Verse::FRunningContext VerseContext;
#endif
	/** Cached symbol definitions from the schema manifest */
	TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions;

private:
	/** Collection of behaviors pending fused batch reduction */
	TArray<FVexDeferredBehavior> DeferredBehaviors;
};
