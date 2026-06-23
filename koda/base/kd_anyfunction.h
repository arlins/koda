/* *******************************************************************************
	Created by: Arlin (arlins.dps@gmail.com).

	any_function
	any_function uses type erasure to store a function pointer
	of any class or global function and then it can be called with
	the specified function signature to call the real function.

	Important note:
	1. When using any_function, ensure that the function signature declared
	when calling it matches the member function signature when initializing it.
	Otherwise, the generated binary code during compilation will have incorrect
	parameters pushed onto the stack, meaning the parameters will not
	match the original function's requirements, which can lead to a crash.

	2. Because the invoke function cannot make any assumptions about its return value,
	it does not perform any checks before calling the actual function.
	Therefore, before calling the invoke function, you must use if(any_function_var)
	to ensure that any_function is valid.

	Usage:
		struct ClassA {
			int hello(int a) { return ++a;}
		};

		ClassA obj;
		any_function amf(&ClassA::hello);
		if(amf) {
			auto res = amf.invoke_mf<int(int)>(&obj, 1);
			// or: auto res = amf.invoke_as(&ClassA::hello, &obj, 1);
		}
 ******************************************************************************* */

#pragma once

#include "koda/kd_global.h"
#include "koda/base/kd_typetraits.h"

__NAMESPACE_KD_BEGIN

class any_function
{
private:
	// Use unsigned char to hold the address of function pointer
	// Pointers to functions may be up to 16 bytes for virtual classes,
	// so make sure we have enough space to store it.
	unsigned char pmethod[16];

	typedef void (*invoker_t)(const any_function*);
	invoker_t static_invoker = nullptr;

	template< typename From, typename To >
	union invoker_caster { From from; To to; };

	// FuncHandler
	template <typename F>
	struct FuncHandler;

	// FuncHandler - R(*)(FnArgs...)
	template <typename R, typename... FnArgs>
	struct FuncHandler<R(*)(FnArgs...)> {
		static constexpr bool is_mem_fn = false;
		static R invoke(const any_function* self, FnArgs... args) {
			R(*pm)(FnArgs...);
			std::memcpy(&pm, self->pmethod, sizeof(pm));
			return (*pm)(std::forward<FnArgs>(args)...);
		}
	};

	// FuncHandler - R(T::*)(FnArgs...)
	template <typename T, typename R, typename... FnArgs>
	struct FuncHandler<R(T::*)(FnArgs...)> {
		static constexpr bool is_mem_fn = true;
		static R invoke(const any_function* self, T* obj, FnArgs... args) {
			R(T:: * pm)(FnArgs...);
			std::memcpy(&pm, self->pmethod, sizeof(pm));
			return (obj->*pm)(std::forward<FnArgs>(args)...);
		}
	};

	// FuncHandler - R(T::*)(FnArgs...) const
	template <typename T, typename R, typename... FnArgs>
	struct FuncHandler<R(T::*)(FnArgs...) const> {
		static constexpr bool is_mem_fn = true;
		static R invoke(const any_function* self, const T* obj, FnArgs... args) {
			R(T:: * pm)(FnArgs...) const;
			std::memcpy(&pm, self->pmethod, sizeof(pm));
			return (obj->*pm)(std::forward<FnArgs>(args)...);
		}
	};

public:
	template<typename F>
	any_function(F pm) {
		KD_STATIC_ASSERT_M(sizeof(F) <= sizeof(pmethod), "Pointer too large.");
		std::memcpy(pmethod, &pm, sizeof(F));

		using handler_t = decltype(&FuncHandler<F>::invoke);
		invoker_caster<handler_t, invoker_t> c;
		c.from = &FuncHandler<F>::invoke;
		static_invoker = c.to;
	}

public:
	any_function()
		: static_invoker(nullptr) {
	};

	any_function(const any_function& o) {
		static_invoker = o.static_invoker;
		std::memcpy(pmethod, o.pmethod, sizeof(pmethod));
	}

	any_function(any_function&& o) noexcept {
		static_invoker = o.static_invoker;
		std::memcpy(pmethod, o.pmethod, sizeof(pmethod));
		o.static_invoker = nullptr;
	}

	any_function& operator= (const any_function& o) {
		if (this != &o) {
			static_invoker = o.static_invoker;
			std::memcpy(pmethod, o.pmethod, sizeof(pmethod));
		}

		return *this;
	}

	any_function& operator= (any_function&& o) noexcept {
		if (this != &o) {
			static_invoker = o.static_invoker;
			std::memcpy(pmethod, o.pmethod, sizeof(pmethod));
			o.static_invoker = nullptr;
		}

		return *this;
	}

public:  // invoke
	// Invoke any function
	template<typename Signature, typename... CallArgs>
	typename std::enable_if<
		sizeof...(CallArgs) == function_traits<Signature>::arity,
		typename function_traits<Signature>::return_type
	>::type
		invoke(CallArgs&&... args) const {
		using FnSignature = function_traits<Signature>;
		return _invoke_fn<Signature>(std::make_index_sequence<FnSignature::arity>{},
			std::forward<CallArgs>(args)...);
	}

