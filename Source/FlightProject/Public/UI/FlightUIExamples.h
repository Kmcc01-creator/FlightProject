// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightUIExamples - Usage examples for FlightSlate, ReactiveUI, and Platform
//
// This file demonstrates the integrated UI system combining:
//   - Declarative widget construction (FlightSlate)
//   - Reactive data binding (FlightReactiveUI)
//   - Platform-specific optimizations (FlightLinuxPlatform)

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/FlightSlate.h"
#include "UI/FlightReactiveUI.h"
#include "Platform/FlightLinuxPlatform.h"

namespace Flight::UI::Examples
{

using namespace Flight::Slate;
using namespace Flight::ReactiveUI;
using namespace Flight::Platform;


// ============================================================================
// Example 1: Simple Counter with Reactive Binding
// ============================================================================

class SReactiveCounter : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SReactiveCounter)
		: _InitialValue(0)
	{}
		SLATE_ARGUMENT(int32, InitialValue)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Counter = TReactiveValue<int32>(InArgs._InitialValue);

		// Create the text widget
		TSharedRef<STextBlock> CounterText = SNew(STextBlock)
			.Justification(ETextJustify::Center);

		// Bind reactive value to text
		Bindings::BindNumericText(CounterText, Counter, TEXT("Count: {0}"));

		// Build UI with declarative API
		ChildSlot
		[
			Border()
				.Background(FLinearColor(0.05f, 0.05f, 0.08f))
				.Padding(FMargin(20))
				.Content(
					VBox()
						+ CounterText
						+ HBox()
							.Padding(FMargin(0, 10, 0, 0))
							+ Button(TEXT("-"))
								.OnClick([this]() { Counter = Counter.Get() - 1; })
								.Padding(10)
								.Build()
							+ Button(TEXT("+"))
								.OnClick([this]() { Counter = Counter.Get() + 1; })
								.Padding(10)
								.Build()
						.Build()
					.Build()
				)
				.Build()
		];
	}

private:
	TReactiveValue<int32> Counter;
	FReactiveContext ReactiveContext;
};


// ============================================================================
// Example 2: Entity Status Panel (ECS Integration)
// ============================================================================

// State for a single entity
struct FEntityDisplayState
{
	FString Name;
	float Health = 100.0f;
	float Speed = 0.0f;
	FVector Position = FVector::ZeroVector;
	bool bIsActive = true;

	FLIGHT_REFLECT_BODY(FEntityDisplayState)
};

FLIGHT_REFLECT_FIELDS(FEntityDisplayState,
	FLIGHT_FIELD(FString, Name),
	FLIGHT_FIELD(float, Health),
	FLIGHT_FIELD(float, Speed),
	FLIGHT_FIELD(FVector, Position),
	FLIGHT_FIELD(bool, bIsActive)
)

class SEntityStatusPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEntityStatusPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		// Initialize reactive state
		EntityState = TStateContainer<FEntityDisplayState>();

		// Create bound widgets
		NameText = SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16));
		HealthText = SNew(STextBlock);
		SpeedText = SNew(STextBlock);
		PositionText = SNew(STextBlock);
		StatusIndicator = SNew(SBorder).Padding(FMargin(8, 4));

		// Subscribe to state changes
		EntityState.Subscribe([this](const FEntityDisplayState& Old, const FEntityDisplayState& New) {
			UpdateDisplay(New);
		});

		// Initial display
		UpdateDisplay(EntityState.Get());

		// Build layout
		ChildSlot
		[
			Border()
				.Background(FLinearColor(0.02f, 0.02f, 0.03f))
				.Padding(FMargin(16))
				.Content(
					VBox()
						// Header
						+ HBox()
							+ StatusIndicator
							+ Box().Width(8).Build()  // Spacer
							+ NameText.ToSharedRef()
						.Build()

						// Stats
						+ Box().Height(8).Build()  // Spacer
						+ HealthText.ToSharedRef()
						+ SpeedText.ToSharedRef()
						+ PositionText.ToSharedRef()
					.Build()
				)
				.Build()
		];
	}

	// Public API to update entity
	void SetEntity(const FEntityDisplayState& NewState)
	{
		EntityState.Update([&NewState](FEntityDisplayState& S) {
			S = NewState;
		});
	}

	void SetHealth(float Health)
	{
		EntityState.Update([Health](FEntityDisplayState& S) {
			S.Health = Health;
		});
	}

	void SetPosition(const FVector& Pos)
	{
		EntityState.Update([&Pos](FEntityDisplayState& S) {
			S.Position = Pos;
		});
	}

