/** **********************************************************
Created by: Arlin (arlins.dps@gmail.com).
Implementation of RLDispatcher on Unknown OS
This file is only intended to trick the compilation system into
compiling successfully.
**************************************************************/

#pragma once
#include "koda/runloop/kd_runloopdefs.h"

__NAMESPACE_KD_BEGIN

// =================================
// RLDispatcherDummy
// =================================
class RLDispatcherDummy : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLDispatcherDummy);

public:
	void construct() override { 
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId); 
		KD_ASSERT_M(false, "RLDispatcher unsupported on the OS"); 
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	};

	void addEventNotifier(const RLEventNotifier& eventNotifier) override { 
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	}

	void removeEventNotifier(RLEvent ev) override { 
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override { 
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId); 
		return std::shared_ptr<RLLooper>(nullptr);
	};

	bool processEvents(bool canWait) override { 
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId); 
		return false; 
	}

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
	}
};

__NAMESPACE_KD_END
