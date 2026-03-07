// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Functional programming utilities with lambda combinators
//
// Exploring how far we can push C++ lambdas toward Rust-like ergonomics.
// Not trying to replicate Rust's macro system, but capturing some patterns.

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include <utility>
#include <optional>

namespace Flight::Functional
{

// ============================================================================
// Defer - Scoped cleanup (like Go's defer, Rust's Drop, or scopeguard)
// ============================================================================

template<typename F>
class TDefer
{
public:
	explicit TDefer(F&& Fn) : Func(MoveTemp(Fn)), bActive(true) {}
	~TDefer() { if (bActive) Func(); }

	// Move-only
	TDefer(TDefer&& Other) : Func(MoveTemp(Other.Func)), bActive(Other.bActive)
	{
		Other.bActive = false;
	}

	TDefer(const TDefer&) = delete;
	TDefer& operator=(const TDefer&) = delete;
	TDefer& operator=(TDefer&&) = delete;

	void Cancel() { bActive = false; }
	void Execute() { if (bActive) { Func(); bActive = false; } }

private:
	F Func;
	bool bActive;
};

template<typename F>
[[nodiscard]] TDefer<F> Defer(F&& Fn)
{
	return TDefer<F>(Forward<F>(Fn));
}

// Usage:
//   auto Guard = Defer([&]() { close(Fd); });
//   // ... code that might throw or early return ...
//   // Fd is closed automatically when Guard goes out of scope


// ============================================================================
// TResult - Rust-like Result<T, E> with monadic operations
// ============================================================================

template<typename T, typename E = FString>
class TResult
{
public:
	// Constructors
	static TResult Ok(T Value) { return TResult(MoveTemp(Value), true); }
	static TResult Err(E Error) { return TResult(MoveTemp(Error)); }

	bool IsOk() const { return bIsOk; }
	bool IsErr() const { return !bIsOk; }

	// Unwrap (crashes if Err - use sparingly, like Rust's unwrap)
	T& Unwrap() & { check(bIsOk); return Value; }
	const T& Unwrap() const& { check(bIsOk); return Value; }
	T Unwrap() && { check(bIsOk); return MoveTemp(Value); }

	// UnwrapOr - provide default on error
	T UnwrapOr(T Default) && { return bIsOk ? MoveTemp(Value) : MoveTemp(Default); }

	// UnwrapOrElse - compute default lazily
	template<typename F>
	T UnwrapOrElse(F&& Fn) &&
	{
		return bIsOk ? MoveTemp(Value) : Fn(MoveTemp(Error));
	}

	// Map - transform the Ok value
	template<typename F>
	auto Map(F&& Fn) && -> TResult<decltype(Fn(DeclVal<T>())), E>
	{
		using U = decltype(Fn(DeclVal<T>()));
		if (bIsOk)
		{
			return TResult<U, E>::Ok(Fn(MoveTemp(Value)));
		}
		return TResult<U, E>::Err(MoveTemp(Error));
	}

	// MapErr - transform the Err value
	template<typename F>
	auto MapErr(F&& Fn) && -> TResult<T, decltype(Fn(DeclVal<E>()))>
	{
		using E2 = decltype(Fn(DeclVal<E>()));
		if (bIsOk)
		{
			return TResult<T, E2>::Ok(MoveTemp(Value));
		}
		return TResult<T, E2>::Err(Fn(MoveTemp(Error)));
	}

	// AndThen - chain operations (flatMap / bind)
	template<typename F>
	auto AndThen(F&& Fn) && -> decltype(Fn(DeclVal<T>()))
	{
		if (bIsOk)
		{
			return Fn(MoveTemp(Value));
		}
		using ReturnType = decltype(Fn(DeclVal<T>()));
		return ReturnType::Err(MoveTemp(Error));
	}

	// OrElse - recover from error
	template<typename F>
	auto OrElse(F&& Fn) && -> TResult<T, E>
	{
		if (bIsOk)
		{
			return TResult::Ok(MoveTemp(Value));
		}
		return Fn(MoveTemp(Error));
	}

	// Match - pattern matching style
	template<typename OkFn, typename ErrFn>
	auto Match(OkFn&& OnOk, ErrFn&& OnErr) && -> decltype(OnOk(DeclVal<T>()))
	{
		if (bIsOk)
		{
			return OnOk(MoveTemp(Value));
		}
		return OnErr(MoveTemp(Error));
	}

	const E& GetError() const& { check(!bIsOk); return Error; }
	E GetError() && { check(!bIsOk); return MoveTemp(Error); }

private:
	TResult(T Val, bool) : Value(MoveTemp(Val)), bIsOk(true) {}
	TResult(E Err) : Error(MoveTemp(Err)), bIsOk(false) {}

