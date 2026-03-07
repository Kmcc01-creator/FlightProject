// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightSlate - Declarative Widget System with Functional Patterns
//
// A more functional approach to Slate widget construction using:
//   - Row types for type-safe widget properties
//   - Declarative composition via operator overloading
//   - Reactive data binding via TObservableField
//   - Result types for fallible widget operations
//
// Design Goals:
//   - Reduce Slate boilerplate (SLATE_BEGIN_ARGS, etc.)
//   - Enable compile-time property checking
//   - Support reactive UI updates
//   - Integrate with FlightProject's functional utilities

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightRowTypes.h"
#include "Core/FlightReflection.h"
#include "UI/FlightReactiveSlate.h"

namespace Flight::Slate
{

using namespace Flight::Functional;
using namespace Flight::RowTypes;
using namespace Flight::Reflection;
using namespace Flight::ReactiveUI;

// ============================================================================
// Forward Declarations
// ============================================================================

template<typename TWidget, typename TProps>
class TFlightWidget;

template<typename... Children>
struct TWidgetTree;


// ============================================================================
// Widget Property Definitions (Row Types)
// ============================================================================

namespace Props
{

// Common property types
using PText = TRowField<"Text", FText>;
using PToolTip = TRowField<"ToolTip", FText>;
using PEnabled = TRowField<"Enabled", bool>;
using PVisibility = TRowField<"Visibility", EVisibility>;

// Layout properties
using PPadding = TRowField<"Padding", FMargin>;
using PHAlign = TRowField<"HAlign", EHorizontalAlignment>;
using PVAlign = TRowField<"VAlign", EVerticalAlignment>;
using PWidth = TRowField<"Width", FOptionalSize>;
using PHeight = TRowField<"Height", FOptionalSize>;
using PMaxWidth = TRowField<"MaxWidth", FOptionalSize>;
using PMaxHeight = TRowField<"MaxHeight", FOptionalSize>;

// Style properties
using PColorAndOpacity = TRowField<"ColorAndOpacity", FSlateColor>;
using PBackgroundColor = TRowField<"BackgroundColor", FLinearColor>;
using PBorderImage = TRowField<"BorderImage", const FSlateBrush*>;
using PFont = TRowField<"Font", FSlateFontInfo>;

// Event callbacks
using POnClicked = TRowField<"OnClicked", FOnClicked>;
using POnTextChanged = TRowField<"OnTextChanged", FOnTextChanged>;
using POnTextCommitted = TRowField<"OnTextCommitted", FOnTextCommitted>;

// ─────────────────────────────────────────────────────────────────
// Pre-composed Property Sets
// ─────────────────────────────────────────────────────────────────

// Base properties all widgets have
using BaseProps = TRow<PEnabled, PVisibility, PToolTip>;

// Layout container properties
using LayoutProps = TRow<PPadding, PHAlign, PVAlign>;

// Size constraint properties
using SizeProps = TRow<PWidth, PHeight, PMaxWidth, PMaxHeight>;

// Text display properties
using TextProps = TRow<PText, PFont, PColorAndOpacity>;

// Button properties
using ButtonProps = TextProps::Merge<TRow<POnClicked, PBackgroundColor>>;

// Text input properties
using InputProps = TextProps::Merge<TRow<POnTextChanged, POnTextCommitted>>;

} // namespace Props


// ============================================================================
// Widget Builder - Fluent API for widget construction
// ============================================================================

template<typename TSlateWidget>
class TWidgetBuilder
{
public:
	using Self = TWidgetBuilder<TSlateWidget>;
	using WidgetType = TSlateWidget;

	TWidgetBuilder() = default;

	// ─────────────────────────────────────────────────────────────
	// Common Properties
	// ─────────────────────────────────────────────────────────────

	Self& Enabled(bool bEnabled)
	{
		bIsEnabled = bEnabled;
		return *this;
	}

	Self& Visibility(EVisibility InVisibility)
	{
		WidgetVisibility = InVisibility;
		return *this;
	}

	Self& ToolTip(const FText& InToolTip)
	{
		ToolTipText = InToolTip;
		return *this;
	}

	// ─────────────────────────────────────────────────────────────
	// Layout Properties
	// ─────────────────────────────────────────────────────────────

	Self& Padding(FMargin InPadding)
	{
		WidgetPadding = InPadding;
		return *this;
	}

	Self& Padding(float Uniform)
	{
		WidgetPadding = FMargin(Uniform);
		return *this;
	}

	Self& Padding(float Horizontal, float Vertical)
	{
		WidgetPadding = FMargin(Horizontal, Vertical);
		return *this;
	}

