# Functional Programming Patterns

Rust-inspired functional utilities for cleaner C++ code in FlightProject.

## Overview

`FlightFunctional.h` provides lambda-based combinators that push C++ toward Rust-like ergonomics. These patterns reduce nested conditionals, make error paths explicit, and enable chainable transformations.

```
Traditional C++                     Functional Style
─────────────────                   ─────────────────
if (!A) return Err;                 return ValidateA()
if (!B) return Err;        →           .AndThen(ValidateB)
if (!C) return Err;                    .AndThen(ValidateC)
return Ok;                             .Map(Transform);
```

## Core Types

### TResult<T, E> - Error Handling

Rust's `Result<T, E>` for C++. Represents either success (`Ok`) or failure (`Err`).

```cpp
#include "Core/FlightFunctional.h"
using namespace Flight::Functional;

// Creating results
auto Success = TResult<int, FString>::Ok(42);
auto Failure = TResult<int, FString>::Err(TEXT("Something went wrong"));

// Or use the helpers
auto Success = Ok(42);
auto Failure = Err<int>(FString(TEXT("Error message")));
```

**Monadic Operations**:

| Method | Description | Rust Equivalent |
|--------|-------------|-----------------|
| `Map(Fn)` | Transform Ok value | `.map()` |
| `MapErr(Fn)` | Transform Err value | `.map_err()` |
| `AndThen(Fn)` | Chain fallible operations | `.and_then()` / `?` |
| `OrElse(Fn)` | Recover from error | `.or_else()` |
| `Match(OnOk, OnErr)` | Pattern match both cases | `match` |
| `Unwrap()` | Extract value (crashes on Err) | `.unwrap()` |
| `UnwrapOr(Default)` | Extract or use default | `.unwrap_or()` |
| `UnwrapOrElse(Fn)` | Extract or compute default | `.unwrap_or_else()` |

### TDefer<F> - Scoped Cleanup

Like Go's `defer` or Rust's `Drop`. Executes cleanup when scope exits.

```cpp
void ProcessFile()
{
    int Fd = open("file.txt", O_RDONLY);
    auto Guard = Defer([&]() { close(Fd); });

    // ... code that might early return or throw ...
    // Fd is automatically closed when Guard goes out of scope
}

// Or use the macro for auto-naming
void ProcessFile()
{
    int Fd = open("file.txt", O_RDONLY);
    FLIGHT_DEFER([&]() { close(Fd); });

    // ...
}
```

**Conditional Cleanup**:
```cpp
auto Guard = Defer([&]() { RollbackTransaction(); });

if (Commit())
{
    Guard.Cancel();  // Don't rollback on success
}
```

### TPipe<F> - Pipeline Operator

Enable `|` operator chaining for transformations.

```cpp
auto Result = SomeValue
    | Pipe([](auto x) { return x * 2; })
    | Pipe([](auto x) { return x + 1; });

// Built-in combinators
auto Filtered = MyArray
    | Filter([](auto& Item) { return Item.IsValid(); })
    | Transform([](auto& Item) { return Item.Name; })
    | ForEach([](auto& Name) { UE_LOG(LogTemp, Log, TEXT("%s"), *Name); });
```

## Usage Patterns

### Pattern 1: Validation Chains

Replace nested validation with `AndThen` chains:

```cpp
// Before: Nested early returns
bool ValidateAndProcess(const FConfig& Config)
{
    if (!Config.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid config"));
        return false;
    }

    UDataTable* Table = Config.Table.LoadSynchronous();
    if (!Table)
    {
        UE_LOG(LogTemp, Error, TEXT("Table not loaded"));
        return false;
    }

    const FRow* Row = Table->FindRow<FRow>(Config.RowName, TEXT(""));
    if (!Row)
    {
        UE_LOG(LogTemp, Error, TEXT("Row not found"));
        return false;
    }

    ProcessRow(*Row);
    return true;
}

// After: AndThen chain with single error handling point
bool ValidateAndProcess(const FConfig& Config)
{
    return TResult<FConfig, FString>::Ok(Config)
        .AndThen([](const FConfig& Cfg) -> TResult<FConfig, FString>
        {
            if (!Cfg.IsValid())
                return Err<FConfig>(FString(TEXT("Invalid config")));
            return Ok(Cfg);
        })
        .AndThen([](const FConfig& Cfg) -> TResult<UDataTable*, FString>
        {
            UDataTable* Table = Cfg.Table.LoadSynchronous();
            if (!Table)
                return Err<UDataTable*>(FString(TEXT("Table not loaded")));
            return Ok(Table);
        })
        .AndThen([&Config](UDataTable* Table) -> TResult<FRow, FString>
        {
            const FRow* Row = Table->FindRow<FRow>(Config.RowName, TEXT(""));
            if (!Row)
                return Err<FRow>(FString(TEXT("Row not found")));
            return Ok(*Row);
        })
        .Match(
            [](const FRow& Row) { ProcessRow(Row); return true; },
            [](const FString& Error) { UE_LOG(LogTemp, Error, TEXT("%s"), *Error); return false; }
        );
}
```

### Pattern 2: DataTable Loading

Generic helper for loading typed rows from DataTables:

```cpp
template<typename TRow>
static TResult<TRow, FString> LoadConfigRow(
    const TSoftObjectPtr<UDataTable>& TableRef,
    FName RowName,
    const TCHAR* TableName)
{
    return TResult<UDataTable*, FString>::Ok(TableRef.LoadSynchronous())
        .AndThen([TableName](UDataTable* Table) -> TResult<UDataTable*, FString>
        {
            if (!Table)
                return Err<UDataTable*>(FString::Printf(TEXT("%s not set"), TableName));
            return Ok(Table);
        })
        .AndThen([RowName, TableName](UDataTable* Table) -> TResult<TRow, FString>
        {
            if (RowName.IsNone())
                return Err<TRow>(FString::Printf(TEXT("%s row not specified"), TableName));

            const TRow* Row = Table->FindRow<TRow>(RowName, TEXT("LoadConfigRow"));
            if (!Row)
                return Err<TRow>(FString::Printf(TEXT("Row '%s' not found"), *RowName.ToString()));

            return Ok(*Row);
        });
}

// Usage
bool UMySubsystem::LoadConfig()
{
    return LoadConfigRow<FMyConfigRow>(Settings->ConfigTable, Settings->ConfigRow, TEXT("ConfigTable"))
        .Match(
            [this](FMyConfigRow Row) { Config = MoveTemp(Row); return true; },
            [](const FString& Err) { UE_LOG(LogTemp, Warning, TEXT("%s"), *Err); return false; }
        );
}
```

### Pattern 3: Multi-Row Loading with Filter + Map

Load, filter, and transform rows in a pipeline:

```cpp
template<typename TRow>
static TResult<TArray<TRow>, FString> LoadTableRows(
    const TSoftObjectPtr<UDataTable>& TableRef,
    const TCHAR* TableName,
    TFunction<bool(const TRow&)> RowFilter,
    TFunction<void(TRow&, FName)> OnRow)
{
    UDataTable* Table = TableRef.LoadSynchronous();
    if (!Table)
        return Err<TArray<TRow>>(FString::Printf(TEXT("%s not set"), TableName));

    TArray<TRow> Result;
    for (const auto& Pair : Table->GetRowMap())
    {
        const TRow* Row = reinterpret_cast<const TRow*>(Pair.Value);
        if (!Row || (RowFilter && !RowFilter(*Row)))
            continue;

        TRow RowCopy = *Row;
        if (OnRow) OnRow(RowCopy, Pair.Key);
        Result.Add(MoveTemp(RowCopy));
    }

    return Result.Num() > 0
        ? Ok(MoveTemp(Result))
        : Err<TArray<TRow>>(FString(TEXT("No rows matched")));
}

// Usage: Load, filter by scenario, sort by name
return LoadTableRows<FSpatialRow>(Settings->SpatialTable, TEXT("SpatialTable"),
    [Scenario](const FSpatialRow& Row) { return Row.Scenario == Scenario; },
    [](FSpatialRow& Row, FName Key) { Row.RowName = Key; })
.Map([](TArray<FSpatialRow> Rows)
{
    Rows.Sort([](const auto& A, const auto& B) { return A.RowName.LexicalLess(B.RowName); });
    return Rows;
})
.Match(
    [this](auto Rows) { SpatialRows = MoveTemp(Rows); return true; },
    [](const FString&) { return false; }
);
```

