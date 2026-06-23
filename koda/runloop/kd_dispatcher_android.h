/** ********************************************
Created by: Arlin (arlins.dps@gmail.com).
This file implements RLDispatcher for Android
************************************************/

#pragma once
#include "koda/runloop/kd_runloopdefs.h"
#include "koda/runloop/kd_dispatcher_dummy.h"


#if defined(KD_OS_ANDROID)
#include <android/looper.h>

__NAMESPACE_KD_BEGIN

// =================================
// RLNativeLooperAndroid (base on ALooper)
// =================================
class RLNativeLooperAndroid : public RLLooper {
private:
	std::atomic<bool> m_exit{ false };
	std::atomic<int> m_exitCode{ 0 };
	ALooper* m_looper{ nullptr };

public:
	void construct(std::weak_ptr<RLContext> ctx) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		RLLooper::construct(ctx);

		// ALooper_prepare will create an ALooper if the current thread 
		// does not have one; otherwise, it will return the existing one.
		m_looper = ::ALooper_prepare(0);
		if (m_looper) {
			::ALooper_acquire(m_looper);
		}
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_looper) {
			::ALooper_release(m_looper);
			m_looper = nullptr;
		}

		RLLooper::destroy();
	}

	int exec() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		auto ctx = m_ctx.lock();
		if (ctx == nullptr || ctx->getDispatcher()->destroyed()) {
			return -1;
		}
		if (m_looper == nullptr) {
			return -1;
		}

		m_exit = false;
		m_exitCode = 0;

		//
		// ALooper_pollOnce Return Value:
		// ALOOPER_POLL_WAKE (-1): Wake-up by ALooper_wake()
		// ALOOPER_POLL_CALLBACK(-2) : A callback function registered via ALooper_addFd has finished executing
		//	ALOOPER_POLL_TIMEOUT(-3) : Timeout
		//	ALOOPER_POLL_ERROR(-4) : A critical error occurred(e.g., the epoll handle became invalid).
		//

		// Exec the loop
		while (!m_exit && !ctx->getDispatcher()->destroyed()) {
			int ident = ::ALooper_pollOnce(-1, nullptr, nullptr, nullptr);
			if (ident == ALOOPER_POLL_ERROR) {
				break;
			}
		}

		return m_exitCode.load();
	}

	void exit(int exitCode) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_exitCode = exitCode;
		m_exit = true;

		if (m_looper) {
			::ALooper_wake(m_looper);
		}
	}
};

// =================================
// RLNativeDispatcherAndroid (base on ALooper)
// =================================
class RLNativeDispatcherAndroid : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLNativeDispatcherAndroid);

private:
	ALooper* m_looper{ nullptr };
	int m_wakeFd[2]{ -1, -1 };
	int m_timerFd{ -1 }; 
	std::map<RLEvent, int> m_fdWatchMap;
	int64_t m_lastAbsoluteFireTimeMs{ -1 };