	T Value{};
	E Error{};
	bool bIsOk;
};

// Helper to create Ok/Err
template<typename T>
auto Ok(T Value) { return TResult<T>::Ok(MoveTemp(Value)); }

template<typename T = void*, typename E = FString>
auto Err(E Error) { return TResult<T, E>::Err(MoveTemp(Error)); }


// ============================================================================
// Heterogeneous Validation Chain - Type-accumulating lambda pipeline
// ============================================================================
//
// Inspired by Rust's ? operator, but with explicit chaining.
// Each step receives ALL previous results, enabling dependent validation.
//
// Usage:
//   auto [rhi, timeline, sem] = Validate<FString>()
//       .Then([]() { return GetVulkanRHI(); })
//       .Then([](VulkanRHI* r) { return GetTimeline(r); })
//       .Then([](VulkanRHI* r, Timeline t) { return CreateSem(t); })
//       .Finish()
//       .Unwrap();
//
// Each lambda receives unpacked results of all previous steps.
// Short-circuits on first error. Returns TResult<tuple<T1,T2,...>, E>.

namespace Detail
{
	// Invoke a callable with tuple elements unpacked as arguments
	template<typename F, typename Tuple, size_t... Is>
	auto ApplyImpl(F&& Fn, Tuple&& Args, std::index_sequence<Is...>)
		-> decltype(Fn(std::get<Is>(Forward<Tuple>(Args))...))
	{
		return Fn(std::get<Is>(Forward<Tuple>(Args))...);
	}

	template<typename F, typename Tuple>
	auto Apply(F&& Fn, Tuple&& Args)
		-> decltype(ApplyImpl(Forward<F>(Fn), Forward<Tuple>(Args),
			std::make_index_sequence<std::tuple_size_v<std::decay_t<Tuple>>>{}))
	{
		return ApplyImpl(
			Forward<F>(Fn),
			Forward<Tuple>(Args),
			std::make_index_sequence<std::tuple_size_v<std::decay_t<Tuple>>>{}
		);
	}

	// Extract T from TResult<T, E>
	template<typename R> struct TResultValueType;
	template<typename T, typename E>
	struct TResultValueType<TResult<T, E>> { using Type = T; };

	template<typename R>
	using TResultValueT = typename TResultValueType<std::decay_t<R>>::Type;
}

// The chain builder - each Then() returns a new type with extended tuple
template<typename E, typename Tuple>
class TValidateChain
{
public:
	explicit TValidateChain(TResult<Tuple, E> Init) : Current(MoveTemp(Init)) {}

	// Move-only
	TValidateChain(TValidateChain&&) = default;
	TValidateChain& operator=(TValidateChain&&) = default;
	TValidateChain(const TValidateChain&) = delete;
	TValidateChain& operator=(const TValidateChain&) = delete;

	/**
	 * Add a validation step. Lambda receives all previous results unpacked.
	 * Must return TResult<T, E> where T is the new value to accumulate.
	 */
	template<typename F>
	auto Then(F&& Step) &&
	{
		// Deduce what Step returns when called with unpacked Tuple
		using StepResult = decltype(Detail::Apply(Forward<F>(Step), std::declval<Tuple>()));
		using NewT = Detail::TResultValueT<StepResult>;
		using NewTuple = decltype(std::tuple_cat(
			std::declval<Tuple>(),
			std::declval<std::tuple<NewT>>()
		));

		// If already failed, propagate error with new tuple type
		if (Current.IsErr())
		{
			return TValidateChain<E, NewTuple>(
				TResult<NewTuple, E>::Err(MoveTemp(Current).GetError())
			);
		}

		// Execute step with unpacked current tuple
		Tuple AccumulatedValues = MoveTemp(Current).Unwrap();
		auto Result = Detail::Apply(Forward<F>(Step), AccumulatedValues);

		// If step failed, return error
		if (Result.IsErr())
		{
			return TValidateChain<E, NewTuple>(
				TResult<NewTuple, E>::Err(MoveTemp(Result).GetError())
			);
		}

		// Append new result to tuple and continue
		NewTuple Extended = std::tuple_cat(
			MoveTemp(AccumulatedValues),
			std::make_tuple(MoveTemp(Result).Unwrap())
		);

		return TValidateChain<E, NewTuple>(
			TResult<NewTuple, E>::Ok(MoveTemp(Extended))
		);
	}

	// ========================================================================
	// Multi-Faceted Binding - Slate-inspired variants for different callable types
	// ========================================================================

	/**
	 * Then_Lambda - Explicit lambda form (same as Then())
	 * For when you want to be explicit about the callable type.
	 */
	template<typename F>
	auto Then_Lambda(F&& Step) &&
	{
		return MoveTemp(*this).Then(Forward<F>(Step));
	}