### Pattern 4: GPU/Async Context Validation

Validate prerequisites before async operations:

```cpp
struct FGpuContext
{
    IVulkanDynamicRHI* VulkanRHI = nullptr;
    VkSemaphore TimelineSemaphore = VK_NULL_HANDLE;
    uint64 TimelineValue = 0;
    TUniquePtr<FExportableSemaphore> Semaphore;
};

void SignalGpuCompletion(int64 TrackingId, TFunction<void()> Callback)
{
    auto Ctx = TResult<FGpuContext, FString>::Ok(FGpuContext{})
        .AndThen([this](FGpuContext C) -> TResult<FGpuContext, FString>
        {
            if (!bExportableSemaphoresAvailable)
                return Err<FGpuContext>(FString(TEXT("Exportable semaphores unavailable")));
            return Ok(MoveTemp(C));
        })
        .AndThen([](FGpuContext C) -> TResult<FGpuContext, FString>
        {
            C.VulkanRHI = GetIVulkanDynamicRHI();
            if (!C.VulkanRHI)
                return Err<FGpuContext>(FString(TEXT("Vulkan RHI unavailable")));
            return Ok(MoveTemp(C));
        })
        .AndThen([](FGpuContext C) -> TResult<FGpuContext, FString>
        {
            if (!C.VulkanRHI->RHIGetGraphicsQueueTimelineSemaphoreInfo(
                    &C.TimelineSemaphore, &C.TimelineValue))
                return Err<FGpuContext>(FString(TEXT("Timeline semaphores unavailable")));
            return Ok(MoveTemp(C));
        })
        .AndThen([](FGpuContext C) -> TResult<FGpuContext, FString>
        {
            C.Semaphore = FExportableSemaphore::Create();
            if (!C.Semaphore)
                return Err<FGpuContext>(FString(TEXT("Failed to create semaphore")));
            return Ok(MoveTemp(C));
        });

    if (Ctx.IsErr())
    {
        UE_LOG(LogGpuBridge, Warning, TEXT("SignalGpuCompletion: %s"), *Ctx.GetError());
        return;
    }

    FGpuContext ValidCtx = MoveTemp(Ctx).Unwrap();
    // ... proceed with validated context ...
}
```

### Pattern 5: Retry Combinator

Retry fallible operations:

```cpp
auto Result = Retry(3, []() -> TResult<FData, FString>
{
    return FetchFromNetwork();
});

Result.Match(
    [](const FData& Data) { Process(Data); },
    [](const FString& Err) { UE_LOG(LogTemp, Error, TEXT("All retries failed: %s"), *Err); }
);
```

## Available Utilities

| Utility | Purpose | Header Location |
|---------|---------|-----------------|
| `TResult<T,E>` | Error handling monad | `FlightFunctional.h:62` |
| `TDefer<F>` | Scoped cleanup | `FlightFunctional.h:21` |
| `TValidateChain<E,Tuple>` | Heterogeneous validation | `FlightFunctional.h:217` |
| `TLazy<T,E>` | Deferred memoized result | `FlightFunctional.h:438` |
| `TLazyChain<E,Tuple>` | Lazy heterogeneous validation | `FlightFunctional.h:632` |
| `TPhantomState<D,Tags>` | Compile-time state encoding | `FlightFunctional.h:586` |
| `TPhantomChain<D,E,Tags>` | Chainable phantom transitions | `FlightFunctional.h:704` |
| `TPipe<F>` | Pipeline operator | `FlightFunctional.h:732` |
| `TAsyncOp<T>` | Async continuations | `FlightFunctional.h:786` |
| `TBuilder<T>` | Fluent builder | `FlightFunctional.h:836` |
| `TStateMachine<Ctx>` | Lambda state machine | `FlightFunctional.h:865` |
| `Retry(n, Fn)` | Retry combinator | `FlightFunctional.h:903` |

## Macros

```cpp
// Auto-named defer guard
FLIGHT_DEFER([&]() { Cleanup(); });

// Early return on error (GCC only - uses statement expressions)
auto Value = FLIGHT_TRY(FallibleOperation());
```

Note: `FLIGHT_TRY` uses GCC statement expressions and won't compile on MSVC.

## Integration Points

Currently integrated in:

| File | Pattern Used |
|------|--------------|
| `FlightDataSubsystem.cpp` | `LoadConfigRow`, `LoadTableRows` with `Match` |
| `FlightGpuIoUringBridge.cpp` | Validation chain with `AndThen` |

## Design Philosophy

Inspired by:
- **Rust**: `Result<T,E>`, `?` operator, `map`/`and_then`
- **Go**: `defer` for cleanup
- **F#/Elixir**: Pipeline operator `|>`
- **Haskell**: Monadic bind, functor map

The goal isn't to replicate Rust's type system, but to capture ergonomic patterns that reduce boilerplate and make error paths explicit.

## Limitations

- No `std::uncaught_exceptions` in UE's libc++ (TScopeSuccess/TScopeFailure removed)
- `FLIGHT_TRY` requires GCC statement expressions
- `TEXT()` returns `const TCHAR*`, must wrap in `FString()` for `Err<T>()`
- No compile-time exhaustiveness checking like Rust's `match`

## The Enum Problem: Why Rust Does This Better

### The Awkward Accumulator Pattern

Our GPU validation chain reveals a fundamental limitation:

```cpp
struct FGpuContext
{
    IVulkanDynamicRHI* VulkanRHI = nullptr;      // Maybe set?
    VkSemaphore TimelineSemaphore = VK_NULL_HANDLE;  // Maybe set?
    uint64 TimelineValue = 0;                    // Maybe set?
    TUniquePtr<FExportableSemaphore> Semaphore;  // Maybe set?
};

auto Ctx = TResult<FGpuContext, FString>::Ok(FGpuContext{})
    .AndThen([](FGpuContext C) { /* set VulkanRHI */ return Ok(MoveTemp(C)); })
    .AndThen([](FGpuContext C) { /* set Timeline */ return Ok(MoveTemp(C)); })
    .AndThen([](FGpuContext C) { /* set Semaphore */ return Ok(MoveTemp(C)); });
```

**Problems:**
1. Pre-declare all fields that *might* exist
2. Mutate and pass struct through each step
3. No compile-time guarantee fields are set before access
4. Explicit `AndThen` ceremony for each step

### How Rust Solves This

**With `?` operator - invisible chaining:**

```rust
fn signal_gpu_completion(&self, tracking_id: i64) -> Result<(), String> {
    if !self.exportable_available {
        return Err("Exportable semaphores unavailable".into());
    }

    // Each line either succeeds and binds, or early-returns error
    let rhi = get_vulkan_rhi()
        .ok_or("Vulkan RHI unavailable")?;

    let (timeline_sem, timeline_val) = rhi.get_timeline_info()
        .ok_or("Timeline semaphores unavailable")?;

    let semaphore = ExportableSemaphore::create()
        .ok_or("Failed to create semaphore")?;

    // All bindings in scope - no struct needed
    // Compiler knows timeline_sem exists here
    submit_work(rhi, timeline_sem, timeline_val, semaphore);
    Ok(())
}
```

**With sum types - state encoded in type system:**

```rust
enum ValidationState {
    Initial,
    HasRhi(VulkanRhi),
    HasTimeline { rhi: VulkanRhi, sem: Semaphore, val: u64 },
    Ready { rhi: VulkanRhi, sem: Semaphore, val: u64, exportable: ExportableSemaphore },
}

// Pattern match is exhaustive - compiler enforces you handle all cases
match state {
    ValidationState::Ready { rhi, sem, val, exportable } => {
        // Can ONLY access these fields in this state
    }
    _ => { /* error */ }
}
```

### C++ Alternatives (All Imperfect)

| Approach | Implementation | Tradeoff |
|----------|---------------|----------|
| `std::variant` | `std::variant<Initial, HasRhi, HasTimeline, Ready>` | Verbose `std::visit`, no `?` sugar |
| `std::optional` per field | Each field wrapped in optional | Runtime checks, partial states possible |
| Builder with assertions | `check(VulkanRHI != nullptr)` | Runtime crashes, not compile-time |
| Our `TResult` chain | `AndThen` accumulator | Works, but clunky |
| Coroutines | `co_await` each step | C++20, complex machinery |

### What We're Missing

