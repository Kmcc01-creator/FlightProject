# Unreal Engine Python Reflection Guide

To make C++ classes, structs, and functions accessible to Unreal Engine's Python API, you must expose them to the reflection system using specific macros and specifiers.

## 1. Exposing Structs (`USTRUCT`)

Python wrappers for structs are generated automatically if the struct is marked as a `BlueprintType`.

```cpp
// MyStruct.h
#pragma once
#include "CoreMinimal.h"
#include "MyStruct.generated.h"

USTRUCT(BlueprintType) // <--- CRITICAL: Makes it visible to Python
struct FMyStruct
{
    GENERATED_BODY()

    // Properties must be EditAnywhere or BlueprintReadWrite to be accessible
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python Access")
    float MyFloatValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python Access")
    FString MyStringValue;
};
```

**Python Usage:**
```python
import unreal
my_data = unreal.MyStruct()
my_data.my_float_value = 10.5
```

## 2. Exposing Classes (`UCLASS`) & Functions (`UFUNCTION`)

To call a C++ function from Python, it must be a `UFUNCTION` with the `BlueprintCallable` specifier.

```cpp
// MyFunctionLibrary.h
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyFunctionLibrary.generated.h"

UCLASS()
class MYPROJECT_API UMyFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // "BlueprintCallable" exposes it to both Blueprints and Python
    UFUNCTION(BlueprintCallable, Category = "Python Tools")
    static void LogMessageFromPython(FString Message);
};
```

**Python Usage:**
```python
import unreal
unreal.MyFunctionLibrary.log_message_from_python("Hello from Script!")
```

## 3. Properties on Classes

Properties on `UCLASS` objects follow the same rules as structs.

```cpp
UCLASS(BlueprintType)
class AMyActor : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Python")
    int32 Score; 
};
```

**Python Usage:**
```python
# Assuming you have a reference to the actor 'my_actor'
current_score = my_actor.score
my_actor.set_editor_property("Score", 100) # Safer way to set in Editor
```

## 4. Key Takeaways

1.  **`BlueprintType`**: Required for Structs/Enums to be seen by Python.
2.  **`BlueprintCallable`**: Required for Functions to be called by Python.
3.  **`BlueprintReadWrite` / `EditAnywhere`**: Required for Properties to be read/modified.
4.  **Snake_Case**: Python API converts C++ `CamelCase` to `snake_case` automatically (e.g., `MyFunction` -> `my_function`).