	Self& HAlign(EHorizontalAlignment Align)
	{
		HorizontalAlignment = Align;
		return *this;
	}

	Self& VAlign(EVerticalAlignment Align)
	{
		VerticalAlignment = Align;
		return *this;
	}

	Self& Size(float Width, float Height)
	{
		DesiredWidth = Width;
		DesiredHeight = Height;
		return *this;
	}

	Self& Width(float InWidth)
	{
		DesiredWidth = InWidth;
		return *this;
	}

	Self& Height(float InHeight)
	{
		DesiredHeight = InHeight;
		return *this;
	}

	// ─────────────────────────────────────────────────────────────
	// Reactive Bindings
	// ─────────────────────────────────────────────────────────────

	Self& BindEnabled(FReactiveContext& Context, TReactiveValue<bool>& Source)
	{
		PostBuildActions.Add([&Context, &Source](TSharedRef<TSlateWidget> Widget) {
			Bindings::BindEnabled(Context, Widget, Source);
		});
		return *this;
	}

	Self& BindVisibility(FReactiveContext& Context, TReactiveValue<bool>& Source)
	{
		PostBuildActions.Add([&Context, &Source](TSharedRef<TSlateWidget> Widget) {
			Bindings::BindVisibility(Context, Widget, Source);
		});
		return *this;
	}

	// ─────────────────────────────────────────────────────────────
	// Build
	// ─────────────────────────────────────────────────────────────

	TSharedRef<TSlateWidget> Build() const
	{
		TSharedRef<TSlateWidget> Widget = BuildImpl();
		
		// Apply any post-build actions (like reactive bindings)
		for (auto& Action : PostBuildActions)
		{
			Action(Widget);
		}
		
		return Widget;
	}

	operator TSharedRef<TSlateWidget>() const
	{
		return Build();
	}

protected:
	virtual TSharedRef<TSlateWidget> BuildImpl() const = 0;

	// Actions to apply after widget is built
	mutable TArray<TFunction<void(TSharedRef<TSlateWidget>)>> PostBuildActions;

	// Common state
	bool bIsEnabled = true;
	EVisibility WidgetVisibility = EVisibility::Visible;
	FText ToolTipText;
	FMargin WidgetPadding;
	EHorizontalAlignment HorizontalAlignment = EHorizontalAlignment::HAlign_Fill;
	EVerticalAlignment VerticalAlignment = EVerticalAlignment::VAlign_Fill;
	TOptional<float> DesiredWidth;
	TOptional<float> DesiredHeight;
};


// ============================================================================
// Text Builder
// ============================================================================

class FTextBuilder : public TWidgetBuilder<STextBlock>
{
public:
	using Self = FTextBuilder;

	Self& Text(const FText& InText)
	{
		DisplayText = InText;
		return *this;
	}

	Self& Text(const FString& InText)
	{
		DisplayText = FText::FromString(InText);
		return *this;
	}

	Self& Font(const FSlateFontInfo& InFont)
	{
		TextFont = InFont;
		return *this;
	}

	Self& FontSize(int32 Size)
	{
		TextFont = FCoreStyle::GetDefaultFontStyle("Regular", Size);
		return *this;
	}

	Self& Color(const FSlateColor& InColor)
	{
		TextColor = InColor;
		return *this;
	}

	Self& Color(const FLinearColor& InColor)
	{
		TextColor = FSlateColor(InColor);
		return *this;
	}

	Self& Justification(ETextJustify::Type InJustification)
	{
		TextJustification = InJustification;
		return *this;
	}

	Self& AutoWrap(bool bAutoWrap)
	{
		bAutoWrapText = bAutoWrap;
		return *this;
	}

	template<typename TValue>
	Self& Bind(FReactiveContext& Context, TReactiveValue<TValue>& Source, TFunction<FText(const TValue&)> Formatter = nullptr)
	{
		PostBuildActions.Add([&Context, &Source, Formatter = MoveTemp(Formatter)](TSharedRef<STextBlock> Widget) mutable {
			Bindings::BindText(Context, Widget, Source, MoveTemp(Formatter));
		});
		return *this;
	}

