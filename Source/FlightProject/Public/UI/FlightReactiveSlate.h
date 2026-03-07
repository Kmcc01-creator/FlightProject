// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightReactiveSlate - Slate Integration for Reactive Primitives
//
// Connects Flight::Reactive types to Slate widgets with automatic lifecycle management.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SSlider.h"
#include "Core/FlightReactive.h"

namespace Flight::ReactiveUI
{

using namespace Flight::Reactive;

// ============================================================================
// Managed Binding State - Refactored for C++23/Clang 20 compatibility
// ============================================================================
//
// Base class for all heap-allocated binding state to ensure proper deletion.

struct IManagedBinding
{
	virtual ~IManagedBinding() = default;
};

// ============================================================================
// Reactive Context - Manages subscription lifecycles
// ============================================================================

class FReactiveContext
{
public:
	FReactiveContext() = default;

	~FReactiveContext()
	{
		for (auto& Cleanup : Cleanups) { Cleanup(); }
		for (auto* Binding : ManagedBindings) { delete Binding; }
	}

	FReactiveContext(const FReactiveContext&) = delete;
	FReactiveContext& operator=(const FReactiveContext&) = delete;
	FReactiveContext(FReactiveContext&&) = default;
	FReactiveContext& operator=(FReactiveContext&&) = default;

	void AddCleanup(TFunction<void()> Cleanup) { Cleanups.Add(MoveTemp(Cleanup)); }
	
	void AddManagedBinding(IManagedBinding* Binding) { ManagedBindings.Add(Binding); }

private:
	TArray<TFunction<void()>> Cleanups;
	TArray<IManagedBinding*> ManagedBindings;
};


// ============================================================================
// Widget Bindings
// ============================================================================

namespace Bindings
{

/**
 * Text Binding Class (Refactored from struct)
 */
template<typename TValue>
class TTextBinding : public IManagedBinding
{
public:
	TTextBinding(TSharedRef<STextBlock> InWidget, TFunction<FText(const TValue&)> InFormatter)
		: Widget(InWidget), Formatter(MoveTemp(InFormatter)) {}

	TWeakPtr<STextBlock> Widget;
	TFunction<FText(const TValue&)> Formatter;

	static void OnChanged(const TValue& Old, const TValue& New, void* UserData)
	{
		auto* Self = static_cast<TTextBinding<TValue>*>(UserData);
		if (auto W = Self->Widget.Pin())
		{
			if (Self->Formatter) { W->SetText(Self->Formatter(New)); }
			else if constexpr (std::is_same_v<TValue, FText>) { W->SetText(New); }
			else if constexpr (std::is_same_v<TValue, FString>) { W->SetText(FText::FromString(New)); }
		}
	}
};

// Bind a reactive text value to an STextBlock
template<typename TValue>
void BindText(
	FReactiveContext& Context,
	TSharedRef<STextBlock> TextWidget,
	TReactiveValue<TValue>& Source,
	TFunction<FText(const TValue&)> Formatter = nullptr)
{
	// Set initial value
	if (Formatter) { TextWidget->SetText(Formatter(Source.Get())); }
	else if constexpr (std::is_same_v<TValue, FText>) { TextWidget->SetText(Source.Get()); }
	else if constexpr (std::is_same_v<TValue, FString>) { TextWidget->SetText(FText::FromString(Source.Get())); }

	// Create managed state with explicit constructor (Clang 20 safe)
	auto* State = new TTextBinding<TValue>(TextWidget, MoveTemp(Formatter));
	Context.AddManagedBinding(State);

	// Subscribe
	auto Id = Source.Subscribe(&TTextBinding<TValue>::OnChanged, State);
	Context.AddCleanup([&Source, Id]() { Source.Unsubscribe(Id); });
}

/**
 * Visibility Binding Class
 */
class FVisibilityBinding : public IManagedBinding
{
public:
	FVisibilityBinding(TSharedRef<SWidget> InWidget, EVisibility InVisible, EVisibility InHidden)
		: Widget(InWidget), VisibleState(InVisible), HiddenState(InHidden) {}

	TWeakPtr<SWidget> Widget;
	EVisibility VisibleState;
	EVisibility HiddenState;