	/**
	 * Then_Static - For free functions or static member functions.
	 *
	 * Usage:
	 *   TResult<Foo, FString> GlobalValidator(int x) { ... }
	 *   .Then_Static(&GlobalValidator)
	 */
	template<typename Ret, typename... Args>
	auto Then_Static(Ret(*FuncPtr)(Args...)) &&
	{
		return MoveTemp(*this).Then([FuncPtr](Args... args) {
			return FuncPtr(Forward<Args>(args)...);
		});
	}

	/**
	 * Then_Method - For member functions on arbitrary objects.
	 *
	 * Usage:
	 *   .Then_Method(this, &MyClass::ValidateRhi)
	 */
	template<typename Obj, typename Ret, typename Class, typename... Args>
	auto Then_Method(Obj* Object, Ret(Class::*MethodPtr)(Args...)) &&
	{
		return MoveTemp(*this).Then([Object, MethodPtr](Args... args) {
			return (Object->*MethodPtr)(Forward<Args>(args)...);
		});
	}

	/**
	 * Then_Method - Const member function variant
	 */
	template<typename Obj, typename Ret, typename Class, typename... Args>
	auto Then_Method(const Obj* Object, Ret(Class::*MethodPtr)(Args...) const) &&
	{
		return MoveTemp(*this).Then([Object, MethodPtr](Args... args) {
			return (Object->*MethodPtr)(Forward<Args>(args)...);
		});
	}

	/**
	 * Then_WeakLambda - Like Then_Lambda but guards against stale UObject captures.
	 *
	 * Usage:
	 *   .Then_WeakLambda(this, [this](int x) { return ValidateSomething(x); })
	 *
	 * Returns Err if the UObject has been destroyed.
	 */
	template<typename UObj, typename F>
	auto Then_WeakLambda(UObj* Object, F&& Lambda) &&
	{
		static_assert(std::is_base_of_v<UObject, UObj>,
			"Then_WeakLambda requires a UObject-derived type");

		TWeakObjectPtr<UObj> WeakObj(Object);
		return MoveTemp(*this).Then([WeakObj, Lambda = Forward<F>(Lambda)](auto&&... Args)
			-> decltype(Lambda(Forward<decltype(Args)>(Args)...))
		{
			if (!WeakObj.IsValid())
			{
				using RetType = decltype(Lambda(Forward<decltype(Args)>(Args)...));
				return RetType::Err(FString(TEXT("UObject destroyed during validation")));
			}
			return Lambda(Forward<decltype(Args)>(Args)...);
		});
	}

	/**
	 * Finish the chain and get the final TResult<tuple<...>, E>
	 */
	TResult<Tuple, E> Finish() && { return MoveTemp(Current); }

	/**
	 * Check if the chain has failed (for early inspection)
	 */
	bool IsErr() const { return Current.IsErr(); }

private:
	TResult<Tuple, E> Current;
};

/**
 * Start a validation chain.
 *
 * @tparam E Error type (default: FString)
 * @return Empty chain ready for .Then() calls
 *
 * Example:
 *   auto Result = Validate<FString>()
 *       .Then([]() -> TResult<int, FString> { return Ok(42); })
 *       .Then([](int x) -> TResult<float, FString> { return Ok(x * 1.5f); })
 *       .Finish();
 *   // Result is TResult<std::tuple<int, float>, FString>
 */
template<typename E = FString>
TValidateChain<E, std::tuple<>> Validate()
{
	return TValidateChain<E, std::tuple<>>(
		TResult<std::tuple<>, E>::Ok(std::tuple<>{})
	);
}

/**
 * Start a validation chain with an initial value.
 *
 * @param Initial First value to seed the chain
 * @return Chain containing Initial, ready for .Then() calls
 */
template<typename T, typename E = FString>
TValidateChain<E, std::tuple<T>> ValidateWith(T Initial)
{
	return TValidateChain<E, std::tuple<T>>(
		TResult<std::tuple<T>, E>::Ok(std::make_tuple(MoveTemp(Initial)))
	);
}


// ============================================================================
// Lazy/Cached Validation - Deferred evaluation with memoization
// ============================================================================
//
// Inspired by Slate's TAttribute<T> which stores either a value OR a delegate.
// Useful for AST parsing and recursive structures where we defer evaluation
// until actual access, then cache the result.
//
// Usage:
//   // Create deferred computation
//   auto LazyRhi = TLazy<IVulkanDynamicRHI*, FString>([]() {
//       return GetIVulkanDynamicRHI()
//           ? Ok(GetIVulkanDynamicRHI())
//           : Err<IVulkanDynamicRHI*>(TEXT("No RHI available"));
//   });
//
//   // Later, when we need the value (evaluates and caches):
//   auto Result = LazyRhi.Get();  // Returns TResult<T, E>
//   auto Value = LazyRhi.Unwrap(); // Returns T, crashes on error
//
//   // Subsequent calls return cached value (no re-evaluation)
//   auto Cached = LazyRhi.Get();  // Same result, instant

/**
 * TLazy<T, E> - A lazily-evaluated, cached result.
 *
 * Stores either:
 * - An immediate value (already computed)
 * - A thunk (deferred computation that returns TResult<T, E>)
 *
 * First access evaluates the thunk and caches the result.
 * Thread-safety: NOT thread-safe. Use external synchronization if needed.
 */
template<typename T, typename E = FString>
class TLazy
{
public:
	using ThunkType = TFunction<TResult<T, E>()>;