public:
	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		// Get looper
		m_looper = ::ALooper_forThread();
		if (m_looper == nullptr) {
			KD_ASSERT_M(false, "Fatal: ALooper_forThread returned null.");
			return;
		}
		::ALooper_acquire(m_looper);

		// Create wake-up fd
		if (::pipe2(m_wakeFd, O_CLOEXEC | O_NONBLOCK) == 0) {
			int ret = ::ALooper_addFd(m_looper, m_wakeFd[0], ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
				&RLNativeDispatcherAndroid::_wakePipeCallback, this);
			if (ret == -1) {
				::close(m_wakeFd[0]); ::close(m_wakeFd[1]);
				m_wakeFd[0] = -1; m_wakeFd[1] = -1;
				KD_ASSERT(false);
			}
		} else {
			KD_ASSERT(false);
		}

		// Create timer
		m_timerFd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
		if (m_timerFd != -1) {
			int ret = ::ALooper_addFd(m_looper, m_timerFd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
				&RLNativeDispatcherAndroid::_timerFdCallback, this);
			KD_ASSERT(ret == 1);
		} else {
			KD_ASSERT(false);
		}
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_looper == nullptr) {
			return;
		}

		// Clear wake-up fd
		if (m_wakeFd[0] != -1) {
			::ALooper_removeFd(m_looper, m_wakeFd[0]);
			::close(m_wakeFd[0]); ::close(m_wakeFd[1]);
			m_wakeFd[0] = -1; m_wakeFd[1] = -1;
		}

		// Clear timer fd
		if (m_timerFd != -1) {
			::ALooper_removeFd(m_looper, m_timerFd);
			::close(m_timerFd);
			m_timerFd = -1;
		}

		// // Clear external fds
		for (auto& pair : m_fdWatchMap) {
			::ALooper_removeFd(m_looper, pair.first);
		}
		m_fdWatchMap.clear();

		// Release looper
		::ALooper_release(m_looper);
		m_looper = nullptr;
		m_lastAbsoluteFireTimeMs = -1;
	};

	void addEventNotifier(const RLEventNotifier& eventNotifier) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_looper == nullptr) {
			return;
		}

		RLEvent nativeFd = eventNotifier.ev;
		int androidEvents = 0;
		if (eventNotifier.type & RLEventNotifier::READ) {
			androidEvents |= ALOOPER_EVENT_INPUT;
		}
		if (eventNotifier.type & RLEventNotifier::WRITE) {
			androidEvents |= ALOOPER_EVENT_OUTPUT;
		}

		int ret = ::ALooper_addFd(m_looper, nativeFd, ALOOPER_POLL_CALLBACK, androidEvents,
			&RLNativeDispatcherAndroid::_fdEventCallback, this);

		if (ret == 1) {
			m_fdWatchMap[nativeFd] = androidEvents;
		}
	}

	void removeEventNotifier(RLEvent ev) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_looper == nullptr) {
			return;
		}

		auto it = m_fdWatchMap.find(ev);
		if (it != m_fdWatchMap.end()) {
			::ALooper_removeFd(m_looper, ev);
			m_fdWatchMap.erase(it);
		}
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_looper == nullptr || m_timerFd == -1) {
			return;
		}

		int64_t nextAbsoluteFireTimeMs = getNextAbsoluteFireTimeMs();
		if (nextAbsoluteFireTimeMs == m_lastAbsoluteFireTimeMs) {
			return;
		}
		m_lastAbsoluteFireTimeMs = nextAbsoluteFireTimeMs;

		struct itimerspec newValue;
		::memset(&newValue, 0, sizeof(newValue));

		if (nextAbsoluteFireTimeMs < 0) {
			::timerfd_settime(m_timerFd, 0, &newValue, nullptr); // Cancel timer
		} else {
			int64_t delayMs = nextAbsoluteFireTimeMs - now_time();
			delayMs = delayMs > 0 ? delayMs : 0;

			newValue.it_value.tv_sec = delayMs / 1000;
			newValue.it_value.tv_nsec = (delayMs % 1000) * 1000000;
			::timerfd_settime(m_timerFd, 0, &newValue, nullptr); // Reset timer
		}
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		return std::shared_ptr<RLLooper>(new RLNativeLooperAndroid);
	};

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed()) {
			return;
		}

		int wakeUpFd = m_wakeFd[1];
		if (wakeUpFd != -1) {
			char dummy = 1;
			::write(wakeUpFd, &dummy, sizeof(dummy));
		}
	}

	bool processEvents(bool canWait) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		KD_ASSERT_M(!canWait, "canWait must be false in native mode");
		if (destroyed()) {
			return false;
		}

		// Process tasks and timers 
		processTasks();
		processTimerTasks();

		return true;
	}

private:
	static int _wakePipeCallback(int fd, int events, void* data) {
		if (data == nullptr) {
			return 0;
		}

		// We can only use raw pointers here. Using a raw pointer to the dispatcher 
		// is safe because the dispatcher will not be released until the run loop finished
		auto* dispatcher = static_cast<RLNativeDispatcherAndroid*>(data);
		KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
		KD_RUNLOOP_CHECK_SAME_THREAD(dispatcher->m_runLoopThreadId);
		if (dispatcher->destroyed()) {
			return 0;
		}

		// Clear wake-up buffers
		char buf[128];
		while (::read(fd, buf, sizeof(buf)) > 0) {}

		// Process events
		dispatcher->processEvents(false);

		return 1; // 1 means keep alive
	}

	static int _timerFdCallback(int fd, int events, void* data) {
		if (data == nullptr) {
			return 0;
		}

		auto* dispatcher = static_cast<RLNativeDispatcherAndroid*>(data);
		KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
		KD_RUNLOOP_CHECK_SAME_THREAD(dispatcher->m_runLoopThreadId);
		if (dispatcher->destroyed()) {
			return 0;
		}

		// Clear data in fd
		uint64_t expirations = 0;
		long len = ::read(fd, &expirations, sizeof(expirations));
		(void)len;

		// Process timer tasks
		dispatcher->processEvents(false);
		dispatcher->updateTimer();

		return 1;
	}

	static int _fdEventCallback(int fd, int events, void* data) {
		if (data == nullptr) {
			return 0;
		}

		auto* dispatcher = static_cast<RLNativeDispatcherAndroid*>(data);
		KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
		KD_RUNLOOP_CHECK_SAME_THREAD(dispatcher->m_runLoopThreadId);
		if (dispatcher->destroyed()) {
			return 0;
		}

		std::map<RLEvent, int> activatedEvents;
		if (events & ALOOPER_EVENT_INPUT) {
			activatedEvents[fd] |= RLEventNotifier::READ;
		}
		if (events & ALOOPER_EVENT_OUTPUT) {
			activatedEvents[fd] |= RLEventNotifier::WRITE;
		}

		if (!activatedEvents.empty()) {
			dispatcher->processEventNotifiers(activatedEvents);
		}

		return dispatcher->isEventNotifierActive(fd) ? 1 : 0;
	}
};

__NAMESPACE_KD_END
#endif // KD_OS_ANDROID