	static void OnChanged(const bool& Old, const bool& New, void* UserData)
	{
		auto* Self = static_cast<FVisibilityBinding*>(UserData);
		if (auto W = Self->Widget.Pin())
		{
			W->SetVisibility(New ? Self->VisibleState : Self->HiddenState);
		}
	}
};

inline void BindVisibility(
	FReactiveContext& Context,
	TSharedRef<SWidget> Widget,
	TReactiveValue<bool>& Source,
	EVisibility VisibleState = EVisibility::Visible,
	EVisibility HiddenState = EVisibility::Collapsed)
{
	Widget->SetVisibility(Source.Get() ? VisibleState : HiddenState);

	auto* State = new FVisibilityBinding(Widget, VisibleState, HiddenState);
	Context.AddManagedBinding(State);

	auto Id = Source.Subscribe(&FVisibilityBinding::OnChanged, State);
	Context.AddCleanup([&Source, Id]() { Source.Unsubscribe(Id); });
}

/**
 * Numeric Text Binding
 */
template<typename TNumeric>
class TNumericTextBinding : public IManagedBinding
{
public:
	TNumericTextBinding(TSharedRef<STextBlock> InWidget, const FString& InFormat)
		: Widget(InWidget), Format(InFormat) {}

	TWeakPtr<STextBlock> Widget;
	FString Format;

	static void OnChanged(const TNumeric& Old, const TNumeric& New, void* UserData)
	{
		auto* Self = static_cast<TNumericTextBinding<TNumeric>*>(UserData);
		if (auto W = Self->Widget.Pin())
		{
			W->SetText(FText::Format(FTextFormat::FromString(Self->Format), FText::AsNumber(New)));
		}
	}
};

template<typename TNumeric>
	requires std::is_arithmetic_v<TNumeric>
void BindNumericText(
	FReactiveContext& Context,
	TSharedRef<STextBlock> TextWidget,
	TReactiveValue<TNumeric>& Source,
	const FString& Format = TEXT("{0}"))
{
	TextWidget->SetText(FText::Format(FTextFormat::FromString(Format), FText::AsNumber(Source.Get())));

	auto* State = new TNumericTextBinding<TNumeric>(TextWidget, Format);
	Context.AddManagedBinding(State);

	auto Id = Source.Subscribe(&TNumericTextBinding<TNumeric>::OnChanged, State);
	Context.AddCleanup([&Source, Id]() { Source.Unsubscribe(Id); });
}

/**
 * Enabled Binding
 */
class FEnabledBinding : public IManagedBinding
{
public:
	FEnabledBinding(TSharedRef<SWidget> InWidget) : Widget(InWidget) {}
	TWeakPtr<SWidget> Widget;

	static void OnChanged(const bool& Old, const bool& New, void* UserData)
	{
		auto* Self = static_cast<FEnabledBinding*>(UserData);
		if (auto W = Self->Widget.Pin()) { W->SetEnabled(New); }
	}
};

inline void BindEnabled(
	FReactiveContext& Context,
	TSharedRef<SWidget> Widget,
	TReactiveValue<bool>& Source)
{
	Widget->SetEnabled(Source.Get());
	auto* State = new FEnabledBinding(Widget);
	Context.AddManagedBinding(State);
	auto Id = Source.Subscribe(&FEnabledBinding::OnChanged, State);
	Context.AddCleanup([&Source, Id]() { Source.Unsubscribe(Id); });
}

/**
 * Color Binding
 */
class FColorBinding : public IManagedBinding
{
public:
	FColorBinding(TSharedRef<STextBlock> InWidget) : Widget(InWidget) {}
	TWeakPtr<STextBlock> Widget;

	static void OnChanged(const FSlateColor& Old, const FSlateColor& New, void* UserData)
	{
		auto* Self = static_cast<FColorBinding*>(UserData);
		if (auto W = Self->Widget.Pin()) { W->SetColorAndOpacity(New); }
	}
};

inline void BindColor(
	FReactiveContext& Context,
	TSharedRef<STextBlock> TextWidget,
	TReactiveValue<FSlateColor>& Source)
{
	TextWidget->SetColorAndOpacity(Source.Get());
	auto* State = new FColorBinding(TextWidget);
	Context.AddManagedBinding(State);
	auto Id = Source.Subscribe(&FColorBinding::OnChanged, State);
	Context.AddCleanup([&Source, Id]() { Source.Unsubscribe(Id); });
}

} // namespace Bindings


// ============================================================================
// Reactive Widget Base
// ============================================================================

class SReactiveWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SReactiveWidget) {}
	SLATE_END_ARGS()

	virtual ~SReactiveWidget() = default;

protected:
	FReactiveContext ReactiveContext;
};

} // namespace Flight::ReactiveUI