| Rust Feature | C++ Equivalent | Gap |
|--------------|----------------|-----|
| `?` operator | `FLIGHT_TRY` macro | GCC-only, ugly syntax |
| Sum types (enums) | `std::variant` | No pattern matching syntax |
| Exhaustive match | `std::visit` | Compiler doesn't enforce |
| Ownership/borrowing | Manual discipline | No compiler help |
| `impl Trait` | Templates | Less ergonomic |

### Solution: Heterogeneous Validation Chain

We implemented option #3 - `TValidateChain` with type-accumulating `Then()`:

```cpp
// Each step receives ALL previous results, produces its own type
auto [_, VulkanRHI, TimelineInfo, Semaphore] = Validate<FString>()
    .Then([]() -> TResult<bool, FString>
    {
        if (!bExportableSemaphoresAvailable)
            return Err<bool>(FString(TEXT("Exportable semaphores unavailable")));
        return Ok(true);
    })
    .Then([](bool) -> TResult<IVulkanDynamicRHI*, FString>
    {
        auto* RHI = GetIVulkanDynamicRHI();
        if (!RHI)
            return Err<IVulkanDynamicRHI*>(FString(TEXT("Vulkan RHI unavailable")));
        return Ok(RHI);
    })
    .Then([](bool, IVulkanDynamicRHI* RHI) -> TResult<FTimelineInfo, FString>
    {
        FTimelineInfo Info{};
        if (!RHI->GetTimelineInfo(&Info.Semaphore, &Info.Value))
            return Err<FTimelineInfo>(FString(TEXT("Timeline unavailable")));
        return Ok(Info);
    })
    .Then([](bool, IVulkanDynamicRHI*, FTimelineInfo)
        -> TResult<TUniquePtr<FExportableSemaphore>, FString>
    {
        auto Sem = FExportableSemaphore::Create();
        if (!Sem)
            return Err<TUniquePtr<FExportableSemaphore>>(FString(TEXT("Create failed")));
        return Ok(MoveTemp(Sem));
    })
    .Finish()
    .Unwrap();

// Now use VulkanRHI, TimelineInfo, Semaphore directly!
```

**Key properties:**
- Each `Then()` receives unpacked results of ALL previous steps
- Each step returns `TResult<T, E>` where T can be any type
- Types accumulate into `std::tuple<T1, T2, T3, ...>`
- Structured bindings unpack at the end
- Short-circuits on first error
- No accumulator struct with "maybe set" fields

**How it works internally:**
```cpp
template<typename E, typename Tuple>
class TValidateChain {
    TResult<Tuple, E> Current;

    template<typename F>
    auto Then(F&& Step) && {
        // 1. Deduce Step's return type when called with unpacked Tuple
        using NewT = /* extract T from TResult<T,E> returned by Step */;
        using NewTuple = tuple_cat_t<Tuple, tuple<NewT>>;

        // 2. If already failed, propagate with new type
        if (Current.IsErr())
            return TValidateChain<E, NewTuple>(Err(Current.GetError()));

        // 3. Invoke Step with unpacked current tuple
        auto Result = std::apply(Step, Current.Unwrap());

        // 4. Append result, return chain with extended type
        return TValidateChain<E, NewTuple>(
            Ok(tuple_cat(current_values, make_tuple(Result.Unwrap())))
        );
    }
};
```

### The Lambda Bracket Trickery

The lambda parameters/captures enable powerful patterns:

```cpp
// Stateful with mutable capture
[attempts = 0](auto rhi) mutable -> TResult<Timeline, FString> {
    if (++attempts > 3) return Err<Timeline>(TEXT("Retries exhausted"));
    return GetTimeline(rhi);
}

// Conditional behavior via concepts (C++20)
[](std::derived_from<IVulkanRHI> auto rhi) { ... }

// Template lambda for generic steps
[]<typename T>(T prev) { ... }

// Deducing this (C++23) for recursive patterns
[](this auto&& self, auto rhi) -> TResult<Timeline, FString> {
    return TryGetTimeline(rhi).OrElse([&](auto) { return self(rhi); });
}
```

### Future Directions

1. **C++23 pattern matching proposals** (P1371, P2688) - would make `std::variant` feel like Rust enums

2. **`?` operator via coroutines**
   ```cpp
   Task<void> SignalGpuCompletion() {
       auto rhi = co_await GetVulkanRHI();      // early return on error
       auto timeline = co_await GetTimeline(rhi);
       auto sem = co_await CreateSemaphore();
       // ...
   }
   ```

3. **Compile-time state encoding** (phantom types) - **IMPLEMENTED!**

## Phantom Types / Typestate

Encode validation state in the type system. The type *changes* with each transition, and the compiler enforces valid progressions.

### The Pattern

```cpp
// Zero-cost tag types (empty structs)
namespace GpuTags {
    struct HasRhi {};
    struct HasTimeline {};
    struct HasSemaphore {};
    struct Ready {};
}

// Data being built up
struct FGpuData {
    IVulkanDynamicRHI* Rhi = nullptr;
    VkSemaphore TimelineSem = VK_NULL_HANDLE;
    uint64 TimelineValue = 0;
    TUniquePtr<FExportableSemaphore> Semaphore;
};

// Chainable validation with compile-time state tracking
auto Result = PhantomChain<FGpuData>(FGpuData{})
    .Then<GpuTags::HasRhi>([](FGpuData& d) -> TResult<bool, FString>
    {
        d.Rhi = GetIVulkanDynamicRHI();
        if (!d.Rhi) return Err<bool>(FString(TEXT("No RHI")));
        return Ok(true);
    })
    .Then<GpuTags::HasTimeline>([](FGpuData& d) -> TResult<bool, FString>
    {
        if (!d.Rhi->GetTimelineInfo(&d.TimelineSem, &d.TimelineValue))
            return Err<bool>(FString(TEXT("No timeline")));
        return Ok(true);
    })
    .Then<GpuTags::HasSemaphore>([](FGpuData& d) -> TResult<bool, FString>
    {
        d.Semaphore = FExportableSemaphore::Create();
        if (!d.Semaphore) return Err<bool>(FString(TEXT("Create failed")));
        return Ok(true);
    })
    .Finish();

// Type at each step:
// PhantomChain<FGpuData, FString>
// PhantomChain<FGpuData, FString, HasRhi>
// PhantomChain<FGpuData, FString, HasRhi, HasTimeline>
// PhantomChain<FGpuData, FString, HasRhi, HasTimeline, HasSemaphore>
```

### Compile-Time Assertions

Use `Require<>()` to assert tags at compile time:

```cpp
auto state = PhantomChain<FGpuData>(FGpuData{})
    .Then<GpuTags::HasRhi>([](auto& d) { ... })
    .Finish()
    .Unwrap();

// This compiles:
state.Require<GpuTags::HasRhi>();

// This FAILS TO COMPILE:
state.Require<GpuTags::HasTimeline>();  // Error: Missing required state tags
```

### Conditional Methods via SFINAE

Define methods that only exist when certain tags are present:

```cpp
template<typename Data, typename... Tags>
class TGpuContextWrapper : public TPhantomState<Data, Tags...>
{
public:
    // Only available when HasRhi and HasTimeline are both present
    template<typename = std::enable_if_t<
        TPhantomState<Data, Tags...>::template Has<GpuTags::HasRhi>() &&
        TPhantomState<Data, Tags...>::template Has<GpuTags::HasTimeline>()
    >>
    void Submit()
    {
        // Can only call this when both tags are present
        // Compiler error otherwise!
    }
};
```

### Validate() vs PhantomChain() - When to Use Which

| Pattern | Use Case | Result Type |
|---------|----------|-------------|
| `Validate()` | Collecting multiple independent values | `tuple<T1, T2, T3>` |
| `PhantomChain()` | Building up single object with state | `TPhantomState<Data, Tags...>` |

**Validate()** - each step produces a new value:
```cpp
auto [rhi, timeline, sem] = Validate<FString>()
    .Then([]() { return GetRhi(); })           // Produces RHI*
    .Then([](RHI*) { return GetTimeline(); })  // Produces Timeline
    .Then([](RHI*, Timeline) { return CreateSem(); })  // Produces Sem
    .Finish().Unwrap();
```