	template<typename Signature, typename... CallArgs>
	typename std::enable_if<
		sizeof...(CallArgs) == function_traits<Signature>::arity + 1,
		typename function_traits<Signature>::return_type
	>::type
		invoke(CallArgs&&... args) const {
		return _invoke_mfn_dispatch<Signature>(std::forward<CallArgs>(args)...);
	}

	// Invoke function only
	template<typename Signature, typename... CallArgs>
	typename function_traits<Signature>::return_type
		invoke_fn(CallArgs&&... args) const {
		using FnSignature = function_traits<Signature>;
		return _invoke_fn<Signature>(std::make_index_sequence<FnSignature::arity>{},
			std::forward<CallArgs>(args)...);
	}

	// Invoke member function only
	template<typename Signature, typename... CallArgs>
	typename function_traits<Signature>::return_type
		invoke_mfn(void* obj, CallArgs... args) const {
		using FnSignature = function_traits<Signature>;
		return _invoke_mfn<Signature>(std::make_index_sequence<FnSignature::arity>{},
			obj, std::forward<CallArgs>(args)...);
	}

public: // invoke_as
	// Invoke as function: R(*)(FnArgs...)
	template<typename R, typename ... FnArgs, typename ... CallArgs >
	auto invoke_as(R(*)(FnArgs...), CallArgs&&... args) -> R {
		return _invoke_fn_impl<R, FnArgs...>(std::forward<CallArgs>(args)...);
	}

	// Invoke as member function: R(T::*)(FnArgs...)
	template<typename T, typename R, typename ... FnArgs, typename ... CallArgs >
	auto invoke_as(R(T::* fn)(FnArgs...), T* obj, CallArgs&&... args) -> R {
		return _invoke_mfn_impl<R, FnArgs...>(obj, std::forward<CallArgs>(args)...);
	}

	// Invoke as member function: R(T::*)(FnArgs...) const
	template<typename T, typename R, typename ... FnArgs, typename ... CallArgs >
	auto invoke_as(R(T::* fn)(FnArgs...) const, T* obj, CallArgs&&... args) const -> R {
		return _invoke_mfn_impl<R, FnArgs...>(obj, std::forward<CallArgs>(args)...);
	}

public:
	explicit operator bool() const {
		return static_invoker != nullptr;
	}

private: // Function
	template<typename Signature, size_t ...Is, typename... CallArgs>
	typename function_traits<Signature>::return_type
		_invoke_fn(std::index_sequence<Is...>, CallArgs&&... args) const {
		using FnSignature = function_traits<Signature>;
		using R = typename FnSignature::return_type;
		return _invoke_fn_impl<R, typename FnSignature::template args_type<Is>...>(
			std::forward<CallArgs>(args)...);
	}

	// FnArgs must be the same as the function when initializing
	template<typename R, typename ... FnArgs, typename ... CallArgs >
	R _invoke_fn_impl(CallArgs... args) const {
		KD_ASSERT_M(static_invoker != nullptr, "Attempting to invoke an empty function");
		typedef R(*static_invoke_t)(const any_function*, FnArgs...);
		invoker_caster< invoker_t, static_invoke_t > caster;
		caster.from = static_invoker;
		return (caster.to)(this, std::forward<CallArgs>(args)...);
	}

private: // Member function
	template<typename Signature, typename T, typename... RestArgs>
	typename function_traits<Signature>::return_type
		_invoke_mfn_dispatch(T&& obj, RestArgs&&... rest_args) const {
		using FnSignature = function_traits<Signature>;
		return _invoke_mfn<Signature>(std::make_index_sequence<FnSignature::arity>{},
			static_cast<void*>(obj), std::forward<RestArgs>(rest_args)...);
	}

	template<typename Signature, size_t ...Is, typename... CallArgs>
	typename function_traits<Signature>::return_type
		_invoke_mfn(std::index_sequence<Is...>, void* obj, CallArgs... args) const {
		using FnSignature = function_traits<Signature>;
		using R = typename FnSignature::return_type;
		return _invoke_mfn_impl<R, typename FnSignature::template args_type<Is>...>(
			obj, std::forward<CallArgs>(args)...);
	}

	// FnArgs must be the same as the member function when initializing
	template<typename R, typename ... FnArgs, typename ... CallArgs >
	R _invoke_mfn_impl(void* obj, CallArgs... args) const {
		KD_ASSERT_M(static_invoker != nullptr, "Attempting to invoke an empty member function");
		typedef R(*static_invoke_t)(const any_function*, void*, FnArgs...);
		invoker_caster< invoker_t, static_invoke_t > caster;
		caster.from = static_invoker;
		return (caster.to)(this, obj, std::forward<CallArgs>(args)...);
	}
};

__NAMESPACE_KD_END