private:
	void UpdateDisplay(const FEntityDisplayState& State)
	{
		NameText->SetText(FText::FromString(State.Name));

		// Health with color based on value
		FLinearColor HealthColor = FLinearColor::LerpUsingHSV(
			FLinearColor::Red,
			FLinearColor::Green,
			State.Health / 100.0f
		);
		HealthText->SetText(FText::Format(
			NSLOCTEXT("Flight", "Health", "Health: {0}%"),
			FText::AsNumber(FMath::RoundToInt(State.Health))
		));
		HealthText->SetColorAndOpacity(FSlateColor(HealthColor));

		// Speed
		SpeedText->SetText(FText::Format(
			NSLOCTEXT("Flight", "Speed", "Speed: {0} m/s"),
			FText::AsNumber(FMath::RoundToInt(State.Speed))
		));

		// Position
		PositionText->SetText(FText::Format(
			NSLOCTEXT("Flight", "Position", "Position: ({0}, {1}, {2})"),
			FText::AsNumber(FMath::RoundToInt(State.Position.X)),
			FText::AsNumber(FMath::RoundToInt(State.Position.Y)),
			FText::AsNumber(FMath::RoundToInt(State.Position.Z))
		));

		// Status indicator
		FLinearColor StatusColor = State.bIsActive
			? FLinearColor(0.0f, 0.8f, 0.2f)
			: FLinearColor(0.5f, 0.5f, 0.5f);
		StatusIndicator->SetBorderBackgroundColor(StatusColor);
	}

	TStateContainer<FEntityDisplayState> EntityState;
	TSharedPtr<STextBlock> NameText;
	TSharedPtr<STextBlock> HealthText;
	TSharedPtr<STextBlock> SpeedText;
	TSharedPtr<STextBlock> PositionText;
	TSharedPtr<SBorder> StatusIndicator;
};


// ============================================================================
// Example 3: Platform-Aware Settings Panel
// ============================================================================

class SPlatformSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlatformSettingsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		// Get platform info
		const FPlatformInfo& Platform = FFlightPlatformService::Get().GetPlatformInfo();

		// Build platform info display
		TSharedRef<SVerticalBox> PlatformInfoBox = SNew(SVerticalBox);

		auto AddInfoRow = [&PlatformInfoBox](const FString& Label, const FString& Value) {
			PlatformInfoBox->AddSlot()
				.AutoHeight()
				.Padding(FMargin(0, 2))
				[
					HBox()
						+ Text(Label + TEXT(":")).FontSize(10).Color(FLinearColor::Gray).Build()
						+ Box().Width(8).Build()
						+ Text(Value).FontSize(10).Build()
					.Build()
				];
		};

		AddInfoRow(TEXT("Display Server"),
			Platform.IsWayland() ? TEXT("Wayland") :
			Platform.IsX11() ? TEXT("X11") : TEXT("Unknown"));

		AddInfoRow(TEXT("Compositor"), Platform.CompositorName);
		AddInfoRow(TEXT("Session"), Platform.SessionType);
		AddInfoRow(TEXT("DPI Scale"), FString::Printf(TEXT("%.2f"), Platform.DPIScale));
		AddInfoRow(TEXT("Fractional Scaling"),
			Platform.bFractionalScaling ? TEXT("Yes") : TEXT("No"));

#if PLATFORM_LINUX
		AddInfoRow(TEXT("SDL Driver"), SDL3Utils::GetVideoDriver());
#endif

		// Wayland-specific options
		TSharedRef<SVerticalBox> WaylandOptionsBox = SNew(SVerticalBox);

		if (Platform.IsWayland())
		{
			WaylandOptionsBox->AddSlot()
				.AutoHeight()
				.Padding(FMargin(0, 4))
				[
					Text(TEXT("Wayland Options")).FontSize(12).Color(FLinearColor(0.8f, 0.8f, 1.0f)).Build()
				];

			// Could add toggles for:
			// - Server-side decorations
			// - Idle inhibit
			// - etc.
		}

		ChildSlot
		[
			Border()
				.Background(FLinearColor(0.03f, 0.03f, 0.04f))
				.Padding(FMargin(16))
				.Content(
					VBox()
						+ Text(TEXT("Platform Information"))
							.FontSize(14)
							.Color(FLinearColor(0.9f, 0.9f, 1.0f))
							.Build()
						+ Box().Height(8).Build()
						+ PlatformInfoBox
						+ Box().Height(16).Build()
						+ WaylandOptionsBox
					.Build()
				)
				.Build()
		];
	}
};


