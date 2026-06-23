/* **************************************************************
 Created by: Arlin (arlins.dps@gmail.com).
 This file implements Memory-related APIs
************************************************************** */

#pragma once
#include <iostream>
#include <string>
#include <memory>
#include <utility>
#include <cstdint>
#include <cstdlib>
#include "koda/kd_global.h"

#if defined(KD_OS_WIN)
#include <malloc.h>
#endif

__NAMESPACE_KD_BEGIN

// make_unique (C++11)
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
	return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// make_shared
// The object held by the shared_ptr created by this method 
// will be released when all the shared_ptrs are destructed
// 
// The object held by the shared_ptr created by std::make_shared 
// will only be released when all shared_ptr and weak_ptr are destructed. 
// This may result in the memory not being released in a timely manner.
template<typename T, typename... Args>
std::shared_ptr<T> make_shared(Args&&... args) {
	return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
}

template<typename T>
bool is_same_weak_ptr(const std::weak_ptr<T>& a, const std::weak_ptr<T>& b) {
	return (!a.owner_before(b)) && (!b.owner_before(a));
}

__NAMESPACE_KD_END


// ==============================
// align
// ==============================
#if defined(KD_OS_WIN)
#include <malloc.h> // _aligned_malloc of Windows 
#elif defined(KD_OS_UNIX)
#include <stdlib.h> // posix_memalign of Linux/macOS
#endif

__NAMESPACE_KD_BEGIN

// Is the value a power of 2
inline bool is_pow_2(size_t value) {
	return value != 0 && (value & (value - 1)) == 0;
};

// Custom allocation of aligned memory
inline void* _aligned_malloc_(size_t alignment, size_t size) {
	// Calculate the total size that needs to be allocated
	size_t actual_size = size + alignment - 1 + sizeof(void*);

	// Allocate memory
	void* raw_ptr = malloc(actual_size);
	if (!raw_ptr) return nullptr;

	// Calculate the aligned address
	void* aligned_ptr = reinterpret_cast<void*>(
		(reinterpret_cast<uintptr_t>(raw_ptr) + sizeof(void*) + alignment - 1)
		& ~(alignment - 1)
		);

	// Store the original pointer before the alignment area to facilitate release.
	*(reinterpret_cast<void**>(aligned_ptr) - 1) = raw_ptr;

	return aligned_ptr;
}

// Custom release
inline void _aligned_free_(void* ptr) {
	void* raw_ptr = *(reinterpret_cast<void**>(ptr) - 1);
	free(raw_ptr);
}

// Aligning the pointer
inline void* _align_(size_t alignment, size_t size, size_t& space, void*& ptr) {
	size_t required = size + alignment - 1; // Required bytes
	if (space < required) return nullptr;

	return std::align(alignment, size, ptr, space);
}

// Allocate aligned memory (cross-platform)
inline void* aligned_malloc(size_t alignment, size_t size) {
	// Check if the alignment is a power of 2
	if (!is_pow_2(alignment)) {
		return nullptr;
	}

#if defined(KD_OS_WIN) // Win
	return _aligned_malloc(size, alignment);
#elif KD_CXX >= KD_CXX17 // C++17
	return std::aligned_alloc(alignment, size);
#elif defined(KD_OS_UNIX) // Unix
	void* ptr = nullptr;
	if (posix_memalign(&ptr, alignment, size) != 0) {
		return nullptr;
	}
	return ptr;
#else // Other platforms
	return _aligned_malloc_(alignment, size); // Custom
#endif
}

// Release aligned memory (cross-platform)
inline void aligned_free(void* ptr) {
	if (nullptr == ptr) {
		return;
	}

#if defined(KD_OS_WIN) // Win
	_aligned_free(ptr);
#elif KD_CXX >= KD_CXX17 // C++17
	free(ptr);
#elif defined(KD_OS_UNIX) // Unix
	free(ptr);
#else // Other platforms
	_aligned_free_(ptr); // Custom
#endif
}

__NAMESPACE_KD_END
