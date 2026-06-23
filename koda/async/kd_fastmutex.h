/** ****************************************************
Created by: Arlin (arlins.dps@gmail.com).

FastMutex
It is a high-performance lock used within a single process based on the
system's native API, employing User-mode Spin + Kernel-mode Wait.

Advantages compared to std::mutex:
1. Smaller memory footprint
2. Supports user-space spin, reducing kernel context switching overhead
3. Assembly-level optimizations for different platform instruction sets (ARM/X86).
**************************************************** **/

#pragma once
#include <mutex>
#include "koda/kd_global.h"
#include "koda/base/kd_utils.h"

#if defined(KD_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(KD_OS_DARWIN)
#include <os/lock.h>
#include <pthread.h>
#endif


__NAMESPACE_KD_BEGIN

// ======================
// Mutex
// ======================
class Mutex {
	KDD_DEFINE_SELFDEADLOCK_DETECTION(Mutex);

public:
	explicit Mutex() noexcept {
	}

	~Mutex() noexcept {
	}

	void lock() noexcept {
		KDD_SDL_CHECK_PRE_LOCK();
		m_mtx.lock();
		KDD_SDL_RECORD_POST_LOCK();
	}

	void unlock() noexcept {
		KDD_SDL_CHECK_PRE_UNLOCK();
		m_mtx.unlock();
		KDD_SDL_RECORD_POST_UNLOCK();
	}

	bool try_lock() noexcept {
		KDD_SDL_CHECK_PRE_LOCK();
		if (m_mtx.try_lock()) {
			KDD_SDL_RECORD_POST_LOCK();
			return true;
		}
		return false;
	}

private:
	std::mutex m_mtx;
};


// ======================
// FastMutex
// ======================
class FastMutex {
	KD_DISABLE_COPY(FastMutex);
	KD_DISABLE_MOVE(FastMutex);
	KDD_DEFINE_SELFDEADLOCK_DETECTION(FastMutex);

public:
	explicit FastMutex(unsigned int spin_count = 2000) noexcept;
	~FastMutex() noexcept;

	void lock() noexcept;
	void unlock() noexcept;
	bool try_lock() noexcept;

private:
#if defined(KD_OS_WIN)
	CRITICAL_SECTION m_handle;
#elif defined(KD_OS_DARWIN)
	os_unfair_lock   m_handle;
	unsigned int     m_spin_count;
#else
	std::mutex m_mtx;
#endif
}; // FastMutex


// ===============================
// FastMutex - Implementations
// ===============================
#if defined(KD_OS_WIN)
// =========================
// Implementation - Win32
// =========================

inline FastMutex::FastMutex(unsigned int spin_count) noexcept {
	InitializeCriticalSectionEx(&m_handle, (DWORD)spin_count, 0);
}

inline FastMutex::~FastMutex() noexcept {
	DeleteCriticalSection(&m_handle);
}

inline void FastMutex::lock() noexcept {
	KDD_SDL_CHECK_PRE_LOCK();
	EnterCriticalSection(&m_handle);
	KDD_SDL_RECORD_POST_LOCK();
}

inline void FastMutex::unlock() noexcept {
	KDD_SDL_CHECK_PRE_UNLOCK();
	LeaveCriticalSection(&m_handle);
	KDD_SDL_RECORD_POST_UNLOCK();
}

inline bool FastMutex::try_lock() noexcept {
	KDD_SDL_CHECK_PRE_LOCK();
	if (TryEnterCriticalSection(&m_handle) != FALSE) {
		KDD_SDL_RECORD_POST_LOCK();
		return true;
	};
	return false;
}

#elif defined(KD_OS_DARWIN)
// =========================
// Implementation - Darwin (Apple)
// =========================

inline FastMutex::FastMutex(unsigned int spin_count) noexcept
	: m_handle(OS_UNFAIR_LOCK_INIT)
	, m_spin_count(spin_count) {
}

inline FastMutex::~FastMutex() noexcept = default;

inline void FastMutex::lock() noexcept {
	KDD_SDL_CHECK_PRE_LOCK();
	for (unsigned int i = 0; i < m_spin_count; ++i) {
		if (os_unfair_lock_trylock(&m_handle)) {
			KDD_SDL_RECORD_POST_LOCK();
			return;
		}
		KD_PAUSE_ASM();
	}
	os_unfair_lock_lock(&m_handle);
	KDD_SDL_RECORD_POST_LOCK();
}

inline void FastMutex::unlock() noexcept {
	KDD_SDL_CHECK_PRE_UNLOCK();
	os_unfair_lock_unlock(&m_handle);
	KDD_SDL_RECORD_POST_UNLOCK();
}

inline bool FastMutex::try_lock() noexcept {
	KDD_SDL_CHECK_PRE_LOCK();
	if (os_unfair_lock_trylock(&m_handle)) {
		KDD_SDL_RECORD_POST_LOCK();
		return true;
	};
	return false;
}

#else

// =========================
// Implementation - Linux/Unix
// =========================
inline FastMutex::FastMutex(unsigned int /*spin_count*/) noexcept {
}

inline FastMutex::~FastMutex() noexcept {
}

inline void FastMutex::lock() noexcept {
	KDD_SDL_CHECK_PRE_LOCK();
	m_mtx.lock();
	KDD_SDL_RECORD_POST_LOCK();
}

inline void FastMutex::unlock() noexcept {
	KDD_SDL_CHECK_PRE_UNLOCK();
	m_mtx.unlock();
	KDD_SDL_RECORD_POST_UNLOCK();
}

inline bool FastMutex::try_lock() noexcept {
	KDD_SDL_CHECK_PRE_LOCK();
	if (m_mtx.try_lock()) {
		KDD_SDL_RECORD_POST_LOCK();
		return true;
	};
	return false;
}
#endif // KD_OS_WIN

__NAMESPACE_KD_END
