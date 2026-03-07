// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightReactive - Core Reactive Primitives
//
// generic reactive system for managing data dependencies and side effects.
// Decoupled from UI to allow use in ECS, Gameplay, and Async systems.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Core/FlightFunctional.h"

namespace Flight::Reactive
{

using namespace Flight::Reflection;
using namespace Flight::Functional;

// ============================================================================
// Forward Declarations
// ============================================================================

template<typename T>
class TReactiveValue;

template<typename T>
class TComputedValue;


// ============================================================================
// Reactive Value - Multi-subscriber observable
// ============================================================================

template<typename T>
class TReactiveValue
{
public:
	using ValueType = T;
	using ChangeCallback = void(*)(const T& /*Old*/, const T& /*New*/, void* /*UserData*/);

	TReactiveValue() = default;
	explicit TReactiveValue(T InValue) : Value(MoveTemp(InValue)) {}

	const T& Get() const { return Value; }
	T& GetMutable() { return Value; }
	operator const T&() const { return Value; }

	void Set(T NewValue)
	{
		if (!(Value == NewValue))
		{
			T OldValue = MoveTemp(Value);
			Value = MoveTemp(NewValue);
			NotifySubscribers(OldValue, Value);
		}
	}

	TReactiveValue& operator=(T NewValue)
	{
		Set(MoveTemp(NewValue));
		return *this;
	}

	template<typename F>
	void Modify(F&& Modifier)
	{
		T OldValue = Value;
		Modifier(Value);
		if (!(OldValue == Value))
		{
			NotifySubscribers(OldValue, Value);
		}
	}

	using SubscriptionId = uint32;

	SubscriptionId Subscribe(ChangeCallback Callback, void* UserData = nullptr)
	{
		SubscriptionId Id = NextSubscriptionId++;
		Subscriptions.Add({Id, Callback, UserData});
		return Id;
	}

	template<typename F>
	SubscriptionId SubscribeLambda(F&& Callback)
	{
		auto* CapturedCallback = new TFunction<void(const T&, const T&)>(Forward<F>(Callback));
		return Subscribe(
			[](const T& Old, const T& New, void* UserData) {
				auto* Fn = static_cast<TFunction<void(const T&, const T&)>*>(UserData);
				(*Fn)(Old, New);
			},
			CapturedCallback
		);
	}

	void Unsubscribe(SubscriptionId Id)
	{
		Subscriptions.RemoveAll([Id](const FSubscription& Sub) {
			return Sub.Id == Id;
		});
	}

	void UnsubscribeAll() { Subscriptions.Empty(); }

private:
	void NotifySubscribers(const T& OldValue, const T& NewValue)
	{
		for (const FSubscription& Sub : Subscriptions)
		{
			if (Sub.Callback)
			{
				Sub.Callback(OldValue, NewValue, Sub.UserData);
			}
		}
	}

	struct FSubscription
	{
		SubscriptionId Id;
		ChangeCallback Callback;
		void* UserData;
	};

	T Value{};
	TArray<FSubscription> Subscriptions;
	SubscriptionId NextSubscriptionId = 1;
};


// ============================================================================
// State Container - Grouped reflectable state
// ============================================================================

template<typename TState>
class TStateContainer
{
	static_assert(CReflectable<TState>, "State must be a reflectable type");

public:
	TStateContainer() = default;
	explicit TStateContainer(TState InitialState) : State(MoveTemp(InitialState)) {}

	const TState& Get() const { return State; }

	template<typename F>
	void Update(F&& Modifier)
	{
		TState OldState = State;
		Modifier(State);

		if (!(OldState == State))
		{
			for (auto& [Id, Callback] : Subscribers)
			{
				Callback(OldState, State);
			}
		}
	}

	using SubscriptionId = uint32;
	using StateCallback = TFunction<void(const TState&, const TState&)>;

	SubscriptionId Subscribe(StateCallback Callback)
	{
		SubscriptionId Id = NextId++;
		Subscribers.Add(Id, MoveTemp(Callback));
		return Id;
	}

	void Unsubscribe(SubscriptionId Id) { Subscribers.Remove(Id); }

private:
	TState State{};
	TMap<SubscriptionId, StateCallback> Subscribers;
	SubscriptionId NextId = 1;
};


// ============================================================================
// Effect - Side effects on dependency changes
// ============================================================================

template<typename... TDeps>
class TEffect
{
public:
	using EffectFn = TFunction<void()>;
	using CleanupFn = TFunction<void()>;

	TEffect(EffectFn Effect, TReactiveValue<TDeps>&... Dependencies)
		: EffectCallback(MoveTemp(Effect))
	{
		(SubscribeTo(Dependencies), ...);
		RunEffect();
	}

	~TEffect() { if (CleanupCallback) { CleanupCallback(); } }

	void SetCleanup(CleanupFn Cleanup) { CleanupCallback = MoveTemp(Cleanup); }

private:
	template<typename T>
	void SubscribeTo(TReactiveValue<T>& Dep)
	{
		Dep.Subscribe(
			[](const T&, const T&, void* UserData) {
				static_cast<TEffect*>(UserData)->RunEffect();
			},
			this
		);
	}

	void RunEffect()
	{
		if (CleanupCallback) { CleanupCallback(); }
		EffectCallback();
	}

	EffectFn EffectCallback;
	CleanupFn CleanupCallback;
};

} // namespace Flight::Reactive
