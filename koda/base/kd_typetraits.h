/* **************************************************************
 Created by: Arlin (arlins.dps@gmail.com).
 This file implements type-related APIs.
************************************************************** */

#pragma once

#include <iostream>
#include <string>
#include <type_traits>
#include <tuple>
#include <utility>
#include "koda/kd_global.h"


//
__NAMESPACE_KD_BEGIN

// void_t for C++14
template <class... _Types>
using void_t = void;

__NAMESPACE_KD_END


// ============================
// KD_DEFINE_HAS_MEMBER
// KD_DEFINE_HAS_FUNCTION
// ============================

// Usage:
// struct Class {
//	    int age;  
//	    void add(int, int) {}; 
// };
//
// KD_DEFINE_HAS_FUNCTION(add);
// KD_DEFINE_HAS_MEMBER(age);
// KD_STATIC_ASSERT_M(has_mem_age<Class>::value, "has no age");
// KD_STATIC_ASSERT_M(has_func_add<Class, int, int>::value, "has no add");

// KD_DEFINE_HAS_MEMBER
// has_mem_xxx<T>::value
#define KD_DEFINE_HAS_MEMBER(name) \
template<typename, typename = void> \
struct has_mem_##name : std::false_type {}; \
\
template<typename T> \
struct has_mem_##name<T, kd::void_t<decltype(std::declval<T>().name)>>: std::true_type {}; \
\
template<typename T> \
static constexpr bool has_mem_##name##_v = has_mem_##name<T>::value;


// KD_DEFINE_HAS_FUNCTION
// has_func_xxx<T>::value
#define KD_DEFINE_HAS_FUNCTION(name) \
template<typename T, typename... Args> \
struct has_func_##name { \
    template<typename U> \
    static auto test(int) -> decltype( \
        std::declval<U>().name(std::declval<Args>()...), \
        std::true_type{} \
    ); \
\
    template<typename> \
    static auto test(...) -> std::false_type; \
\
    static constexpr bool value = decltype(test<T>(0))::value; \
}; \
\
template<typename T, typename... Args> \
static constexpr bool has_func_##name##_v = has_func_##name<T, Args...>::value;


//
__NAMESPACE_KD_BEGIN

// Extract container type
// using T = container_traits<decltype(vec)>::type;
template <typename Container>
struct container_traits {
	using type = Container;
	using value_type = typename Container::value_type;
};

// function_traits
// Extract function information

// define
template<typename F>
struct function_traits;

// Partial specialization of non-class functions
// R(Args...)
template<typename R, typename... Args>
struct function_traits<R(Args...)> {
	using return_type = R;
	using args_tulpe_type = std::tuple<Args...>;

	template<std::size_t N>
	using args_type = typename std::tuple_element<N, args_tulpe_type>::type;

	static constexpr std::size_t arity = sizeof...(Args);
	static constexpr bool is_mem_fn = false;
};

// Partial specialization of non-class function pointers
// R(*)(Args...)
template<typename R, typename... Args>
struct function_traits<R(*)(Args...)> {
	using signature = R(Args...);
	using return_type = R;
	using args_tulpe_type = std::tuple<Args...>;

	template<std::size_t N>
	using args_type = typename std::tuple_element<N, args_tulpe_type>::type;

	static constexpr std::size_t arity = sizeof...(Args);
	static constexpr bool is_mem_fn = false;
};

// Partial specialization of non-const member functions of a class
// R(T::*)(Args...)
template<typename T, typename R, typename... Args>
struct function_traits<R(T::*)(Args...)> {  
	using signature = R(Args...);
	using return_type = R;
	using type = T;
	using args_tulpe_type = std::tuple<Args...>;

	template<std::size_t N>
	using args_type = typename std::tuple_element<N, args_tulpe_type>::type;

	static constexpr std::size_t arity = sizeof...(Args);
	static constexpr bool is_mem_fn = true;
};

// Partial specialization of const member functions of a class
// R(T::*)(Args...) const
template<typename T, typename R, typename... Args>
struct function_traits<R(T::*)(Args...) const> {  
	using signature = R(Args...);
	using return_type = R;
	using type = T;
	using args_tulpe_type = std::tuple<Args...>;

	template<std::size_t N>
	using args_type = typename std::tuple_element<N, args_tulpe_type>::type;

	static constexpr std::size_t arity = sizeof...(Args);
	static constexpr bool is_mem_fn = true;
};


// is_printable
// Check T is printable
template<typename T, typename = void>
struct is_printable : std::false_type {};

template<typename T>
struct is_printable<T,
	decltype(void(std::declval<std::ostream&>() << std::declval<T>()))>
	: std::true_type {};

// is_all_printable
// Check all Types of args are printable
template<typename... Args>
struct is_all_printable;

template<>
struct is_all_printable<> : std::true_type {};

template<typename FirstType, typename... RestArgs>
struct is_all_printable<FirstType, RestArgs...> :
	std::integral_constant<bool, is_printable<FirstType>::value && is_all_printable<RestArgs...>::value> {};

__NAMESPACE_KD_END