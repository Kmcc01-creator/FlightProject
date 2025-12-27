# DevNote 004: The Rust Developer's Guide to Unreal C++

**Date:** December 2025
**Target Audience:** Rust Developers migrating to Modern Unreal Engine (5.7+)

Unreal C++ is historically "C with Classes," but modern UE5 + C++20 has adopted many patterns that will feel familiar to a Rustacean, albeit with different syntax and safety guarantees.

---

## 1. Ownership & Memory ( The "Borrow Checker" Replacement)

In Rust, ownership is enforced at compile time. In Unreal, it is enforced at runtime via two distinct systems. You must know which system owns your data.

| Rust Concept | Unreal Equivalent | Notes |
| :--- | :--- | :--- |
| `Box<T>` | `TUniquePtr<T>` | Unique ownership. Non-copyable. Rare in gameplay code, common in low-level tools. |
| `Rc<T>` / `Arc<T>` | `TSharedPtr<T>` / `TSharedRef<T>` | Reference counted. `TSharedRef` is non-nullable (like `Rc`), `TSharedPtr` is nullable (like `Option<Rc>`). **Use for pure C++ structs (F-types).** |
| `Weak<T>` | `TWeakPtr<T>` | Weak reference for F-types. |
| **Garbage Collection** | `UObject*` / `TObjectPtr<T>` | **The Gameplay Standard.** If it inherits `UObject` (Actors, Components), you **cannot** use smart pointers. You use raw pointers, and the GC keeps them alive if they are referenced by a `UPROPERTY`. |
| `&T` (Borrow) | `T*` or `T&` | Raw pointers/references. In UE, raw pointers are usually non-owning "borrows". |

### The "UObject" Rule
If you `NewObject<T>()`, the Engine owns it.
*   **Strong Ref:** Store it in a `UPROPERTY()` variable.
*   **Weak Ref:** Use `TWeakObjectPtr<T>`. **Always** use this for UI or caching references to Actors that might be destroyed (killed) while you hold the pointer.

---

## 2. Traits vs. Interfaces

Rust `Traits` define behavior. Unreal has `Interfaces`, but they are messier due to C++ legacy.

### Rust
```rust
trait Flyable {
    fn fly(&self);
}
impl Flyable for Bird { ... }
```

### Unreal (The "I" and "U" Split)
Unreal interfaces generate *two* classes: `UInterface` (for reflection) and `IInterface` (vtable).

```cpp
// 1. Definition
UINTERFACE(MinimalAPI, Blueprintable)
class UFlyable : public UInterface { GENERATED_BODY() };

class IFlyable
{
    GENERATED_BODY()
public:
    // C++ only virtual
    virtual void Fly() = 0; 

    // Blueprint/C++ hybrid (The "Event" pattern)
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    void OnFly();
};

// 2. Implementation
class ABird : public AActor, public IFlyable
{
    virtual void Fly() override { ... } // C++ impl
    virtual void OnFly_Implementation() override { ... } // BP impl
};

// 3. Usage (The tricky part)
// You cannot Cast<IFlyable>(Obj) safely if it's a Blueprint implementing the interface.
// You must use:
if (Obj->Implements<UFlyable>()) 
{
    IFlyable::Execute_OnFly(Obj); // Static helper for BP calls
}
```
*Tip:* For pure C++ systems (like Mass), prefer simple C++ abstract base classes or Templates (Concepts) over `UINTERFACE`.

---

## 3. Closures & Delegators (Events)

Rust uses `Fn`, `FnMut`, `FnOnce`. Unreal uses **Delegates**.

### Single-Cast Delegate (`Fn`)
Calls exactly one function. Often used for return values.
```cpp
DECLARE_DELEGATE_OneParam(FOnItemPickedUp, int32 /*ItemID*/);
FOnItemPickedUp OnPickup;

// Binding a Lambda (Closure)
OnPickup.BindLambda([](int32 Id) {
    UE_LOG(LogTemp, Log, TEXT("Picked up %d"), Id);
});
```

### Multi-Cast Delegate (`Vec<Fn>`)
Calls multiple functions. No return value. Used for Event dispatching.
```cpp
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDamageTaken, float);
FOnDamageTaken OnDamage;

OnDamage.Broadcast(50.f); // Calls everyone
```

### Dynamic Delegate (Blueprint Visible)
Slower, strictly for exposing events to the Blueprint VM.
```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FButtonPress);
UPROPERTY(BlueprintAssignable)
FButtonPress OnClicked;
```

### The "Weak Lambda" Pattern
In Rust, you capture `Weak<T>` to avoid cycles. In UE, `BindWeakLambda` is your best friend.
```cpp
// Safe! If 'this' is destroyed, the lambda will NOT run.
MyDelegate.BindWeakLambda(this, [this]() {
    this->DoSomething(); 
});
```

---

## 4. Iterators & Combinators (Algo::)

You are used to `.iter().map().filter().collect()`.
Unreal C++ has the `Algo` namespace (headers: `Algo/Transform.h`, `Algo/Accumulate.h`, etc.).

### Rust
```rust
let names: Vec<String> = actors.iter()
    .filter(|a| a.is_alive())
    .map(|a| a.get_name())
    .collect();
```

### Unreal
```cpp
// Range-based for is preferred for simple loops
TArray<FString> Names;
for (AActor* Actor : Actors)
{
    if (IsValid(Actor))
    {
        Names.Add(Actor->GetName());
    }
}

// "Functional" style (Algo)
TArray<FString> Names;
Algo::TransformIf(Actors, Names, 
    [](AActor* A) { return IsValid(A); }, // Predicate (Filter)
    [](AActor* A) { return A->GetName(); } // Transform (Map)
);
```
*Dev Tip:* `Algo` is powerful but verbose. For readable gameplay code, raw `for` loops are idiomatic in C++.

---

## 5. Result/Option vs. Checks

Unreal does not have a monadic `Result<T, E>`.
*   **Return Values:** Usually `bool` (Success/Fail) with an `OutParameter` reference.
*   **Nulls:** Pointers can be null. **Always** check `if (MyPtr)`.
*   **Soft Pointers:** `TSoftObjectPtr<T>` is like a "File Path" that might load into a "Pointer". You must check `IsNull()` or `IsValid()`.

## 6. Mass Entity (The "Rust-like" System)

Since you are using Mass, you will feel at home.
*   **Fragments** = Rust Structs (Data).
*   **Processors** = Systems.
*   **EntityQuery** = `Join` / View.
*   **ArrayView** = Slice `&[T]`.

**Mass is data-oriented.** You don't "call methods" on entities. You iterate slices of data. This is the closest you will get to Rust's performance characteristics in UE5.

## Quick Cheat Sheet

| Rust | Unreal C++ |
| :--- | :--- |
| `String` | `FString` (Heap) |
| `&str` | `FStringView` (Non-owning) |
| `Vec<T>` | `TArray<T>` |
| `HashMap<K, V>` | `TMap<K, V>` |
| `HashSet<T>` | `TSet<T>` |
| `println!` | `UE_LOG` |
| `#[derive(Debug)]` | `USTRUCT()` + Reflection |
| `mod` | `Module` (Build.cs) |
| `crate` | `Plugin` |