	template<typename TNumeric>
		requires std::is_arithmetic_v<TNumeric>
	Self& BindNumeric(FReactiveContext& Context, TReactiveValue<TNumeric>& Source, const FString& Format = TEXT("{0}"))
	{
		PostBuildActions.Add([&Context, &Source, Format](TSharedRef<STextBlock> Widget) {
			Bindings::BindNumericText(Context, Widget, Source, Format);
		});
		return *this;
	}

protected:
	TSharedRef<STextBlock> BuildImpl() const override
	{
		return SNew(STextBlock)
			.Text(DisplayText)
			.Font(TextFont)
			.ColorAndOpacity(TextColor)
			.Justification(TextJustification)
			.AutoWrapText(bAutoWrapText)
			.Visibility(WidgetVisibility);
	}

private:
	FText DisplayText;
	FSlateFontInfo TextFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	FSlateColor TextColor = FSlateColor::UseForeground();
	ETextJustify::Type TextJustification = ETextJustify::Left;
	bool bAutoWrapText = false;
};


// ============================================================================
// Button Builder
// ============================================================================

class FButtonBuilder : public TWidgetBuilder<SButton>
{
public:
	using Self = FButtonBuilder;

	Self& Text(const FText& InText)
	{
		ButtonText = InText;
		return *this;
	}

	Self& Text(const FString& InText)
	{
		ButtonText = FText::FromString(InText);
		return *this;
	}

	Self& OnClicked(FOnClicked::TFuncType* Callback)
	{
		OnClickedDelegate = FOnClicked::CreateStatic(Callback);
		return *this;
	}

	Self& OnClicked(TFunction<FReply()> Callback)
	{
		OnClickedDelegate = FOnClicked::CreateLambda([Cb = MoveTemp(Callback)]() {
			return Cb();
		});
		return *this;
	}

	// Simplified callback that just runs code (returns FReply::Handled)
	Self& OnClick(TFunction<void()> Callback)
	{
		OnClickedDelegate = FOnClicked::CreateLambda([Cb = MoveTemp(Callback)]() {
			Cb();
			return FReply::Handled();
		});
		return *this;
	}

	Self& Style(const FButtonStyle* InStyle)
	{
		ButtonStyle = InStyle;
		return *this;
	}

	Self& Content(TSharedRef<SWidget> InContent)
	{
		CustomContent = InContent;
		return *this;
	}

protected:
	TSharedRef<SButton> BuildImpl() const override
	{
		TSharedRef<SButton> Button = SNew(SButton)
			.OnClicked(OnClickedDelegate)
			.IsEnabled(bIsEnabled)
			.Visibility(WidgetVisibility);

		if (ButtonStyle)
		{
			Button->SetButtonStyle(ButtonStyle);
		}

		if (CustomContent.IsValid())
		{
			Button->SetContent(CustomContent.ToSharedRef());
		}
		else if (!ButtonText.IsEmpty())
		{
			Button->SetContent(
				SNew(STextBlock)
				.Text(ButtonText)
				.Justification(ETextJustify::Center)
			);
		}

		return Button;
	}

private:
	FText ButtonText;
	FOnClicked OnClickedDelegate;
	const FButtonStyle* ButtonStyle = nullptr;
	TSharedPtr<SWidget> CustomContent;
};


// ============================================================================
// Box Builder (SBox wrapper)
// ============================================================================

class FBoxBuilder : public TWidgetBuilder<SBox>
{
public:
	using Self = FBoxBuilder;

	Self& Content(TSharedRef<SWidget> InContent)
	{
		BoxContent = InContent;
		return *this;
	}

	Self& WidthOverride(float InWidth)
	{
		DesiredWidth = InWidth;
		return *this;
	}

	Self& HeightOverride(float InHeight)
	{
		DesiredHeight = InHeight;
		return *this;
	}

	Self& MinWidth(float InMinWidth)
	{
		MinDesiredWidth = InMinWidth;
		return *this;
	}

	Self& MinHeight(float InMinHeight)
	{
		MinDesiredHeight = InMinHeight;
		return *this;
	}

	Self& MaxWidth(float InMaxWidth)
	{
		MaxDesiredWidth = InMaxWidth;
		return *this;
	}

