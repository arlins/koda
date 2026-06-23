/** ****************************************************************
 Created by: Arlin (arlins.dps@gmail.com).

 TrackPointer
 TrackPointer is a type of intelligent pointer that holds an object
 and supports automatic management. When the object is released
 anywhere, this pointer can intelligently detect that the object
 has been released.

 Usage:
	class Object{
		KD_TRACKABLE_OBJECT(Object)
		void hello(){ printf("hello"); }
	};

	auto obj = new Object();
	TrackPointer<Object> ptr(obj);
	delete obj;
	if(ptr) { ptr->hello(); }
***************************************************************** */


#pragma once

#include <memory>
#include <type_traits>
#include "koda/kd_global.h"

__NAMESPACE_KD_BEGIN

namespace tracking_internal
{
// Check T::TrackableRefCountData____
template <typename T, typename = void>
struct has_trackableRefCountDataClass : std::false_type
{};

template <typename T>
struct has_trackableRefCountDataClass<T,
	kd::void_t<typename T::TrackableRefCountData____>> : std::true_type
{};

// Check T::trackableRefData____()
template <typename T, typename = void>
struct has_trackableRefDataFunction : std::false_type
{};

template <typename T>
struct has_trackableRefDataFunction < T,
	std::enable_if_t<std::is_same<
	decltype(std::declval<T>().trackableRefData____()),
	std::shared_ptr<typename T::TrackableRefCountData____>>::value,
	void> > : std::true_type
{};

// Check T::TrackableRefCountData____ and T::trackableRefData____()
template <typename T, typename = void>
struct has_trackableRefCountData : std::false_type
{};

template <typename T>
struct has_trackableRefCountData < T,
	kd::void_t<decltype(std::declval<T>().trackableRefData____(),
		typename T::TrackableRefCountData____{},
		void()) >> : std::true_type
{};

// is_trackable_object_v
template <typename T>
constexpr bool is_trackable_object_v =
has_trackableRefCountDataClass<T>::value && has_trackableRefDataFunction<T>::value;
};

// TrackingPointer
template<typename T>
class TrackPointer {
	KD_STATIC_ASSERT_M(tracking_internal::is_trackable_object_v<T>,
		"Object must be defined using KD_TRACKABLE_OBJECT(Class).");
	using TrackableRefCountData = typename T::TrackableRefCountData____;

private:
	T* m_ptr;
	std::weak_ptr<TrackableRefCountData> m_weakRefData;

public:
	TrackPointer()
		: m_ptr(nullptr)
		, m_weakRefData() {
	};

	TrackPointer(T* ptr)
		: m_ptr(nullptr)
		, m_weakRefData() {
		if (ptr) {
			m_weakRefData = ptr->trackableRefData____();
			m_ptr = ptr;
		}
	}

	~TrackPointer() {
		m_ptr = nullptr;
		m_weakRefData.reset();
	}

	// Copy
	TrackPointer(const TrackPointer& o)
		: m_ptr(o.m_ptr)
		, m_weakRefData(o.m_weakRefData) {
	}

	TrackPointer& operator=(const TrackPointer& o) {
		if (this != &o) {
			m_ptr = o.m_ptr;
			m_weakRefData = o.m_weakRefData;
		}
		return *this;
	};

	// Move
	TrackPointer(TrackPointer&& o) noexcept
		: m_ptr(o.m_ptr)
		, m_weakRefData(o.m_weakRefData) {
		o.m_ptr = nullptr;
		o.m_weakRefData.reset();
	}

	TrackPointer& operator=(TrackPointer&& o) noexcept {
		if (this != &o) {
			m_ptr = o.m_ptr;
			m_weakRefData = o.m_weakRefData;
			o.m_ptr = nullptr;
			o.m_weakRefData.reset();
		}
		return *this;
	};

public:
	T* get() const {
		if (m_weakRefData.expired()) {
			return nullptr;
		}
		return m_ptr;
	}

	// if(ptr)
	operator bool() const {
		return get() != nullptr;
	}

	// ptr->
	T* operator->() const {
		return get();
	}

	// ptr = nullptr;
	TrackPointer& operator=(std::nullptr_t) {
		m_ptr = nullptr;
		m_weakRefData.reset();
		return *this;
	};
};

__NAMESPACE_KD_END


// KD_TRACKABLE_OBJECT
#define KD_TRACKABLE_OBJECT(Class) \
public:\
struct TrackableRefCountData____ {}; \
mutable std::shared_ptr<TrackableRefCountData____> m_trackableRefData____; \
std::shared_ptr<TrackableRefCountData____> trackableRefData____() const { \
	if (!m_trackableRefData____) { \
		m_trackableRefData____ = ::kd::make_shared<TrackableRefCountData____>(); \
	} \
\
	return m_trackableRefData____; \
}