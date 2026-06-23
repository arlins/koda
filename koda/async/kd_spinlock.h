/** ****************************************
Created by: Arlin (arlins.dps@gmail.com).
This file implements a SpinLock
********************************************/
	
#pragma once

#include <atomic>
#include "koda/kd_global.h"
#include "koda/base/kd_utils.h"

__NAMESPACE_KD_BEGIN

class SpinLock {
	KDD_DEFINE_SELFDEADLOCK_DETECTION(SpinLock);

private:
	std::atomic<bool> locked{ false };

public:
	SpinLock() = default;

	SpinLock(const SpinLock&) = delete;
	SpinLock& operator=(const SpinLock&) = delete;

	void lock() {
		KDD_SDL_CHECK_PRE_LOCK();
		while (locked.exchange(true, std::memory_order_acquire)) {
			while (locked.load(std::memory_order_relaxed)) {
				KD_PAUSE_ASM();
			}
		}
		KDD_SDL_RECORD_POST_LOCK();
	}

	bool try_lock() {
		KDD_SDL_CHECK_PRE_LOCK();
		if (!locked.exchange(true, std::memory_order_acquire)) {
			KDD_SDL_RECORD_POST_LOCK();
			return true;
		};
		return false;
	}

	void unlock() {
		KDD_SDL_CHECK_PRE_UNLOCK();
		locked.store(false, std::memory_order_release);
		KDD_SDL_RECORD_POST_UNLOCK();
	}
};

__NAMESPACE_KD_END