	/**
	 * Create from immediate value (already computed)
	 */
	static TLazy Immediate(T Value)
	{
		TLazy Result;
		Result.Cached_ = TResult<T, E>::Ok(MoveTemp(Value));
		return Result;
	}

	/**
	 * Create from immediate error
	 */
	static TLazy Failed(E Error)
	{
		TLazy Result;
		Result.Cached_ = TResult<T, E>::Err(MoveTemp(Error));
		return Result;
	}

	/**
	 * Create from deferred computation
	 */
	explicit TLazy(ThunkType Thunk)
		: Thunk_(MoveTemp(Thunk))
	{
	}

	// Default construction (invalid state, must assign before use)
	TLazy() = default;

	// Move-only
	TLazy(TLazy&&) = default;
	TLazy& operator=(TLazy&&) = default;
	TLazy(const TLazy&) = delete;
	TLazy& operator=(const TLazy&) = delete;

	/**
	 * Get the value, evaluating thunk if needed.
	 * Result is cached for subsequent calls.
	 *
	 * @return Reference to cached TResult<T, E>
	 */
	const TResult<T, E>& Get() const
	{
		EnsureEvaluated();
		return *Cached_;
	}

	/**
	 * Force evaluation and return by value (moves on first call)
	 */
	TResult<T, E> Take()
	{
		EnsureEvaluated();
		Thunk_ = nullptr;  // Clear thunk, we've consumed it
		return MoveTemp(*Cached_);
	}

	/**
	 * Unwrap - get the value directly, crash on error
	 */
	const T& Unwrap() const
	{
		return Get().Unwrap();
	}

	/**
	 * Check if already evaluated (cached)
	 */
	bool IsEvaluated() const { return Cached_.IsSet(); }

	/**
	 * Check if evaluation succeeded (must be evaluated first)
	 */
	bool IsOk() const { return IsEvaluated() && Cached_->IsOk(); }

	/**
	 * Check if evaluation failed (must be evaluated first)
	 */
	bool IsErr() const { return IsEvaluated() && Cached_->IsErr(); }

	/**
	 * Force evaluation without accessing result
	 */
	void Evaluate() const { EnsureEvaluated(); }

	/**
	 * Reset to unevaluated state with new thunk
	 */
	void Reset(ThunkType NewThunk)
	{
		Cached_.Reset();
		Thunk_ = MoveTemp(NewThunk);
	}

	/**
	 * Force evaluation and release the thunk closure.
	 *
	 * After Collapse(), the cached value remains but the closure
	 * (and any captured references) is freed. Use this to avoid
	 * space leaks when you know you won't need to re-evaluate.
	 */
	void Collapse()
	{
		EnsureEvaluated();
		Thunk_ = nullptr;  // Release closure, keep cached value
	}

	/**
	 * Map - transform the value lazily
	 *
	 * Returns a new TLazy that, when evaluated, will apply Fn to this value.
	 */
	template<typename F>
	auto Map(F&& Fn) const -> TLazy<decltype(Fn(std::declval<T>())), E>
	{
		using U = decltype(Fn(std::declval<T>()));

		// Capture this by pointer (lazy reference)
		return TLazy<U, E>([this, Fn = Forward<F>(Fn)]() -> TResult<U, E> {
			const auto& Result = this->Get();
			if (Result.IsErr())
			{
				return TResult<U, E>::Err(Result.GetError());
			}
			return TResult<U, E>::Ok(Fn(Result.Unwrap()));
		});
	}

	/**
	 * AndThen - chain lazy computations
	 *
	 * Returns a new TLazy that, when evaluated, chains this result into Fn.
	 */
	template<typename F>
	auto AndThen(F&& Fn) const -> decltype(Fn(std::declval<T>()))
	{
		using ResultType = decltype(Fn(std::declval<T>()));
		using U = Detail::TResultValueT<typename ResultType::ThunkType::ReturnType>;

		return ResultType([this, Fn = Forward<F>(Fn)]() {
			const auto& Result = this->Get();
			if (Result.IsErr())
			{
				return typename ResultType::ThunkType::ReturnType::Err(Result.GetError());
			}
			return Fn(Result.Unwrap()).Get();
		});
	}

private:
	void EnsureEvaluated() const
	{
		if (!Cached_.IsSet() && Thunk_)
		{
			Cached_ = Thunk_();
		}
		checkf(Cached_.IsSet(), TEXT("TLazy accessed without thunk or cached value"));
	}