**PhantomChain()** - each step mutates shared data, tags track progress:
```cpp
FGpuData data = PhantomChain<FGpuData>(FGpuData{})
    .Then<HasRhi>([](FGpuData& d) { d.Rhi = GetRhi(); return Ok(true); })
    .Then<HasTimeline>([](FGpuData& d) { d.Timeline = ...; return Ok(true); })
    .Then<HasSem>([](FGpuData& d) { d.Sem = ...; return Ok(true); })
    .Unwrap();
```

### Connection to Rust's PhantomData

In Rust's `syn`/`quote` (procedural macros), `PhantomData<T>` marks type-level associations without storing data. Our phantom tags serve a similar purpose:

| Rust | C++ (Our Pattern) |
|------|-------------------|
| `PhantomData<T>` | Empty struct tags in template params |
| Marker traits | `Has<Tag>()` compile-time check |
| Typestate pattern | `TPhantomState<Data, Tags...>` |
| `#[derive(...)]` | `Transition<NewTag>()` |

The key insight: **the type IS the state**. You can't accidentally call methods meant for a later state because the type literally doesn't have them yet.

## Multi-Faceted Binding (Slate-Inspired)

Inspired by Unreal's Slate UI framework, which uses `SLATE_ATTRIBUTE` macros to generate multiple binding variants (`_Lambda`, `_Static`, `_Raw`). Our `TValidateChain` now offers explicit variants for different callable types:

### Available Binding Variants

| Method | Purpose | When to Use |
|--------|---------|-------------|
| `Then(lambda)` | Generic callable | Default, accepts anything |
| `Then_Lambda(lambda)` | Explicit lambda | Same as `Then()`, for clarity |
| `Then_Static(&Func)` | Free/static function | When using existing global validators |
| `Then_Method(obj, &Class::Method)` | Member function | When validation is on another object |
| `Then_WeakLambda(uobj, lambda)` | Guarded lambda | When capturing UObjects that might die |

### Usage Examples

```cpp
// Global validation function
TResult<FRhiInfo, FString> ValidateRhi()
{
    auto* RHI = GetIVulkanDynamicRHI();
    if (!RHI) return Err<FRhiInfo>(FString(TEXT("No RHI")));
    return Ok(FRhiInfo{RHI});
}

// Member function validator
class UGpuValidator : public UObject
{
public:
    TResult<FTimelineInfo, FString> ValidateTimeline(FRhiInfo Rhi) const;
};

// Validation chain with mixed binding types
auto Result = Validate<FString>()
    .Then_Static(&ValidateRhi)                              // Free function
    .Then_Method(Validator, &UGpuValidator::ValidateTimeline)  // Member function
    .Then_Lambda([](FRhiInfo, FTimelineInfo Timeline) {     // Explicit lambda
        return CreateSemaphore(Timeline);
    })
    .Finish();
```

### WeakLambda for UObject Safety

When a lambda captures a UObject pointer, that object might be garbage collected before the lambda executes. `Then_WeakLambda` guards against this:

```cpp
auto Result = Validate<FString>()
    .Then([]() { return GetRhi(); })
    .Then_WeakLambda(this, [this](auto Rhi) {
        // If 'this' has been destroyed, returns Err automatically
        return this->ValidateSomething(Rhi);
    })
    .Finish();
```

The guard:
1. Creates a `TWeakObjectPtr` to the UObject
2. Checks validity before invoking the lambda
3. Returns `Err("UObject destroyed during validation")` if invalid

## Lazy/Cached Validation

Inspired by Slate's `TAttribute<T>` which stores either a value OR a delegate. Useful for expensive computations that should be deferred until actually needed, then cached.

### TLazy<T, E> - Deferred Memoized Result

```cpp
// Create a lazy computation (nothing executed yet)
auto LazyRhi = TLazy<IVulkanDynamicRHI*, FString>([]() {
    auto* RHI = GetIVulkanDynamicRHI();
    return RHI ? Ok(RHI) : Err<IVulkanDynamicRHI*>(FString(TEXT("No RHI")));
});

// Later, when we need it (NOW it evaluates)
const TResult<IVulkanDynamicRHI*, FString>& Result = LazyRhi.Get();

// Subsequent calls return cached result (no re-evaluation)
const TResult<IVulkanDynamicRHI*, FString>& Cached = LazyRhi.Get();  // Instant!
```

### Creation Methods

```cpp
// From thunk (deferred)
auto Lazy1 = TLazy<int, FString>([]() { return Ok(ComputeExpensive()); });

// From immediate value (already computed)
auto Lazy2 = TLazy<int, FString>::Immediate(42);

// From immediate error
auto Lazy3 = TLazy<int, FString>::Failed(TEXT("Known failure"));

// Convenience helpers
auto Lazy4 = Lazy(42);  // TLazy<int, FString>::Immediate
auto Lazy5 = LazyEval<int>([]() { return Ok(42); });
```

### Lazy Chaining

Compose lazy computations that only execute when the final result is accessed:

```cpp
auto LazyRhi = LazyEval<RHI*>([]() { return GetRhi(); });

// Build dependent computation (still lazy!)
auto LazyTimeline = LazyRhi.Map([](RHI* r) { return r->GetTimeline(); });

// Nothing has executed yet...

// NOW both execute (and cache)
FTimeline Timeline = LazyTimeline.Unwrap();
```

### TLazyChain - Deferred Heterogeneous Validation

Combines `TValidateChain`'s type accumulation with `TLazy`'s deferred evaluation:

```cpp
// Build the validation pipeline (nothing executed)
auto LazyValidation = LazyValidate<FString>()
    .Then([]() { return ExpensiveRhiCheck(); })      // Not called yet
    .Then([](auto rhi) { return ExpensiveTimelineCheck(rhi); })  // Not called yet
    .Then([](auto rhi, auto timeline) { return CreateSemaphore(); })  // Not called yet
    .Build();

// Later, when we actually need results...
if (SomeCondition)
{
    // NOW evaluation happens (and caches)
    auto Result = LazyValidation.Get();
    auto [rhi, timeline, sem] = Result.Unwrap();
}
else
{
    // Condition false - nothing was ever computed!
}
```

### Use Cases

| Pattern | When to Use |
|---------|-------------|
| `Validate().Then()...Finish()` | Eager validation, need results now |
| `LazyValidate().Then()...Build()` | Deferred validation, might not need results |
| `TLazy<T>` | Single expensive value, access multiple times |
| `TLazy<T>.Map()` | Build lazy computation chains |

### AST Parsing Example

The pattern is useful for recursive AST parsing where type resolution is deferred:

```cpp
struct FExpressionNode
{
    TLazy<FType, FString> ResolvedType;  // Deferred type resolution

    FExpressionNode(FParseContext& Ctx)
        : ResolvedType([&Ctx, this]() {
            // Only resolves type when first accessed
            // May involve recursive descent into child nodes
            return this->ComputeType(Ctx);
        })
    {}
};

// Later, during codegen
void GenerateCode(const FExpressionNode& Node)
{
    // NOW type resolution happens (recursively if needed)
    FType Type = Node.ResolvedType.Unwrap();
}
```

## Thunks, Purity, and Side Effects

Understanding the theoretical foundation helps avoid subtle bugs with lazy evaluation.

### What Is a Thunk?

A thunk is a suspended computation - a "promise to compute" rather than a computed value. In graph reduction terms, it's an unreduced redex:

```
Active Node                    Thunk
───────────                    ─────
Reduces eagerly                Defers indefinitely
Consumes resources now         Holds resources hostage
Known value                    Potential value
"Work done"                    "Work promised"
```

Thunks are "oafish but necessary" - when we can't statically determine if computation is needed, we wrap it in a thunk and let runtime demand decide. The thunk doesn't optimize; it *defers* optimization.

### The Purity Gradient

Not all thunks are equal in their safety properties:

| Category | Example | Safety |
|----------|---------|--------|
| Pure values | `3`, `"hello"` | Always safe |
| Pure thunks | `lazy { 1 + 1 }` | Safe to memoize, force anytime |
| Effectful thunks | `lazy { ReadFile() }` | Timing matters, force-once |
| Bottom/undefined | `lazy { while(true){} }` | Forcing may not terminate |

### Side Effects vs Error Values

A common confusion: is `NaN` (from divide-by-zero) a side effect?

```
Pure math:     x/0 = ⊥ (undefined, should crash)
IEEE floats:   x/0 = NaN (encodes "undefined" as a value)
```

**NaN is not a side effect** - it's a sentinel value. The error is encoded *in* the return value rather than escaping via exception or mutation. It's mathematically impure but programmatically pure.