	Self& MaxHeight(float InMaxHeight)
	{
		MaxDesiredHeight = InMaxHeight;
		return *this;
	}

protected:
	TSharedRef<SBox> BuildImpl() const override
	{
		TSharedRef<SBox> Box = SNew(SBox)
			.HAlign(HorizontalAlignment)
			.VAlign(VerticalAlignment)
			.Padding(WidgetPadding)
			.Visibility(WidgetVisibility);

		if (DesiredWidth.IsSet())
		{
			Box->SetWidthOverride(DesiredWidth.GetValue());
		}
		if (DesiredHeight.IsSet())
		{
			Box->SetHeightOverride(DesiredHeight.GetValue());
		}
		if (MinDesiredWidth.IsSet())
		{
			Box->SetMinDesiredWidth(MinDesiredWidth.GetValue());
		}
		if (MinDesiredHeight.IsSet())
		{
			Box->SetMinDesiredHeight(MinDesiredHeight.GetValue());
		}
		if (MaxDesiredWidth.IsSet())
		{
			Box->SetMaxDesiredWidth(MaxDesiredWidth.GetValue());
		}
		if (MaxDesiredHeight.IsSet())
		{
			Box->SetMaxDesiredHeight(MaxDesiredHeight.GetValue());
		}

		if (BoxContent.IsValid())
		{
			Box->SetContent(BoxContent.ToSharedRef());
		}

		return Box;
	}

private:
	TSharedPtr<SWidget> BoxContent;
	TOptional<float> MinDesiredWidth;
	TOptional<float> MinDesiredHeight;
	TOptional<float> MaxDesiredWidth;
	TOptional<float> MaxDesiredHeight;
};


// ============================================================================
// Border Builder
// ============================================================================

class FBorderBuilder : public TWidgetBuilder<SBorder>
{
public:
	using Self = FBorderBuilder;

	Self& Content(TSharedRef<SWidget> InContent)
	{
		BorderContent = InContent;
		return *this;
	}

	Self& Background(const FLinearColor& Color)
	{
		BackgroundColor = Color;
		return *this;
	}

	Self& BorderImage(const FSlateBrush* Brush)
	{
		BorderBrush = Brush;
		return *this;
	}

protected:
	TSharedRef<SBorder> BuildImpl() const override
	{
		auto Border = SNew(SBorder)
			.HAlign(HorizontalAlignment)
			.VAlign(VerticalAlignment)
			.Padding(WidgetPadding)
			.BorderBackgroundColor(BackgroundColor)
			.Visibility(WidgetVisibility);

		if (BorderBrush)
		{
			Border->SetBorderImage(BorderBrush);
		}

		if (BorderContent.IsValid())
		{
			Border->SetContent(BorderContent.ToSharedRef());
		}

		return Border;
	}

private:
	TSharedPtr<SWidget> BorderContent;
	FLinearColor BackgroundColor = FLinearColor::Transparent;
	const FSlateBrush* BorderBrush = nullptr;
};


// ============================================================================
// Horizontal/Vertical Box Builders
// ============================================================================

template<typename TBoxType>
class TBoxPanelBuilder : public TWidgetBuilder<TBoxType>
{
public:
	using Self = TBoxPanelBuilder<TBoxType>;
	using Super = TWidgetBuilder<TBoxType>;

	Self& operator+(TSharedRef<SWidget> Child)
	{
		Children.Add(FSlotData{Child, 0.0f, EHorizontalAlignment::HAlign_Fill, EVerticalAlignment::VAlign_Fill, FMargin()});
		return *this;
	}

	// Add with slot configuration
	Self& Add(TSharedRef<SWidget> Child, float FillSize = 0.0f)
	{
		Children.Add(FSlotData{Child, FillSize, EHorizontalAlignment::HAlign_Fill, EVerticalAlignment::VAlign_Fill, FMargin()});
		return *this;
	}

	Self& AddFill(TSharedRef<SWidget> Child)
	{
		Children.Add(FSlotData{Child, 1.0f, EHorizontalAlignment::HAlign_Fill, EVerticalAlignment::VAlign_Fill, FMargin()});
		return *this;
	}

	Self& AddAuto(TSharedRef<SWidget> Child)
	{
		Children.Add(FSlotData{Child, 0.0f, EHorizontalAlignment::HAlign_Fill, EVerticalAlignment::VAlign_Fill, FMargin()});
		return *this;
	}

	protected:
	TSharedRef<TBoxType> BuildImpl() const override
	{
		TSharedRef<TBoxType> Box = SNew(TBoxType);

		for (const FSlotData& Slot : Children)
		{
			auto NewSlot = Box->AddSlot();

			if constexpr (std::is_same_v<TBoxType, SHorizontalBox>)
			{
				if (Slot.FillSize > 0.0f)
				{
					NewSlot.FillWidth(Slot.FillSize);
				}
				else
				{
					NewSlot.AutoWidth();
				}
			}
			else
			{
				if (Slot.FillSize > 0.0f)
				{
					NewSlot.FillHeight(Slot.FillSize);
				}
				else
				{
					NewSlot.AutoHeight();
				}
			}

			NewSlot
				.HAlign(Slot.HAlign)
				.VAlign(Slot.VAlign)
				.Padding(Slot.Padding)
				[Slot.Widget];
		}

		return Box;
	}

private:
	struct FSlotData
	{
		TSharedRef<SWidget> Widget;
		float FillSize;
		EHorizontalAlignment HAlign;
		EVerticalAlignment VAlign;
		FMargin Padding;
	};