	mutable TOptional<TResult<T, E>> Cached_;
	ThunkType Thunk_;
};

/**
 * Convenience: Create lazy from immediate value
 */
template<typename T>
TLazy<T, FString> Lazy(T Value)
{
	return TLazy<T, FString>::Immediate(MoveTemp(Value));
}

/**
 * Convenience: Create lazy from thunk
 */
template<typename T, typename E = FString>
TLazy<T, E> LazyEval(TFunction<TResult<T, E>()> Thunk)
{
	return TLazy<T, E>(MoveTemp(Thunk));
}


// ============================================================================
// TLazyChain - Chainable lazy validation with deferred evaluation
// ============================================================================
//
// Combines TValidateChain's heterogeneous accumulation with TLazy's
// deferred evaluation. Useful when validation steps are expensive and
// you may not need all results.
//
// Usage:
//   auto LazyValidation = LazyValidate<FString>()
//       .Then([]() { return ComputeExpensiveThing(); })    // Not called yet
//       .Then([](auto x) { return ValidateExpensive(x); }) // Not called yet
//       .Build();
//
//   // Later, when we actually need the results:
//   auto Result = LazyValidation.Get();  // NOW evaluation happens

template<typename E, typename Tuple>
class TLazyChain
{
public:
	using ThunkType = TFunction<TResult<Tuple, E>()>;

	explicit TLazyChain(ThunkType Thunk) : Thunk_(MoveTemp(Thunk)) {}

	// Move-only
	TLazyChain(TLazyChain&&) = default;
	TLazyChain& operator=(TLazyChain&&) = default;
	TLazyChain(const TLazyChain&) = delete;
	TLazyChain& operator=(const TLazyChain&) = delete;

	/**
	 * Add a lazy validation step.
	 * Returns a new chain with extended type, but nothing is evaluated yet.
	 */
	template<typename F>
	auto Then(F&& Step) &&
	{
		// Deduce types
		using StepResult = decltype(Detail::Apply(Forward<F>(Step), std::declval<Tuple>()));
		using NewT = Detail::TResultValueT<StepResult>;
		using NewTuple = decltype(std::tuple_cat(
			std::declval<Tuple>(),
			std::declval<std::tuple<NewT>>()
		));

		// Capture current thunk and step, return new lazy thunk
		auto CapturedThunk = MoveTemp(Thunk_);
		return TLazyChain<E, NewTuple>([CapturedThunk = MoveTemp(CapturedThunk),
		                                Step = Forward<F>(Step)]() mutable
			-> TResult<NewTuple, E>
		{
			// Execute previous steps
			auto PrevResult = CapturedThunk();
			if (PrevResult.IsErr())
			{
				return TResult<NewTuple, E>::Err(MoveTemp(PrevResult).GetError());
			}

			Tuple AccumulatedValues = MoveTemp(PrevResult).Unwrap();

			// Execute this step
			auto StepResultVal = Detail::Apply(Step, AccumulatedValues);
			if (StepResultVal.IsErr())
			{
				return TResult<NewTuple, E>::Err(MoveTemp(StepResultVal).GetError());
			}

			// Extend tuple
			NewTuple Extended = std::tuple_cat(
				MoveTemp(AccumulatedValues),
				std::make_tuple(MoveTemp(StepResultVal).Unwrap())
			);

			return TResult<NewTuple, E>::Ok(MoveTemp(Extended));
		});
	}

	/**
	 * Build the final lazy value.
	 * Returns a TLazy that will evaluate the entire chain when accessed.
	 */
	TLazy<Tuple, E> Build() &&
	{
		return TLazy<Tuple, E>(MoveTemp(Thunk_));
	}

	/**
	 * Force immediate evaluation (like TValidateChain::Finish)
	 */
	TResult<Tuple, E> Evaluate() &&
	{
		return Thunk_();
	}

private:
	ThunkType Thunk_;
};

/**
 * Start a lazy validation chain.
 *
 * Unlike Validate(), evaluation is deferred until Build().Get() or Evaluate().
 */
template<typename E = FString>
TLazyChain<E, std::tuple<>> LazyValidate()
{
	return TLazyChain<E, std::tuple<>>([]() {
		return TResult<std::tuple<>, E>::Ok(std::tuple<>{});
	});
}


// ============================================================================
// Pipeline Operator - Chainable transformations
// ============================================================================

// Wrapper to enable | operator chaining
template<typename F>
struct TPipe
{
	F Func;