// ============================================================================
// Example 4: Declarative Form Builder
// ============================================================================

// Form field types using Row types
using namespace Flight::RowTypes;

using FormTextField = TRow<
	TRowField<"Label", FText>,
	TRowField<"Value", FString>,
	TRowField<"Placeholder", FText>,
	TRowField<"OnChanged", TFunction<void(const FString&)>>
>;

using FormNumberField = TRow<
	TRowField<"Label", FText>,
	TRowField<"Value", float>,
	TRowField<"Min", float>,
	TRowField<"Max", float>,
	TRowField<"OnChanged", TFunction<void(float)>>
>;

using FormCheckboxField = TRow<
	TRowField<"Label", FText>,
	TRowField<"Value", bool>,
	TRowField<"OnChanged", TFunction<void(bool)>>
>;

// Form builder that uses Row types for field configuration
class FFormBuilder
{
public:
	FFormBuilder& AddText(const FormTextField& Field)
	{
		// Would create SEditableTextBox with the field configuration
		TextFields.Add(Field);
		return *this;
	}

	FFormBuilder& AddNumber(const FormNumberField& Field)
	{
		// Would create SSpinBox with the field configuration
		NumberFields.Add(Field);
		return *this;
	}

	FFormBuilder& AddCheckbox(const FormCheckboxField& Field)
	{
		// Would create SCheckBox with the field configuration
		CheckboxFields.Add(Field);
		return *this;
	}

	TSharedRef<SWidget> Build()
	{
		TSharedRef<SVerticalBox> FormBox = SNew(SVerticalBox);

		// Build text fields
		for (const auto& Field : TextFields)
		{
			FormBox->AddSlot()
				.AutoHeight()
				.Padding(FMargin(0, 4))
				[
					VBox()
						+ Text(Field.Get<"Label">()).FontSize(10).Build()
						+ SNew(SEditableTextBox)
							.Text(FText::FromString(Field.Get<"Value">()))
							.HintText(Field.Get<"Placeholder">())
					.Build()
				];
		}

		// Build number fields
		for (const auto& Field : NumberFields)
		{
			FormBox->AddSlot()
				.AutoHeight()
				.Padding(FMargin(0, 4))
				[
					VBox()
						+ Text(Field.Get<"Label">()).FontSize(10).Build()
						+ SNew(SSlider)
							.Value((Field.Get<"Value">() - Field.Get<"Min">()) /
								(Field.Get<"Max">() - Field.Get<"Min">()))
					.Build()
				];
		}

		// Build checkbox fields
		for (const auto& Field : CheckboxFields)
		{
			FormBox->AddSlot()
				.AutoHeight()
				.Padding(FMargin(0, 4))
				[
					HBox()
						+ SNew(SCheckBox)
							.IsChecked(Field.Get<"Value">() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
						+ Box().Width(8).Build()
						+ Text(Field.Get<"Label">()).FontSize(10).Build()
					.Build()
				];
		}

		return Border()
			.Background(FLinearColor(0.02f, 0.02f, 0.03f))
			.Padding(FMargin(16))
			.Content(FormBox)
			.Build();
	}

private:
	TArray<FormTextField> TextFields;
	TArray<FormNumberField> NumberFields;
	TArray<FormCheckboxField> CheckboxFields;
};


// ============================================================================
// Example 5: Composition with Pipe Operators
// ============================================================================

inline TSharedRef<SWidget> CreateStyledCard(
	const FText& Title,
	const FText& Description,
	TFunction<void()> OnAction)
{
	// Use pipe operators for composition
	return VBox()
		+ Text(Title).FontSize(16).Color(FLinearColor::White).Build()
		+ Text(Description).FontSize(11).Color(FLinearColor::Gray).AutoWrap(true).Build()
		+ Box().Height(12).Build()
		+ Button(TEXT("Action"))
			.OnClick(MoveTemp(OnAction))
			.Build()
	.Build()
		| WithPadding(FMargin(16))
		| WithBackground(FLinearColor(0.05f, 0.05f, 0.07f));
}


// ============================================================================
// Integration Example: Full Dashboard
// ============================================================================

class SFlightDashboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlightDashboard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		// Initialize platform
		InitializeFlightPlatform();