True side effects escape the return value:
- I/O (file reads, network)
- Mutation (modifying captured state)
- Exceptions (that propagate out)
- Non-termination

### The Temporal Danger

Thunks defer both computation AND any embedded side effects. This creates timing dependencies:

```cpp
// Pure thunk - force anytime, same result
auto LazySum = TLazy<int>([]() {
    return 1 + 1;
});

// Effectful thunk - WHEN you force matters!
auto LazyRead = TLazy<FString>([]() {
    return LoadFileContents();  // I/O happens on first Get()
});

// Dangerous - mutation on force
int Counter = 0;
auto LazyIncrement = TLazy<int>([&Counter]() {
    return ++Counter;  // When does this happen?
});
```

In Haskell, the runtime can safely memoize thunks because purity is enforced. In C++, we trust the programmer. Our `TLazy` caches after first force (preventing repeated effects), but the *timing* of that first force becomes semantically significant.

### Memory and Lifecycle

Thunks hold references to their closure environment until forced or destroyed. This can cause:

**Space leaks** - thunk chains accumulate heap pressure:
```cpp
// Each iteration creates a thunk holding previous thunks
for (auto& Item : LargeCollection)
{
    LazyResults.Add(TLazy<Result>([&Item]() { return Process(Item); }));
}
// All closures live until we force or destroy LazyResults
```

**Dangling references** - closure outlives captured data:
```cpp
TLazy<int> CreateLazy()
{
    int LocalValue = 42;
    return TLazy<int>([&LocalValue]() {  // Captures reference!
        return LocalValue;  // Dangling after function returns
    });
}
```

### Guidelines for TLazy

1. **Prefer pure thunks** - no I/O, no mutation in the closure
2. **Capture by value** when the thunk might outlive the source
3. **Use `Collapse()`** to release closure memory after forcing
4. **Document effectful thunks** - if forcing has side effects, make it obvious
5. **Consider eager evaluation** if purity can't be guaranteed

```cpp
// Good: pure computation, value capture
auto LazyHash = TLazy<uint32>([Data = CopyOfData]() {
    return ComputeHash(Data);
});

// Risky: I/O in thunk, timing-dependent
auto LazyConfig = TLazy<FConfig>([Path]() {
    return LoadConfigFromDisk(Path);  // When does disk read happen?
});

// Better: make the I/O explicit, thunk only for transform
FString RawConfig = LoadConfigFromDisk(Path);  // I/O now, explicit
auto LazyParsed = TLazy<FConfig>([RawConfig]() {
    return ParseConfig(RawConfig);  // Pure transform, safe to defer
});
```

### The "Already Optimum" Insight

A thunk that's never forced was the *correct* choice - we successfully avoided unnecessary work. The thunk becomes "garbage" only in the memory sense, not the semantic sense.

In compilation graphs with branching factors:
```
                    ┌─ [Active: type-check] ─→ codegen
Source ─→ Parse ───┤
                    └─ [Thunk: optimize] ─→ (never forced in debug build)
```

The optimizer thunk was "right" to exist - it represented capability without commitment. Its eventual garbage collection is success, not waste.

## Trait-Based Reflection System

Inspired by m2's optics crate, we've developed a trait-based reflection system that operates alongside (or instead of) UE's USTRUCT/UPROPERTY macros.

### Philosophy

UE's reflection requires:
- `USTRUCT()` macro on the type
- `GENERATED_BODY()` inside the struct
- `UPROPERTY()` on each reflected field
- UnrealHeaderTool (UHT) preprocessing
- Generated `.generated.h` files

Our trait-based approach:
- Just C++20 traits and concepts
- No code generation step
- Compile-time attribute queries via partial template matching
- Zero runtime overhead (static constexpr metadata)
- User controls struct closing brace (two-part macro pattern)
- Perfect forwarding for non-copyable types
- POD detection for efficient serialization

### Core Components

| File | Purpose |
|------|---------|
| `FlightReflection.h` | Trait-based type registration, attributes, serialization |
| `FlightTemplateMatching.h` | Deep dive on partial template matching |
| `FlightPropertyOptics.h` | Pattern matching on FProperty, tree traversal |
| `FlightMassOptics.h` | Declarative Mass ECS queries, archetype patterns |
| `FlightReflectionExamples.h` | Concrete usage examples |

### Basic Usage (Two-Part Macro Pattern)

```cpp
#include "Core/FlightReflection.h"
using namespace Flight::Reflection;

// Define a reflectable type - user controls closing brace!
struct FPathFollowData
{
    FGuid PathId;
    float CurrentDistance = 0.f;
    float DesiredSpeed = 1500.f;
    bool bLooping = true;

    // Can add methods after the macro
    float GetProgress() const { return CurrentDistance; }

    FLIGHT_REFLECT_BODY(FPathFollowData)
};  // <-- User controls this brace

// Trait specialization OUTSIDE the struct
FLIGHT_REFLECT_FIELDS_ATTR(FPathFollowData,
    FLIGHT_FIELD_ATTR(FGuid, PathId,
        Attr::VisibleAnywhere,
        Attr::Transient
    ),
    FLIGHT_FIELD_ATTR(float, DesiredSpeed,
        Attr::EditAnywhere,
        Attr::BlueprintReadWrite,
        Attr::Replicated,
        Attr::ClampedValue<0, 10000>
    ),
    FLIGHT_FIELD_ATTR(bool, bLooping,
        Attr::EditAnywhere,
        Attr::SaveGame,
        Attr::Category<"Path">
    )
)

// Compile-time queries
static_assert(CReflectable<FPathFollowData>);
using Fields = TReflectionTraits<FPathFollowData>::Fields;
using SpeedField = Fields::At<1>;
static_assert(SpeedField::IsEditable);
static_assert(SpeedField::IsReplicated);
static_assert(SpeedField::HasAttrTemplate<Attr::ClampedValue>);  // Partial match!
```

### Attribute System

Phantom types encode field metadata at compile time:

```cpp
namespace Attr {
    // Simple attributes (exact type match)
    struct EditAnywhere {};
    struct VisibleAnywhere {};
    struct BlueprintReadOnly {};
    struct BlueprintReadWrite {};
    struct Transient {};
    struct SaveGame {};
    struct Replicated {};

    // Parameterized attributes (partial template match)
    template<TStaticString Str>
    struct Category { static constexpr auto Value = Str; };

    template<int32 Min, int32 Max>
    struct ClampedValue {
        static constexpr int32 MinValue = Min;
        static constexpr int32 MaxValue = Max;
    };
}
```

### Partial Template Matching for Parameterized Attributes

A key insight: simple `std::is_same_v` fails for parameterized attributes because each instantiation is a different type:

```cpp
// Problem: different instantiations are different types!
static_assert(!std::is_same_v<Category<"Movement">, Category<"State">>);

// So we can't ask "does this have ANY Category?"
// std::is_same_v only does exact matching
```

**Solution**: Partial specialization as pattern matching:

```cpp
// Primary template: assume NOT a specialization
template<typename T, template<auto...> class Template>
struct TIsSpecializationOfNTTP : std::false_type {};

// Partial specialization: IS a specialization when T = Template<Args...>
template<template<auto...> class Template, auto... Args>
struct TIsSpecializationOfNTTP<Template<Args...>, Template> : std::true_type {};
```

**How it works** (step by step):

```cpp
template<int N> struct Limit {};
using Limit100 = Limit<100>;

// Query: TIsSpecializationOfNTTP<Limit100, Limit>
// Expands to: TIsSpecializationOfNTTP<Limit<100>, Limit>

// Compiler tries partial specialization:
//   Pattern: TIsSpecializationOfNTTP<Template<Args...>, Template>
//
// Unification:
//   Template = Limit           ✓ (matches second param)
//   Template<Args...> = Limit<100>
//   → Limit<Args...> = Limit<100>
//   → Args... = {100}          ✓ (deduced!)
//
// Match succeeds → true_type
```

**Visual diagram**:

```
Query: Is Limit<100> a specialization of Limit<>?

TIsSpecializationOfNTTP< Limit<100> , Limit >
                         ↑            ↑
                         T            Template

Partial spec pattern:
TIsSpecializationOfNTTP< Template<Args...> , Template >
                         ↑                   ↑
                         Must equal T        Must equal Template

The pattern Template<Args...> matches ANY instantiation of Template,
with Args... capturing whatever arguments were used.
```

**Usage in TAttributeSet**:

