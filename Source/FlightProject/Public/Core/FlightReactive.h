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
	~TReactiveValue() { UnsubscribeAll(); }

	// Copy keeps the value but intentionally does not copy active subscriptions.
	// Subscriptions are tied to callback/user-data lifetime at runtime.
	TReactiveValue(const TReactiveValue& Other)
		: Value(Other.Value)
	{
	}

	TReactiveValue& operator=(const TReactiveValue& Other)
	{
		if (this != &Other)
		{
			UnsubscribeAll();
			Value = Other.Value;
		}
		return *this;
	}

	TReactiveValue(TReactiveValue&& Other) noexcept
		: Value(MoveTemp(Other.Value))
		, Subscriptions(MoveTemp(Other.Subscriptions))
		, NextSubscriptionId(Other.NextSubscriptionId)
	{
		Other.Subscriptions.Empty();
		Other.PendingUnsubscribeIds.Empty();
		Other.bIsNotifying = false;
		Other.bClearAllPending = false;
		Other.NextSubscriptionId = 1;
	}

	TReactiveValue& operator=(TReactiveValue&& Other) noexcept
	{
		if (this != &Other)
		{
			UnsubscribeAll();
			Value = MoveTemp(Other.Value);
			Subscriptions = MoveTemp(Other.Subscriptions);
			NextSubscriptionId = Other.NextSubscriptionId;
			Other.Subscriptions.Empty();
			Other.PendingUnsubscribeIds.Empty();
			Other.bIsNotifying = false;
			Other.bClearAllPending = false;
			Other.NextSubscriptionId = 1;
		}
		return *this;
	}

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
		Subscriptions.Add({Id, Callback, UserData, nullptr});
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
			CapturedCallback,
			[](void* UserData) {
				delete static_cast<TFunction<void(const T&, const T&)>*>(UserData);
			}
		);
	}

	void Unsubscribe(SubscriptionId Id)
	{
		if (bIsNotifying)
		{
			PendingUnsubscribeIds.AddUnique(Id);
			return;
		}

		RemoveSubscription(Id);
	}

	void UnsubscribeAll()
	{
		if (bIsNotifying)
		{
			bClearAllPending = true;
			return;
		}

		CleanupAllSubscriptions();
	}

private:
	struct FSubscription
	{
		SubscriptionId Id;
		ChangeCallback Callback;
		void* UserData;
		void(*UserDataCleanup)(void*) = nullptr;
	};

	SubscriptionId Subscribe(ChangeCallback Callback, void* UserData, void(*UserDataCleanup)(void*))
	{
		SubscriptionId Id = NextSubscriptionId++;
		Subscriptions.Add({Id, Callback, UserData, UserDataCleanup});
		return Id;
	}

	void CleanupSubscription(FSubscription& Sub)
	{
		if (Sub.UserDataCleanup && Sub.UserData)
		{
			Sub.UserDataCleanup(Sub.UserData);
			Sub.UserData = nullptr;
		}
	}

	void RemoveSubscription(SubscriptionId Id)
	{
		for (int32 Index = Subscriptions.Num() - 1; Index >= 0; --Index)
		{
			if (Subscriptions[Index].Id == Id)
			{
				CleanupSubscription(Subscriptions[Index]);
				Subscriptions.RemoveAtSwap(Index);
			}
		}
	}

	void CleanupAllSubscriptions()
	{
		for (FSubscription& Sub : Subscriptions)
		{
			CleanupSubscription(Sub);
		}
		Subscriptions.Empty();
	}

	void FlushPendingUnsubscribes()
	{
		if (bClearAllPending)
		{
			CleanupAllSubscriptions();
			PendingUnsubscribeIds.Empty();
			bClearAllPending = false;
			return;
		}

		for (SubscriptionId Id : PendingUnsubscribeIds)
		{
			RemoveSubscription(Id);
		}
		PendingUnsubscribeIds.Empty();
	}

	void NotifySubscribers(const T& OldValue, const T& NewValue)
	{
		bIsNotifying = true;

		const int32 InitialCount = Subscriptions.Num();
		for (int32 Index = 0; Index < InitialCount; ++Index)
		{
			const FSubscription& Sub = Subscriptions[Index];
			if (Sub.Callback)
			{
				Sub.Callback(OldValue, NewValue, Sub.UserData);
			}
		}

		bIsNotifying = false;
		FlushPendingUnsubscribes();
	}

	T Value{};
	TArray<FSubscription> Subscriptions;
	bool bIsNotifying = false;
	bool bClearAllPending = false;
	TArray<SubscriptionId> PendingUnsubscribeIds;
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

	~TEffect()
	{
		Dispose();
		if (CleanupCallback) { CleanupCallback(); }
	}

	TEffect(const TEffect&) = delete;
	TEffect& operator=(const TEffect&) = delete;
	TEffect(TEffect&&) = delete;
	TEffect& operator=(TEffect&&) = delete;

	void SetCleanup(CleanupFn Cleanup) { CleanupCallback = MoveTemp(Cleanup); }

private:
	void Dispose()
	{
		if (bDisposed)
		{
			return;
		}
		bDisposed = true;

		for (auto& Unsubscribe : UnsubscribeCallbacks)
		{
			Unsubscribe();
		}
		UnsubscribeCallbacks.Empty();
	}

	template<typename T>
	void SubscribeTo(TReactiveValue<T>& Dep)
	{
		const typename TReactiveValue<T>::SubscriptionId Id = Dep.Subscribe(
			[](const T&, const T&, void* UserData) {
				static_cast<TEffect*>(UserData)->RunEffect();
			},
			this
		);

		UnsubscribeCallbacks.Add([&Dep, Id]() {
			Dep.Unsubscribe(Id);
		});
	}

	void RunEffect()
	{
		if (CleanupCallback) { CleanupCallback(); }
		EffectCallback();
	}

	EffectFn EffectCallback;
	CleanupFn CleanupCallback;
	TArray<TFunction<void()>> UnsubscribeCallbacks;
	bool bDisposed = false;
};

} // namespace Flight::Reactive