		// Create reactive state
		EntityCount = TReactiveValue<int32>(0);
		SimulationTime = TReactiveValue<float>(0.0f);
		IsSimulating = TReactiveValue<bool>(false);

		// Create stat displays
		TSharedRef<STextBlock> EntityCountText = SNew(STextBlock);
		TSharedRef<STextBlock> SimTimeText = SNew(STextBlock);

		Bindings::BindNumericText(EntityCountText, EntityCount, TEXT("Entities: {0}"));
		Bindings::BindNumericText(SimTimeText, SimulationTime, TEXT("Time: {0}s"));

		// Build dashboard
		ChildSlot
		[
			Border()
				.Background(FLinearColor(0.01f, 0.01f, 0.02f))
				.Content(
					VBox()
						// Header
						.AddFill(
							HBox()
								+ Text(TEXT("Flight Dashboard"))
									.FontSize(20)
									.Color(FLinearColor(0.8f, 0.85f, 1.0f))
									.Build()
								+ Box().Build()  // Flex spacer
								+ EntityCountText
								+ Box().Width(20).Build()
								+ SimTimeText
							.Build()
						)

						// Control buttons
						.Add(
							HBox()
								.Padding(FMargin(0, 16))
								+ Button(TEXT("Start"))
									.OnClick([this]() { IsSimulating = true; })
									.Build()
								+ Box().Width(8).Build()
								+ Button(TEXT("Stop"))
									.OnClick([this]() { IsSimulating = false; })
									.Build()
								+ Box().Width(8).Build()
								+ Button(TEXT("Reset"))
									.OnClick([this]() {
										EntityCount = 0;
										SimulationTime = 0.0f;
										IsSimulating = false;
									})
									.Build()
							.Build()
						)

						// Entity panel
						.AddFill(
							SNew(SEntityStatusPanel)
						)

						// Platform info (collapsed by default in production)
						.Add(
							SNew(SPlatformSettingsPanel)
						)
					.Build()
				)
				.Build()
		];
	}

	// Call from Tick to update simulation
	void UpdateSimulation(float DeltaTime)
	{
		if (IsSimulating.Get())
		{
			SimulationTime.Modify([DeltaTime](float& T) {
				T += DeltaTime;
			});
		}
	}

	void AddEntity()
	{
		EntityCount = EntityCount.Get() + 1;
	}

	void RemoveEntity()
	{
		if (EntityCount.Get() > 0)
		{
			EntityCount = EntityCount.Get() - 1;
		}
	}

private:
	TReactiveValue<int32> EntityCount;
	TReactiveValue<float> SimulationTime;
	TReactiveValue<bool> IsSimulating;
};


// ============================================================================
// Usage Guide
// ============================================================================
//
// 1. DECLARATIVE WIDGETS
//    Replace:
//      SNew(STextBlock).Text(TEXT("Hello"))
//    With:
//      Text(TEXT("Hello")).Build()
//
//    Replace:
//      SNew(SButton).OnClicked(...)
//    With:
//      Button(TEXT("Click")).OnClick([]() { ... }).Build()
//
//
// 2. REACTIVE BINDINGS
//    TReactiveValue<int32> Counter{0};
//    TSharedRef<STextBlock> Display = SNew(STextBlock);
//    Bindings::BindNumericText(Display, Counter, TEXT("Count: {0}"));
//
//    Counter = 42;  // Display automatically updates!
//
//
// 3. COMPOSITION
//    auto Widget = VBox()
//        + Text(TEXT("Header")).Build()
//        + Button(TEXT("Action")).OnClick(OnAction).Build()
//    .Build()
//        | WithPadding(FMargin(16))
//        | WithBackground(FLinearColor::Black);
//
//
// 4. PLATFORM DETECTION
//    const auto& Platform = FFlightPlatformService::Get().GetPlatformInfo();
//    if (Platform.IsWayland())
//    {
//        // Wayland-specific code
//    }
//

} // namespace Flight::UI::Examples