```cpp
template<typename... Attrs>
struct TAttributeSet
{
    // Exact match for simple attributes
    template<typename A>
    static constexpr bool Has = (std::is_same_v<A, Attrs> || ...);

    // Partial match for parameterized attributes
    template<template<auto...> class Template>
    static constexpr bool HasTemplate =
        (TIsSpecializationOfNTTP<Attrs, Template>::value || ...);
};

using MyAttrs = TAttributeSet<
    Attr::EditAnywhere,
    Attr::Category<"Movement">,
    Attr::ClampedValue<0, 100>
>;

// Exact match
static_assert(MyAttrs::Has<Attr::EditAnywhere>);           // true
static_assert(!MyAttrs::Has<Attr::Category<"State">>);     // false (different string)

// Partial match - "has ANY Category?"
static_assert(MyAttrs::HasTemplate<Attr::Category>);       // true!
static_assert(MyAttrs::HasTemplate<Attr::ClampedValue>);   // true!
static_assert(!MyAttrs::HasTemplate<Attr::UIMin>);         // false (not present)
```

This is the same mechanism the standard library uses for type traits like `std::is_same` - partial specialization as compile-time pattern matching on types.

### Static Constexpr Field Names

Field names are stored as static constexpr data in the type, not as instance members:

```cpp
template<typename TOwner, typename TField, auto MemberPtr, TStaticString FieldName>
struct TFieldDescriptor
{
    // Name is STATIC - lives in the type, not an instance
    static constexpr std::string_view Name{FieldName};
    static constexpr const char* NameCStr = FieldName.CStr();

    // Access is also static
    static TField& Get(TOwner& Owner) { return Owner.*MemberPtr; }
};
```

This means:
- No nullptr issues from default-constructed descriptors
- Name survives across template instantiations
- Zero runtime storage per-instance

### Field Iteration

```cpp
FPathFollowData Data;

// Iterate all fields
ForEachField(Data, [](auto& Value, const TCHAR* Name) {
    UE_LOG(LogTemp, Log, TEXT("Field: %s"), Name);
});

// For nested reflectable types, iterate deep
ForEachFieldDeep(Entity, [](auto& Value, const TCHAR* Name) {
    UE_LOG(LogTemp, Log, TEXT("Deep: %s"), Name);
});

// Filter by attribute
using EditableFilter = TFilteredFieldList<Fields, TIsEditable>;
EditableFilter::ForEach([](auto Field) {
    // Only editable fields
});
```

### Serialization Without UPROPERTY

```cpp
FPathFollowData Data;
Data.DesiredSpeed = 2500.f;

// Serialize to buffer
TArray<uint8> Buffer;
{
    FMemoryWriter Writer(Buffer);
    Serialize(Data, Writer);  // Uses reflection traits
}

// Deserialize
FPathFollowData Loaded;
{
    FMemoryReader Reader(Buffer);
    Serialize(Loaded, Reader);
}
```

### Diff/Patch for Undo/Redo

```cpp
FPathFollowData Before, After;
After.DesiredSpeed = 2000.f;  // Changed

// Generate patch (only changed fields)
auto Patch = Diff(Before, After);

// Apply to another instance
FPathFollowData Target;
Apply(Target, Patch);
```

### Observable Fields

Automatic change notifications:

```cpp
TObservableField<float> Speed{1500.f};

Speed.Bind([](const float& Old, const float& New) {
    UE_LOG(LogTemp, Log, TEXT("Speed: %.1f -> %.1f"), Old, New);
});

Speed = 2000.f;  // Triggers callback
```

### Property Optics (Pattern Matching)

Pattern match on UE's FProperty types:

```cpp
#include "Core/FlightPropertyOptics.h"
using namespace Flight::PropertyOptics;

FString TypeName = Match<FString>(Prop)
    .Case<FStructProperty>([](auto* S) {
        return S->Struct->GetName();
    })
    .Case<FArrayProperty>([](auto* A) {
        return FString::Printf(TEXT("Array<%s>"), *A->Inner->GetCPPType());
    })
    .Default([](auto* P) {
        return P->GetCPPType();
    });
```

Tree traversal over property hierarchies:

```cpp
// Count object references in a struct
int32 RefCount = FoldProperties(RootProp, 0, [](int32 Acc, FProperty* P) {
    return Acc + (IsObjectRef()(P) ? 1 : 0);
});

// Find all FVector properties
auto Vectors = FilterProperties(Prop, IsStructType<FVector>());

// Check if any property has metadata
bool HasBPVisible = AnyProperty(Prop, HasMeta(TEXT("BlueprintVisible")));
```

### Mass ECS Optics (Declarative Queries)

```cpp
#include "Core/FlightMassOptics.h"
using namespace Flight::MassOptics;

// Declarative query (replaces AddRequirement calls)
using FlightPathQuery = QueryPattern<
    Write<FFlightPathFollowFragment>,
    Read<FFlightTransformFragment>,
    Has<FFlightSwarmMemberTag>
>;

void ConfigureQueries()
{
    FlightPathQuery::Configure(EntityQuery);
}

// Type-safe fragment access
void Execute(FMassExecutionContext& Context)
{
    auto [PathFrags, TransformFrags] = TTypedContext<
        Write<FFlightPathFollowFragment>,
        Read<FFlightTransformFragment>
    >::Bind(Context);

    for (int32 i : Context)
    {
        PathFrags[i].CurrentDistance += DeltaTime * PathFrags[i].DesiredSpeed;
    }
}
```

Archetype rewrite rules:

```cpp
// Define migration rule
using MigrateToV2 = RewriteRule<
    Has<FLegacyFragment>,
    Without<FNewFragment>
>::Into<
    Without<FLegacyFragment>,
    Has<FNewFragment>
>;

// Apply migration
if (MigrateToV2::Matches(Archetype))
{
    auto NewArchetype = MigrateToV2::Apply(Archetype);
}
```

### When to Use Which

| System | Use Case |
|--------|----------|
| UE USTRUCT | Actor/Component properties exposed to Editor |
| Flight Reflection | Internal data structures, ECS fragments, hot paths |
| PropertyOptics | Analyzing/transforming UE's existing reflection |
| MassOptics | Declarative Mass ECS query definition |

The systems coexist - a type can have both USTRUCT and FLIGHT_REFLECT if needed.

### Connection to m2 Optics

This design is inspired by the Rust m2 optics crate:

| m2 Concept | C++ Implementation |
|------------|-------------------|
| `Tree` trait | `GetPropertyChildren`, `ForEachProperty` |
| `Pattern` trait | `TPropertyMatcher`, `Match<T>()` |
| `Rewriter` | `Diff`/`Apply`, `RewriteRule` |
| Match guards | Template constraints, `if constexpr` |

## Type Theory and Type-Level Programming

### Why Reflection Matters (Type-Theoretic View)

Reflection transforms types from **static compile-time artifacts** into **data that code can manipulate**. This enables:

| Capability | Without Reflection | With Reflection |
|------------|-------------------|-----------------|
| Serialization | Manual `<<` per type | `Serialize(any_reflectable)` |
| Introspection | Can't ask "what fields?" | `ForEachField(x, visitor)` |
| Schema evolution | Manual migration code | Diff/patch by field name |
| Editor tooling | Custom UI per type | Generated from traits |
| Validation | if-checks everywhere | `ClampedValue<0,100>` declarative |
| Debugging | Manual ToString() | Auto-print all fields |

The key insight: reflection lets us write **generic algorithms over structure**.

### Fundamental Type Constructors

In type theory, there are primary ways to construct new types:

```
┌─────────────────────────────────────────────────────────────────────┐
│                    TYPE CONSTRUCTOR HIERARCHY                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  PRODUCT TYPES (×)              SUM TYPES (+)                       │
│  "Has A AND B"                  "Has A OR B"                        │
│  ─────────────────              ────────────                        │
│  struct { A a; B b; }           std::variant<A, B>                  │
│  std::tuple<A, B>               TResult<T, E>                       │
│  std::pair<A, B>                TOptional<T>                        │
│                                                                      │
│  FUNCTION TYPES (→)             INTERSECTION (∩)                    │
│  "Transforms A to B"            "Has all of A AND all of B"         │
│  ─────────────────              ───────────────────────             │
│  B(*)(A)                        TTypeComposition::Intersection      │
│  std::function<B(A)>            (TypeScript's A & B)                │
│  TFunction<B(A)>                                                    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

**Our implementation** in `FlightReflection.h`:

```cpp
template<typename... Components>
struct TTypeComposition
{
    // Set membership test
    template<typename T>
    static constexpr bool Has = (std::is_same_v<T, Components> || ...);