	TArray<FSlotData> Children;
};

using FHBoxBuilder = TBoxPanelBuilder<SHorizontalBox>;
using FVBoxBuilder = TBoxPanelBuilder<SVerticalBox>;


// ============================================================================
// Factory Functions (Entry Points)
// ============================================================================

inline FTextBuilder Text() { return FTextBuilder(); }
inline FTextBuilder Text(const FText& InText) { return FTextBuilder().Text(InText); }
inline FTextBuilder Text(const FString& InText) { return FTextBuilder().Text(InText); }
inline FTextBuilder Text(const TCHAR* InText) { return FTextBuilder().Text(FText::FromString(InText)); }

inline FButtonBuilder Button() { return FButtonBuilder(); }
inline FButtonBuilder Button(const FText& Text) { return FButtonBuilder().Text(Text); }
inline FButtonBuilder Button(const FString& Text) { return FButtonBuilder().Text(Text); }

inline FBoxBuilder Box() { return FBoxBuilder(); }
inline FBorderBuilder Border() { return FBorderBuilder(); }

inline FHBoxBuilder HBox() { return FHBoxBuilder(); }
inline FVBoxBuilder VBox() { return FVBoxBuilder(); }


// ============================================================================
// Composition Operators
// ============================================================================

// Pipe operator for widget transformation
template<typename TWidget, typename TTransform>
auto operator|(TSharedRef<TWidget> Widget, TTransform&& Transform)
	-> decltype(Transform(Widget))
{
	return Transform(Forward<TTransform>(Widget));
}

// Create a Box wrapping content with padding
inline auto WithPadding(FMargin Padding)
{
	return [Padding](TSharedRef<SWidget> Content) {
		return Box().Content(Content).Padding(Padding).Build();
	};
}

// Create a Border wrapping content with background
inline auto WithBackground(FLinearColor Color)
{
	return [Color](TSharedRef<SWidget> Content) {
		return Border().Background(Color).Content(Content).Build();
	};
}

// Center content
inline auto Centered()
{
	return [](TSharedRef<SWidget> Content) {
		return Box()
			.Content(Content)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Build();
	};
}


// ============================================================================
// Result-Based Widget Operations
// ============================================================================

// Widget operation result type
template<typename T>
using TWidgetResult = TResult<T, FString>;

// Try to find a child widget by type
template<typename TWidgetType>
TWidgetResult<TSharedPtr<TWidgetType>> FindChildOfType(TSharedRef<SWidget> Root)
{
	// BFS through widget tree
	TArray<TSharedRef<SWidget>> Queue;
	Queue.Add(Root);

	while (Queue.Num() > 0)
	{
		TSharedRef<SWidget> Current = Queue[0];
		Queue.RemoveAt(0);

		if (TSharedPtr<TWidgetType> Typed = StaticCastSharedRef<TWidgetType>(Current))
		{
			return TWidgetResult<TSharedPtr<TWidgetType>>::Ok(Typed);
		}

		FChildren* Children = Current->GetChildren();
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			Queue.Add(Children->GetChildAt(i));
		}
	}

	return TWidgetResult<TSharedPtr<TWidgetType>>::Err(
		FString::Printf(TEXT("Widget of type %s not found"), *TWidgetType::StaticWidgetClass().GetWidgetType().ToString())
	);
}


// ============================================================================
// Widget Traits (for reflection integration)
// ============================================================================

template<typename TWidget>
struct TWidgetTraits
{
	static constexpr bool IsFlightWidget = false;
};

// Concept for Flight widgets
template<typename T>
concept CFlightWidget = TWidgetTraits<T>::IsFlightWidget;


// ============================================================================
// Example Usage
// ============================================================================
//
// // Simple button with click handler
// auto MyButton = Button(TEXT("Click Me"))
//     .OnClick([]() { UE_LOG(LogTemp, Log, TEXT("Clicked!")); })
//     .Padding(8.0f)
//     .Build();
//
// // Vertical layout
// auto Layout = VBox()
//     + Text(TEXT("Header")).FontSize(24).Build()
//     + Text(TEXT("Description")).Color(FLinearColor::Gray).Build()
//     + HBox()
//         + Button(TEXT("OK")).OnClick(OnOK).Build()
//         + Button(TEXT("Cancel")).OnClick(OnCancel).Build()
//     .Build();
//
// // With composition operators
// auto Styled = Text(TEXT("Hello"))
//     | WithPadding(FMargin(16))
//     | WithBackground(FLinearColor(0.1f, 0.1f, 0.1f))
//     | Centered();
//

} // namespace Flight::Slate