	explicit TPipe(F&& Fn) : Func(Forward<F>(Fn)) {}
};

template<typename F>
TPipe<F> Pipe(F&& Fn) { return TPipe<F>(Forward<F>(Fn)); }

// Enable: value | Pipe([](auto x) { return x + 1; })
template<typename T, typename F>
auto operator|(T&& Value, TPipe<F> Pipe) -> decltype(Pipe.Func(Forward<T>(Value)))
{
	return Pipe.Func(Forward<T>(Value));
}

// Common pipe operations
inline auto Transform = [](auto Fn) {
	return Pipe([Fn = MoveTemp(Fn)](auto&& Value) {
		return Fn(Forward<decltype(Value)>(Value));
	});
};

inline auto Filter = [](auto Pred) {
	return Pipe([Pred = MoveTemp(Pred)](auto&& Container) {
		using T = typename std::decay_t<decltype(Container)>::ElementType;
		std::decay_t<decltype(Container)> Result;
		for (auto&& Item : Container)
		{
			if (Pred(Item))
			{
				Result.Add(Forward<decltype(Item)>(Item));
			}
		}
		return Result;
	});
};

inline auto ForEach = [](auto Fn) {
	return Pipe([Fn = MoveTemp(Fn)](auto&& Container) {
		for (auto&& Item : Container)
		{
			Fn(Item);
		}
		return Forward<decltype(Container)>(Container);
	});
};


// ============================================================================
// Async Combinators - For io_uring style async operations
// ============================================================================

// Represents a pending async operation with continuation
template<typename T>
class TAsyncOp
{
public:
	using ContinuationType = TFunction<void(T)>;

	TAsyncOp() = default;

	// Chain a continuation
	template<typename F>
	auto Then(F&& Fn) -> TAsyncOp<decltype(Fn(DeclVal<T>()))>
	{
		using U = decltype(Fn(DeclVal<T>()));
		TAsyncOp<U> Next;

		Continuations.Add([Fn = Forward<F>(Fn), Next](T Value) mutable
		{
			U Result = Fn(MoveTemp(Value));
			Next.Complete(MoveTemp(Result));
		});

		return Next;
	}

	// Complete the operation, triggering continuations
	void Complete(T Value)
	{
		for (auto& Cont : Continuations)
		{
			Cont(Value);
		}
		Continuations.Empty();
	}

	// Add a raw continuation
	void OnComplete(ContinuationType Fn)
	{
		Continuations.Add(MoveTemp(Fn));
	}

private:
	TArray<ContinuationType> Continuations;
};


// ============================================================================
// Builder Pattern via Lambdas
// ============================================================================

// Fluent builder using lambdas for configuration
template<typename T>
class TBuilder
{
public:
	TBuilder() : Instance() {}

	template<typename F>
	TBuilder& With(F&& Fn)
	{
		Fn(Instance);
		return *this;
	}

	T Build() { return MoveTemp(Instance); }
	T&& Extract() { return MoveTemp(Instance); }

private:
	T Instance;
};

template<typename T>
TBuilder<T> Build() { return TBuilder<T>(); }


// ============================================================================
// State Machine via Lambdas
// ============================================================================

// Type-erased state that can transition
template<typename Context>
class TStateMachine
{
public:
	using StateFunc = TFunction<TFunction<void(Context&)>(Context&)>;

	TStateMachine(StateFunc InitialState) : CurrentState(MoveTemp(InitialState)) {}

	void Tick(Context& Ctx)
	{
		if (CurrentState)
		{
			auto NextState = CurrentState(Ctx);
			if (NextState)
			{
				CurrentState = [Next = MoveTemp(NextState)](Context& C) -> StateFunc
				{
					Next(C);
					return nullptr;  // One-shot transition
				};
			}
		}
	}

	void TransitionTo(StateFunc NewState)
	{
		CurrentState = MoveTemp(NewState);
	}

private:
	StateFunc CurrentState;
};


// ============================================================================
// Retry Combinator
// ============================================================================

template<typename F, typename E = FString>
auto Retry(int32 MaxAttempts, F&& Fn) -> TResult<decltype(Fn().Unwrap()), E>
{
	using T = decltype(Fn().Unwrap());

	for (int32 i = 0; i < MaxAttempts; ++i)
	{
		auto Result = Fn();
		if (Result.IsOk())
		{
			return TResult<T, E>::Ok(MoveTemp(Result).Unwrap());
		}

		if (i == MaxAttempts - 1)
		{
			return TResult<T, E>::Err(MoveTemp(Result).GetError());
		}
	}

	return TResult<T, E>::Err(TEXT("Retry exhausted"));
}


// ============================================================================
// Scope Guard Variants
// ============================================================================

// Note: TScopeSuccess/TScopeFailure would require std::uncaught_exceptions (C++17)
// which isn't available in UE's bundled libc++. Use TDefer with explicit
// Cancel() calls for conditional cleanup instead:
//
//   auto Guard = Defer([&]() { Cleanup(); });
//   if (SomeCondition) { Guard.Cancel(); }
//

// ============================================================================
// Phantom Types / Typestate - Compile-time state encoding
// ============================================================================
//
// Encode validation state in the type system. Invalid transitions become
// compile errors, not runtime errors.
//
// Usage:
//   // Define state tags (empty structs, zero runtime cost)
//   struct HasRhi {};
//   struct HasTimeline {};
//   struct Ready {};
//
//   // Create a typed state machine
//   auto ctx = TPhantomState<FGpuData>::Create({})
//       .Transition<HasRhi>([](auto& data) { data.Rhi = GetRhi(); return Ok(true); })
//       .Transition<HasTimeline>([](auto& data) { ... })
//       .Require<HasRhi, HasTimeline>()  // Compile-time assertion
//       .Get();
//
// The type changes with each transition:
//   TPhantomState<Data>
//   TPhantomState<Data, HasRhi>
//   TPhantomState<Data, HasRhi, HasTimeline>

namespace Detail
{
	// Check if Tag is in Tags... (fold expression)
	template<typename Tag, typename... Tags>
	inline constexpr bool HasTag = (std::is_same_v<Tag, Tags> || ...);