    // UNION: types in A ∪ B
    template<typename... Others>
    using Union = TTypeComposition<Components..., Others...>;

    // INTERSECTION: types in A ∩ B
    template<typename... Others>
    struct Intersection
    {
        template<typename T>
        static constexpr bool InBoth =
            (std::is_same_v<T, Components> || ...) &&
            (std::is_same_v<T, Others> || ...);
    };

    // DIFFERENCE: types in A but not B
    template<typename... Others>
    struct Difference
    {
        template<typename T>
        static constexpr bool InOthers = (std::is_same_v<T, Others> || ...);

        // Filter out types that are in Others
        using Type = /* filtered Components... */;
    };
};
```

### Extended Type Constructors

Beyond the fundamentals, there are more exotic type constructors:

| Constructor | Math Notation | C++ Approximation | Use Case |
|-------------|---------------|-------------------|----------|
| **Refinement** | {x:T \| P(x)} | `TRefined<T,P>` | Constrained values |
| **Dependent** | Π(x:A).B(x) | Template NTTPs | Size-parameterized |
| **Row** | {l₁:T₁, l₂:T₂...} | `TRow<Fields...>` | Extensible records |
| **Effect** | T ! E | `TEffectful<T,E>` | Track side effects |
| **Linear** | A ⊸ B | `TLinear<T>` | Must use exactly once |
| **Higher-Kinded** | F[_] | `template<template<typename>class>` | Abstract over containers |

**Refinement Types** (we have a limited form):

```cpp
// Our ClampedValue is a refinement type!
template<int32 Min, int32 Max>
struct ClampedValue {};  // { x : int32 | Min <= x <= Max }

// More general form:
template<typename T, auto Predicate>
struct TRefined
{
    T Value;

    // Construction validates
    static TOptional<TRefined> Make(T InValue)
    {
        if (Predicate(InValue)) return TRefined{InValue};
        return {};
    }

    // Predicate available at compile-time
    static constexpr auto Validate = Predicate;
};

// Usage: positive floats
constexpr auto IsPositive = [](float x) { return x > 0.0f; };
using PositiveFloat = TRefined<float, IsPositive>;
```

### Type-Level Programming: Iter/Map/Filter on Types

Template metaprogramming gives us **the same operations on types that we have on values**:

```
┌─────────────────────────────────────────────────────────────────────┐
│            VALUE-LEVEL  ←→  TYPE-LEVEL CORRESPONDENCE               │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  Values          →  Types                                           │
│  Functions       →  Template metafunctions                          │
│  Lists           →  Parameter packs (typename... Ts)                │
│  Iteration       →  Fold expressions ((f(Ts), ...))                 │
│  Map             →  Transform each type in pack                     │
│  Filter          →  Conditional type selection                      │
│  Reduce/Fold     →  Recursive templates or fold expressions         │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

**Type-Level Map** (transform each type):

```cpp
// Value-level: transform each element
auto values = {1, 2, 3};
auto doubled = values | std::views::transform([](int x) { return x * 2; });

// Type-level: transform each type
template<typename... Ts>
struct TypeList {};

// Metafunction: add pointer to type
template<typename T>
struct AddPointer { using Type = T*; };

// Map over type list
template<typename List, template<typename> class F>
struct TMap;

template<typename... Ts, template<typename> class F>
struct TMap<TypeList<Ts...>, F>
{
    using Type = TypeList<typename F<Ts>::Type...>;
};

// Usage:
using Originals = TypeList<int, float, FString>;
using Pointers = TMap<Originals, AddPointer>::Type;
// Result: TypeList<int*, float*, FString*>
```

**Type-Level Filter** (select types matching predicate):

```cpp
// Value-level: filter elements
auto evens = values | std::views::filter([](int x) { return x % 2 == 0; });

// Type-level: filter types
template<typename T>
struct IsFloatingPoint : std::bool_constant<std::is_floating_point_v<T>> {};

// Filter implementation (recursive)
template<typename List, template<typename> class Pred>
struct TFilter;

// Base case: empty list
template<template<typename> class Pred>
struct TFilter<TypeList<>, Pred>
{
    using Type = TypeList<>;
};

// Recursive case: check head, recurse on tail
template<typename Head, typename... Tail, template<typename> class Pred>
struct TFilter<TypeList<Head, Tail...>, Pred>
{
private:
    using FilteredTail = typename TFilter<TypeList<Tail...>, Pred>::Type;
public:
    using Type = std::conditional_t<
        Pred<Head>::value,
        typename Prepend<Head, FilteredTail>::Type,  // Include Head
        FilteredTail                                  // Skip Head
    >;
};

// Usage:
using Mixed = TypeList<int, float, FString, double>;
using Floats = TFilter<Mixed, IsFloatingPoint>::Type;
// Result: TypeList<float, double>
```

**Type-Level Fold** (reduce types to single result):

```cpp
// Value-level: reduce to single value
int sum = std::accumulate(values.begin(), values.end(), 0);

// Type-level: reduce types to single type/value
template<typename List, typename Init, template<typename, typename> class Op>
struct TFold;

template<typename Init, template<typename, typename> class Op>
struct TFold<TypeList<>, Init, Op>
{
    using Type = Init;
};

template<typename Head, typename... Tail, typename Init, template<typename, typename> class Op>
struct TFold<TypeList<Head, Tail...>, Init, Op>
{
    using Type = typename TFold<
        TypeList<Tail...>,
        typename Op<Init, Head>::Type,
        Op
    >::Type;
};

// Example: compute total size of types
template<typename Acc, typename T>
struct AddSize
{
    using Type = std::integral_constant<size_t, Acc::value + sizeof(T)>;
};

using Types = TypeList<int, float, double>;
constexpr size_t TotalSize = TFold<Types, std::integral_constant<size_t, 0>, AddSize>::Type::value;
// Result: 4 + 4 + 8 = 16
```

**Our TFieldList already does this!**

```cpp
template<typename... Fields>
struct TFieldList
{
    // Type-level iteration (fold expression)
    template<typename F>
    static void ForEach(F&& Func)
    {
        (Func.template operator()<Fields>(), ...);  // Type-level map!
    }

    // Type-level fold
    template<typename TAccum, typename F>
    static TAccum Fold(TAccum Initial, F&& Func)
    {
        TAccum Accum = MoveTemp(Initial);
        ((Accum = Func(MoveTemp(Accum), Fields{})), ...);
        return Accum;
    }

    // Type-level indexing
    template<size_t I>
    using At = std::tuple_element_t<I, std::tuple<Fields...>>;
};
```

### Row Types: Extensible Records

Row types treat records as **sets of labeled fields** that can be extended/restricted:

```cpp
// ═══════════════════════════════════════════════════════════════════
// ROW TYPES - Extensible Records with Type-Safe Field Operations
// ═══════════════════════════════════════════════════════════════════

// Field descriptor: label + type
template<TStaticString Label, typename T>
struct TField
{
    static constexpr std::string_view Name{Label};
    using Type = T;
    T Value{};
};

// Row: collection of labeled fields
template<typename... Fields>
struct TRow : Fields...
{
    // Check if row has a field
    template<TStaticString Label>
    static constexpr bool Has = ((Fields::Name == std::string_view{Label}) || ...);

    // Get field type by label
    template<TStaticString Label>
    using FieldType = /* find matching Field::Type */;

    // ─────────────────────────────────────────────────────────────
    // Row Operations (Type-Level)
    // ─────────────────────────────────────────────────────────────

    // EXTEND: Add new field
    template<TStaticString Label, typename T>
    using Add = TRow<Fields..., TField<Label, T>>;

    // RESTRICT: Remove field (type-level filter)
    template<TStaticString Label>
    using Remove = /* TRow without field named Label */;

    // MERGE: Combine two rows
    template<typename OtherRow>
    using Merge = /* TRow<Fields..., OtherRow::Fields...> */;

    // ─────────────────────────────────────────────────────────────
    // Value Operations
    // ─────────────────────────────────────────────────────────────

    // Get field value
    template<TStaticString Label>
    auto& Get() { return static_cast<TField<Label, FieldType<Label>>&>(*this).Value; }

    // Set field value
    template<TStaticString Label, typename U>
    void Set(U&& Value) { Get<Label>() = Forward<U>(Value); }
};

// Usage:
using Point2D = TRow<
    TField<"x", float>,
    TField<"y", float>
>;

using Point3D = Point2D::Add<"z", float>;

using ColoredPoint = Point3D::Merge<TRow<
    TField<"r", uint8>,
    TField<"g", uint8>,
    TField<"b", uint8>
>>;

// Row polymorphism: works on ANY row with "x" and "y"
template<typename R>
    requires R::template Has<"x"> && R::template Has<"y">
float Distance(const R& Point)
{
    float x = Point.template Get<"x">();
    float y = Point.template Get<"y">();
    return FMath::Sqrt(x*x + y*y);
}
```

