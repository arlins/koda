/* **************************************************************
 Created by: Arlin (arlins.dps@gmail.com).

 typecast_wrapper
 Automatic memory management for managed pointers
 Supports implicit conversion of wrapped pointers to T, T*, T&, T&
 It can used to extend the lifetime of variables or to manage variables
 for supporting asynchronous calls, etc.
************************************************************** */
#pragma once

#include <type_traits>
#include <stdexcept>
#include <utility>
#include "koda/kd_global.h"


__NAMESPACE_KD_BEGIN


template <typename T>
class typecast_wrapper {
	using value_type = typename std::remove_pointer<typename std::decay<T>::type>::type;
	using value_type_ptr = typename std::add_pointer<value_type>::type;
	mutable value_type_ptr value_ptr; //value_type*

public:
	typecast_wrapper() = delete; // Delete default constructor

	template <typename U>
	explicit typecast_wrapper(U* ptr) : value_ptr(ptr) {
		KD_STATIC_ASSERT_M((std::is_same<value_type, U>::value), "The value_type must be the same");
		KD_STATIC_ASSERT_M((std::is_pointer<U*>::value), "The typecast_wrapper must be initialized with a pointer");
	}

	~typecast_wrapper() noexcept {
		reset();
	}

	// Copy constructor
	typecast_wrapper(const typecast_wrapper& other)
		: value_ptr(nullptr) {
		if (other.value_ptr) {
			value_ptr = new value_type(*other.value_ptr);
		} else {
			value_ptr = nullptr;
		}
	}

	// Copy assignment
	typecast_wrapper& operator=(const typecast_wrapper& other) {
		if (this != &other) {
			reset();
			if (other.value_ptr) {
				value_ptr = new value_type(*other.value_ptr);
			} else {
				value_ptr = nullptr;
			}
		}

		return *this;
	}

	// Move constructor
	typecast_wrapper(typecast_wrapper&& other) noexcept
		: value_ptr(other.value_ptr) {
		other.value_ptr = nullptr;
	}

	// Move assignment
	typecast_wrapper& operator=(typecast_wrapper&& other) noexcept {
		if (this != &other) {
			reset();
			value_ptr = other.value_ptr;
			other.value_ptr = nullptr;
		}

		return *this;
	}

	void reset(value_type* new_value_ptr = nullptr) noexcept {
		if (new_value_ptr == value_ptr) {
			return; // Same obj
		}

		if (value_ptr) {
			delete value_ptr;
			value_ptr = nullptr;
		}

		value_ptr = new_value_ptr;
	}

	value_type* get_ptr() { return value_ptr; }

	// Implicit conversion to T (value type)
	// For: f(T), f(T&&)
	template <typename U = T,
		typename std::enable_if<!std::is_pointer<U>::value && !std::is_reference<U>::value, int>::type = 0>
		operator value_type() {
		if (value_ptr == nullptr) {
			throw std::runtime_error("empty value");
		}

		return *value_ptr;
	}

	// Implicitly convert to T*, 
	// For: f(T*)
	template <typename U = T,
		typename std::enable_if<std::is_pointer<U>::value, int>::type = 0>
		operator value_type* () noexcept {
		return value_ptr;
	}

	// Implicit conversion to T&.
	// For: f(T), f(T&), f(const T&)
	template <typename U = T,
		typename std::enable_if<std::is_lvalue_reference<U>::value, int>::type = 0>
		operator value_type& () {
		if (value_ptr == nullptr) {
			throw std::runtime_error("empty value");
		}

		return *value_ptr;
	}

	// Implicit conversion to T&&
	// For: f(T&&)
	template <typename U = T,
		typename std::enable_if<std::is_rvalue_reference<U>::value, int>::type = 0>
		operator value_type && () const {
		if (value_ptr == nullptr) {
			throw std::runtime_error("empty value");
		}

		return std::move(*value_ptr);
	}
};

//
template <typename T>
using typecast_wrapper_as_value = typecast_wrapper<T>;

template <typename T>
using typecast_wrapper_as_ptr = typecast_wrapper<T*>;

template <typename T>
using typecast_wrapper_as_ref = typecast_wrapper<T&>;

template <typename T>
using typecast_wrapper_as_rref = typecast_wrapper<T&&>;

//
template<typename T>
auto typecast_as_value(T* ptr) -> typecast_wrapper_as_value<T> {
	return typecast_wrapper_as_value<T>(ptr);
}

template<typename T>
auto typecast_as_ptr(T* ptr) -> typecast_wrapper_as_ptr<T> {
	return typecast_wrapper_as_ptr<T>(ptr);
}

template<typename T>
auto typecast_as_ref(T* ptr) -> typecast_wrapper_as_ref<T> {
	return typecast_wrapper_as_ref<T>(ptr);
}

template<typename T>
auto typecast_as_rref(T* ptr) -> typecast_wrapper_as_rref<T> {
	return typecast_wrapper_as_rref<T>(ptr);
}

__NAMESPACE_KD_END