	// Check if ALL of Required... are in Available...
	template<typename Available, typename... Required>
	struct HasAllTags;

	template<typename... Available, typename... Required>
	struct HasAllTags<std::tuple<Available...>, Required...>
	{
		static constexpr bool Value = (HasTag<Required, Available...> && ...);
	};
}

/**
 * TPhantomState - A value with compile-time state tags
 *
 * @tparam Data The actual data being carried
 * @tparam Tags... Zero-cost type tags encoding validated states
 */
template<typename Data, typename... Tags>
class TPhantomState
{
public:
	explicit TPhantomState(Data Value) : Value_(MoveTemp(Value)) {}

	// Move-only
	TPhantomState(TPhantomState&&) = default;
	TPhantomState& operator=(TPhantomState&&) = default;
	TPhantomState(const TPhantomState&) = delete;
	TPhantomState& operator=(const TPhantomState&) = delete;

	/**
	 * Create initial state with no tags
	 */
	static TPhantomState<Data> Create(Data Initial)
	{
		return TPhantomState<Data>(MoveTemp(Initial));
	}

	/**
	 * Transition to a new state by adding a tag.
	 * The function receives mutable access to data and returns TResult.
	 *
	 * @tparam NewTag The tag to add upon successful transition
	 * @param Fn Function (Data&) -> TResult<T, E> that performs validation
	 * @return New TPhantomState with NewTag added, or error
	 */
	template<typename NewTag, typename E = FString, typename F>
	auto Transition(F&& Fn) && -> TResult<TPhantomState<Data, Tags..., NewTag>, E>
	{
		auto Result = Fn(Value_);
		if (Result.IsErr())
		{
			return TResult<TPhantomState<Data, Tags..., NewTag>, E>::Err(
				MoveTemp(Result).GetError()
			);
		}

		return TResult<TPhantomState<Data, Tags..., NewTag>, E>::Ok(
			TPhantomState<Data, Tags..., NewTag>(MoveTemp(Value_))
		);
	}

	/**
	 * Transform the data while keeping the same tags.
	 */
	template<typename F>
	auto Map(F&& Fn) && -> TPhantomState<decltype(Fn(std::declval<Data>())), Tags...>
	{
		using NewData = decltype(Fn(std::declval<Data>()));
		return TPhantomState<NewData, Tags...>(Fn(MoveTemp(Value_)));
	}

	/**
	 * Compile-time assertion that required tags are present.
	 * Returns *this unchanged, but fails to compile if tags missing.
	 */
	template<typename... Required>
	auto Require() && -> TPhantomState<Data, Tags...>
	{
		static_assert(
			Detail::HasAllTags<std::tuple<Tags...>, Required...>::Value,
			"Missing required state tags"
		);
		return MoveTemp(*this);
	}

	/**
	 * Check at compile time if a tag is present
	 */
	template<typename Tag>
	static constexpr bool Has()
	{
		return Detail::HasTag<Tag, Tags...>;
	}

	/**
	 * Get the underlying data (moves out)
	 */
	Data Unwrap() && { return MoveTemp(Value_); }

	/**
	 * Access data by reference
	 */
	Data& Get() & { return Value_; }
	const Data& Get() const& { return Value_; }

	// ========================================================================
	// Conditional Methods - Only available when required tags are present
	// ========================================================================
	// Use SFINAE to enable methods based on accumulated tags.
	// This is where the compile-time magic happens!

	/**
	 * Example: A method only available when HasRhi tag is present
	 * Usage: Define your own methods in derived/wrapper classes:
	 *
	 *   template<typename... Ts>
	 *   auto DoSomethingWithRhi()
	 *       -> std::enable_if_t<Has<GpuTags::HasRhi>(), ReturnType>
	 *   { ... }
	 */

private:
	Data Value_;