**Why Row Types for ECS?**

```cpp
// Entity as row type
using BaseEntity = TRow<
    TField<"Position", FVector>,
    TField<"Rotation", FQuat>
>;

using MovingEntity = BaseEntity::Add<"Velocity", FVector>;
using RenderableEntity = BaseEntity::Add<"Mesh", UStaticMesh*>;
using FullEntity = MovingEntity::Merge<TRow<TField<"Mesh", UStaticMesh*>>>;

// Generic system: works on any entity with Position + Velocity
template<typename E>
    requires E::template Has<"Position"> && E::template Has<"Velocity">
void IntegrateMotion(E& Entity, float DeltaTime)
{
    Entity.template Get<"Position">() +=
        Entity.template Get<"Velocity">() * DeltaTime;
}
```

### Variant Combinators

Sum types (variants) have their own set of combinators:

```cpp
// ═══════════════════════════════════════════════════════════════════
// VARIANT COMBINATORS - Operations on Sum Types
// ═══════════════════════════════════════════════════════════════════

// Our TResult<T, E> is a variant: Either<E, T>
// These combinators work on any variant-like type

// ─────────────────────────────────────────────────────────────────
// MAP: Transform the contained value
// ─────────────────────────────────────────────────────────────────

template<typename Variant, typename F>
auto VariantMap(Variant&& V, F&& Func)
{
    return std::visit([&](auto&& Value) {
        return std::invoke(Forward<F>(Func), Forward<decltype(Value)>(Value));
    }, Forward<Variant>(V));
}

// TResult already has this:
auto Result = GetValue()
    .Map([](int x) { return x * 2; });  // Transform Ok value

// ─────────────────────────────────────────────────────────────────
// MATCH: Pattern match on alternatives (exhaustive)
// ─────────────────────────────────────────────────────────────────

// Overload helper for std::visit
template<typename... Ts>
struct TOverload : Ts... { using Ts::operator()...; };

template<typename... Ts>
TOverload(Ts...) -> TOverload<Ts...>;

// Usage:
std::variant<int, FString, FVector> Value = 42;

auto Result = std::visit(TOverload{
    [](int i)           { return FString::Printf(TEXT("int: %d"), i); },
    [](const FString& s){ return FString::Printf(TEXT("string: %s"), *s); },
    [](const FVector& v){ return v.ToString(); }
}, Value);

// ─────────────────────────────────────────────────────────────────
// BIND/FLATMAP: Chain variant-producing operations
// ─────────────────────────────────────────────────────────────────

// TResult already has this as AndThen:
auto ChainedResult = ParseInt(Input)
    .AndThen([](int x) { return Validate(x); })   // Returns TResult
    .AndThen([](int x) { return Transform(x); }); // Returns TResult

// For general variants:
template<typename Variant, typename F>
auto VariantBind(Variant&& V, F&& Func)
{
    // F returns another variant
    return std::visit([&](auto&& Value) {
        return std::invoke(Forward<F>(Func), Forward<decltype(Value)>(Value));
    }, Forward<Variant>(V));
}

// ─────────────────────────────────────────────────────────────────
// FOLD: Collapse variant to single type
// ─────────────────────────────────────────────────────────────────

// All handlers return same type
FString Description = std::visit(TOverload{
    [](int i)           -> FString { return TEXT("number"); },
    [](const FString& s)-> FString { return TEXT("text"); },
    [](const FVector& v)-> FString { return TEXT("vector"); }
}, Value);

// ─────────────────────────────────────────────────────────────────
// TRAVERSE: Map + Sequence for effectful operations
// ─────────────────────────────────────────────────────────────────

// Process array of Results, fail on first error
template<typename T, typename E>
TResult<TArray<T>, E> Sequence(TArray<TResult<T, E>>&& Results)
{
    TArray<T> Successes;
    for (auto&& R : Results)
    {
        if (R.IsErr()) return TResult<TArray<T>, E>::Err(R.UnwrapErr());
        Successes.Add(R.Unwrap());
    }
    return TResult<TArray<T>, E>::Ok(MoveTemp(Successes));
}
```

### Type-Level Decision Tree

Choosing the right abstraction:

```
Need to combine data?
├─ All fields present simultaneously → PRODUCT (struct/tuple)
├─ One of several alternatives → SUM (variant/TResult)
└─ Properties from multiple sources → INTERSECTION

Need to constrain types?
├─ Value must satisfy predicate → REFINEMENT (ClampedValue)
├─ Type depends on value → DEPENDENT (template<int N>)
└─ Resource must be consumed → LINEAR

Need to abstract over structure?
├─ Extensible records → ROW TYPES
├─ Abstract over containers → HIGHER-KINDED
└─ Track side effects → EFFECT TYPES

Need to transform types?
├─ Apply function to each → TYPE-LEVEL MAP
├─ Select matching types → TYPE-LEVEL FILTER
├─ Combine to single result → TYPE-LEVEL FOLD
└─ Check type properties → CONCEPTS/CONSTRAINTS
```

### Implemented: Row Types

Row types are now implemented in `FlightRowTypes.h`. See the file for full documentation.

```cpp
#include "Core/FlightRowTypes.h"
using namespace Flight::RowTypes;

// Define row types
using Point2D = TRow<TRowField<"x", float>, TRowField<"y", float>>;
using Point3D = Point2D::Add<"z", float>;

// Row polymorphism: works on ANY row with x and y
template<typename R>
    requires CHasFields<R, "x", "y">
float Distance(const R& Point)
{
    float X = Point.template Get<"x">();
    float Y = Point.template Get<"y">();
    return FMath::Sqrt(X*X + Y*Y);
}

// Works on Point2D, Point3D, or any row with x/y!
Point2D p2{3.0f, 4.0f};
Point3D p3; p3.Set<"x">(3.0f); p3.Set<"y">(4.0f); p3.Set<"z">(0.0f);

float d1 = Distance(p2);  // 5.0
float d2 = Distance(p3);  // 5.0 - same function!
```

### Future Explorations

Areas for potential development:

1. **Effect Tracking** - Mark functions with IO/Throws/Pure effects
2. **Refinement Type Library** - General `TRefined<T, Predicate>` with composition
3. **Variant Algebra** - Full suite of variant combinators
4. **Type-Level State Machine** - Encode valid state transitions in types
5. **Capability-Based Types** - Phantom permissions like `TAccessContext`
6. **Row Type Extensions** - Optional fields, default values, field constraints

## See Also

### Core Functional Patterns
- `Source/FlightProject/Public/Core/FlightFunctional.h` - Core functional utilities
- `Source/FlightProject/Public/Core/FlightReflection.h` - Trait-based reflection
- `Source/FlightProject/Public/Core/FlightRowTypes.h` - Row types (extensible records)
- `Source/FlightProject/Public/Core/FlightTemplateMatching.h` - Partial template matching deep dive
- `Source/FlightProject/Public/Core/FlightPropertyOptics.h` - Property tree patterns
- `Source/FlightProject/Public/Core/FlightMassOptics.h` - Mass ECS optics
- `Source/FlightProject/Public/Core/FlightReflectionExamples.h` - Usage examples

### UI Framework
- `Source/FlightProject/Public/UI/FlightSlate.h` - Declarative widget builders
- `Source/FlightProject/Public/UI/FlightReactiveUI.h` - Reactive data binding for Slate
- `Source/FlightProject/Public/UI/FlightUIExamples.h` - UI usage examples

### Platform Integration
- `Source/FlightProject/Public/Platform/FlightLinuxPlatform.h` - Linux/Wayland platform utilities
- `Docs/Architecture/IoUringGpuIntegration.md` - GPU bridge using functional patterns
