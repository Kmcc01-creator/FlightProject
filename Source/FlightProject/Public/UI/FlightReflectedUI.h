// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Core/FlightReactive.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"

using namespace Flight::Reflection;
using namespace Flight::Reactive;

/**
 * FReflectedWidgetGenerator
 * 
 * Automatically generates a Slate UI panel for any reflected struct.
 * Uses reactive bindings to ensure the UI updates the simulation in real-time.
 */
class FLIGHTPROJECT_API SReflectedStatePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SReflectedStatePanel) {}
	SLATE_END_ARGS()

	/** Construct the UI for a specific reflected type */
	template<typename T>
	void Construct(const FArguments& InArgs, T& TargetState)
	{
		auto VerticalBox = SNew(SVerticalBox);

		// Iterate over fields using our C++23 reflection traits
		TReflectTraits<T>::Fields::ForEachDescriptor([&](auto Descriptor) {
			using FieldType = typename decltype(Descriptor)::FieldType;
			FString Name = ANSI_TO_TCHAR(decltype(Descriptor)::Name.data());
			
			// Only generate sliders for float fields for this prototype
			if constexpr (std::is_same_v<FieldType, float>)
			{
				VerticalBox->AddSlot()
				.AutoHeight()
				.Padding(5)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(0.3f)
					[
						SNew(STextBlock).Text(FText::FromString(Name))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.7f)
					[
						SNew(SSlider)
						.Value(Descriptor.Get(TargetState) / 2000.0f) // Normalized for slider
						.OnValueChanged_Lambda([&TargetState, Descriptor](float NewValue) {
							// Update the state reactively
							Descriptor.Get(TargetState) = NewValue * 2000.0f;
						})
					]
				];
			}
		});

		ChildSlot
		[
			SNew(SBorder)
			.Padding(10)
			[
				VerticalBox
			]
		];
	}
};