	// Allow different instantiations to access each other's private members
	template<typename D, typename... Ts>
	friend class TPhantomState;
};

/**
 * TPhantomChain - Chainable phantom state transitions
 *
 * Wraps TPhantomState to allow fluent chaining of fallible transitions.
 * Accumulates errors and short-circuits like TValidateChain.
 */
template<typename Data, typename E, typename... Tags>
class TPhantomChain
{
public:
	explicit TPhantomChain(TResult<TPhantomState<Data, Tags...>, E> State)
		: State_(MoveTemp(State)) {}

	// Move-only
	TPhantomChain(TPhantomChain&&) = default;
	TPhantomChain& operator=(TPhantomChain&&) = default;

	/**
	 * Add a transition. Short-circuits if previous transition failed.
	 */
	template<typename NewTag, typename F>
	auto Then(F&& Fn) && -> TPhantomChain<Data, E, Tags..., NewTag>
	{
		using NewChain = TPhantomChain<Data, E, Tags..., NewTag>;
		using NewState = TPhantomState<Data, Tags..., NewTag>;

		if (State_.IsErr())
		{
			return NewChain(TResult<NewState, E>::Err(MoveTemp(State_).GetError()));
		}

		auto CurrentState = MoveTemp(State_).Unwrap();
		auto Result = MoveTemp(CurrentState).template Transition<NewTag, E>(Forward<F>(Fn));

		return NewChain(MoveTemp(Result));
	}

	/**
	 * Finish the chain, returning the final TResult
	 */
	TResult<TPhantomState<Data, Tags...>, E> Finish() &&
	{
		return MoveTemp(State_);
	}

	/**
	 * Finish and unwrap, returning the data directly (crashes on error)
	 */
	Data Unwrap() &&
	{
		return MoveTemp(State_).Unwrap().Unwrap();
	}

	/**
	 * Check if any transition has failed
	 */
	bool IsErr() const { return State_.IsErr(); }

private:
	TResult<TPhantomState<Data, Tags...>, E> State_;
};

/**
 * Start a chainable phantom state sequence
 */
template<typename Data, typename E = FString>
TPhantomChain<Data, E> PhantomChain(Data Initial)
{
	return TPhantomChain<Data, E>(
		TResult<TPhantomState<Data>, E>::Ok(
			TPhantomState<Data>(MoveTemp(Initial))
		)
	);
}

/**
 * Convenience: Start a phantom state chain
 */
template<typename Data>
TPhantomState<Data> Phantom(Data Initial)
{
	return TPhantomState<Data>::Create(MoveTemp(Initial));
}


// ============================================================================
// GPU Context Typestate Example Tags
// ============================================================================
// These can be used with TPhantomState for GPU validation:
//
//   struct FGpuData {
//       IVulkanDynamicRHI* Rhi = nullptr;
//       VkSemaphore TimelineSem = VK_NULL_HANDLE;
//       uint64 TimelineValue = 0;
//       TUniquePtr<FExportableSemaphore> Semaphore;
//   };
//
//   namespace GpuTags {
//       struct Initialized {};
//       struct HasRhi {};
//       struct HasTimeline {};
//       struct HasSemaphore {};
//       struct Ready {};
//   }
//
//   auto ctx = Phantom(FGpuData{})
//       .Transition<GpuTags::HasRhi>([](FGpuData& d) {
//           d.Rhi = GetIVulkanDynamicRHI();
//           return d.Rhi ? Ok(true) : Err<bool>(TEXT("No RHI"));
//       })
//       .Transition<GpuTags::HasTimeline>([](FGpuData& d) {
//           return d.Rhi->GetTimeline(&d.TimelineSem, &d.TimelineValue)
//               ? Ok(true) : Err<bool>(TEXT("No timeline"));
//       })
//       .Require<GpuTags::HasRhi, GpuTags::HasTimeline>()  // Compile-time check!
//       .Unwrap();

} // namespace Flight::Functional


// ============================================================================
// Convenience Macros (the closest we get to Rust's macro_rules!)
// ============================================================================

// DEFER - creates uniquely named guard
#define FLIGHT_DEFER_CONCAT_IMPL(x, y) x##y
#define FLIGHT_DEFER_CONCAT(x, y) FLIGHT_DEFER_CONCAT_IMPL(x, y)
#define FLIGHT_DEFER auto FLIGHT_DEFER_CONCAT(_defer_, __LINE__) = ::Flight::Functional::Defer

// Usage:
//   FLIGHT_DEFER([&]() { close(fd); });
//   FLIGHT_DEFER([&]() { mutex.Unlock(); });

// TRY - early return on error (like Rust's ? operator, but as a macro)
#define FLIGHT_TRY(expr) \
	({ \
		auto _result = (expr); \
		if (_result.IsErr()) { return ::Flight::Functional::Err<decltype(_result.Unwrap())>(_result.GetError()); } \
		MoveTemp(_result).Unwrap(); \
	})

// Note: FLIGHT_TRY uses GCC statement expressions, won't work on MSVC
