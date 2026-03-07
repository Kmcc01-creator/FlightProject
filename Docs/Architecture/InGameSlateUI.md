# Architectural Spec: In-Game Slate UI

## 1. Vision: Composition over Blueprint
In `FlightProject`, we prioritize high-performance, data-centric interfaces. While Unreal Engine's standard for in-game UI is **UMG (Unreal Motion Graphics)**, we utilize **Direct Slate** for all gameplay overlays and HUDs.

### Why Slate for In-Game?
- **Zero UObject Overhead**: UMG widgets are `UObjects`, adding significant memory and GC pressure in high-scale simulations. Slate widgets are lightweight C++ shared pointers.
- **Unified Reactive Core**: By using Slate, we can bind directly to our `Flight::Reactive` primitives without the "Property Binding" or "Event Dispatcher" boilerplate required by Blueprints.
- **C++23 Modernity**: Leverage monadic chaining, traits, and compile-time reflection (`FlightMassOptics`) to build interfaces.

## 2. The Reactive Binding Tiers

We use a tiered approach to bridge data from the simulation to the screen:

| Tier | Primitive | Best For... | Allocation |
|------|-----------|-------------|------------|
| **High-Perf** | `TObservableField<T>` | Mass ECS Fragment data, per-entity stats. | Zero (Stack/Inline) |
| **Logic/UI** | `TReactiveValue<T>` | Global settings, HUD state, menu toggles. | Managed (Heap) |
| **Composite** | `TStateContainer<T>` | Grouped reflectable structs (e.g. `FDroneStatus`). | Managed (Heap) |

## 3. Core Framework: SReactiveWidget

Every in-game interface inherits from `SReactiveWidget` (defined in `FlightReactiveSlate.h`). This base class provides an `FReactiveContext` that automatically handles subscription cleanups when the widget is destroyed.

### Implementation Pattern:
```cpp
class SFlightHudOverlay : public SReactiveWidget
{
public:
    SLATE_BEGIN_ARGS(SFlightHudOverlay) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, TSharedRef<FFlightHudViewModel> ViewModel)
    {
        ChildSlot
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            .Alignment(HAlign_Left, VAlign_Top)
            [
                SAssignNew(AltitudeText, STextBlock)
            ]
        ];

        // Reactive Binding: Updates whenever ViewModel->Altitude changes
        Bindings::BindNumericText(ReactiveContext, AltitudeText.ToSharedRef(), ViewModel->Altitude);
    }

private:
    TSharedPtr<STextBlock> AltitudeText;
};
```

## 4. Bridging Mass ECS to UI

To display data from the 100k+ entity simulation without polling:

1.  **Fragment Definition**: Use `TObservableField` inside the Mass fragment.
    ```cpp
    struct FFlightStatusFragment : public FMassFragment {
        TObservableField<float> BatteryLevel;
    };
    ```
2.  **System Update**: The Mass Processor assigns values; the field triggers the callback *if and only if* the value changed.
3.  **UI Link**: The HUD widget binds a callback to the specific entity's observable field.

## 5. Viewport Integration

In-game Slate widgets are added to the game viewport via the `GameViewportClient`:

```cpp
void AFlightHUD::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    
    if (GEngine && GEngine->GameViewport)
    {
        SAssignNew(HudWidget, SFlightHudOverlay, MyViewModel);
        GEngine->GameViewport->AddViewportWidgetContent(HudWidget.ToSharedRef());
    }
}
```

## 6. Current Status & Roadmap
- [x] Core Reactive primitives (`FlightReactive.h`)
- [x] Slate lifecycle bindings (`FlightReactiveSlate.h`)
- [x] Reflection-driven UI generation (`FlightReflectedUI.h`)
- [ ] Transition `AFlightHUD` from legacy Canvas to `SReactiveWidget`.
- [ ] Implement `FMassReactiveBridge` to automate entity-to-UI observable binding.
