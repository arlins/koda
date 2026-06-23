/** *******************************************************
Created by: Arlin (arlins.dps@gmail.com).
This file implements RLDispatcher on Darwin(iOS/macOS)
RLNativeDispatcherDarwin base on CFRunLoop
RLEventDispatcherDarwin base on kqueue
***********************************************************/

#pragma once
#include <cstdint>
#include "koda/runloop/kd_runloopdefs.h"
#include "koda/runloop/kd_dispatcher_dummy.h"

#if defined(KD_OS_DARWIN)
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <CoreFoundation/CoreFoundation.h> // CFRunLoop
#endif // KD_OS_DARWIN


// =================================
// RLNativeDispatcherDarwin (Base on CFRunLoop)
// =================================
#if defined(KD_OS_DARWIN)
__NAMESPACE_KD_BEGIN

// RLNativeLooperDarwin
class RLNativeLooperDarwin : public RLLooper {
private:
	std::atomic<bool> m_exit{ false };
	std::atomic<int> m_exitCode{ 0 };
	CFRunLoopRef m_runLoopRef{ nullptr };

public:
	void construct(std::weak_ptr<RLContext> ctx) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
        RLLooper::construct(ctx);

		m_runLoopRef = ::CFRunLoopGetCurrent();
		if (m_runLoopRef) {
			::CFRetain(m_runLoopRef);
		}
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_runLoopRef) {
			::CFRelease(m_runLoopRef);
			m_runLoopRef = nullptr;
		}

        RLLooper::destroy();
	}

	int exec() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		auto ctx = m_ctx.lock();
		if (ctx == nullptr || ctx->getDispatcher()->destroyed()) {
			return -1;
		}

		m_exit = false;
		m_exitCode = 0;

		
		// Exec the loop
		SInt32 result;
		while (!m_exit && !ctx->getDispatcher()->destroyed()) {
			result = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0e10, true);
			if (result == kCFRunLoopRunStopped || result == kCFRunLoopRunFinished) {
				break;
			}
		}

		return m_exitCode.load();
	}

	void exit(int exitCode) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_exitCode = exitCode;
		m_exit = true;

		if (m_runLoopRef) {
			::CFRunLoopStop(m_runLoopRef);
		}
	}
};

// RLNativeDispatcherDarwin
class RLNativeDispatcherDarwin : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLNativeDispatcherDarwin);
	static constexpr CFTimeInterval kFarFutureDelaySec = 3153600000.0;

private:
	struct FdRecord {
		CFFileDescriptorRef fdRef{ nullptr };
		CFRunLoopSourceRef sourceRef{ nullptr };
		CFOptionFlags registeredTypes{ 0 };
	};

	CFRunLoopRef m_runLoopRef{ nullptr };
	CFRunLoopSourceRef m_wakeUpSource{ nullptr };
	std::map<RLEvent, FdRecord> m_fdWatchMap;

	CFRunLoopTimerRef m_timerRef{ nullptr };
	int64_t m_lastAbsoluteFireTimeMs{ -1 };

public:
	~RLNativeDispatcherDarwin() {
		KD_RUNLOOP_CHECK_ANY_THREADS();

		// Release timer
		if (m_timerRef != nullptr) {
			::CFRelease(m_timerRef);
			m_timerRef = nullptr;
		}

		// Release wake-up source
		if (m_wakeUpSource != nullptr) {
			::CFRelease(m_wakeUpSource);
			m_wakeUpSource = nullptr;
		}

		// Release run loop
		if (m_runLoopRef != nullptr) {
			::CFRelease(m_runLoopRef);
			m_runLoopRef = nullptr;
		}
	}

	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		// Get current run loop
		m_runLoopRef = ::CFRunLoopGetCurrent();
		if (m_runLoopRef == nullptr) {
			return;
		}
		::CFRetain(m_runLoopRef);

		// Create wake-up source
		CFRunLoopSourceContext sourceCtx;
		::memset(&sourceCtx, 0, sizeof(sourceCtx));
		sourceCtx.info = this;
		sourceCtx.perform = [](void* info) {
			if (info) {
				auto* dispatcher = static_cast<RLNativeDispatcherDarwin*>(info);
				KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
				KD_RUNLOOP_CHECK_SAME_THREAD(dispatcher->m_runLoopThreadId);
				dispatcher->processEvents(false);
			}
		};

		m_wakeUpSource = ::CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &sourceCtx);
		KD_ASSERT(m_wakeUpSource != nullptr);
		::CFRunLoopAddSource(m_runLoopRef, m_wakeUpSource, kCFRunLoopCommonModes);
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		// Clear all watched fds
		for (auto& pair : m_fdWatchMap) {
			const FdRecord& record = pair.second;
			if (record.sourceRef && m_runLoopRef) {
				::CFRunLoopRemoveSource(m_runLoopRef, record.sourceRef, kCFRunLoopCommonModes);
			}
			if (record.fdRef) {
				::CFFileDescriptorInvalidate(record.fdRef);
				::CFRelease(record.fdRef);
			}
			if (record.sourceRef) {
				::CFRelease(record.sourceRef);
			}
		}
		m_fdWatchMap.clear();

		// Remove timer, it will be released in destructor
		if (m_timerRef != nullptr) {
			if (m_runLoopRef != nullptr) {
				::CFRunLoopRemoveTimer(m_runLoopRef, m_timerRef, kCFRunLoopCommonModes);
			}
			::CFRunLoopTimerInvalidate(m_timerRef);
		}
		m_lastAbsoluteFireTimeMs = -1;

		// Remove wake-up source, it will be released in destructor
		if (m_wakeUpSource != nullptr) {
			if (m_runLoopRef != nullptr) {
				::CFRunLoopRemoveSource(m_runLoopRef, m_wakeUpSource, kCFRunLoopCommonModes);
			}
		}
	}

	void addEventNotifier(const RLEventNotifier& eventNotifier) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_runLoopRef == nullptr) {
			return;
		}

		RLEvent nativeFd = eventNotifier.ev;
		
		// Setup types
		CFOptionFlags cbTypes = 0;
		if (eventNotifier.type & RLEventNotifier::READ) {
			cbTypes |= kCFFileDescriptorReadCallBack;
		}
		if (eventNotifier.type & RLEventNotifier::WRITE) {
			cbTypes |= kCFFileDescriptorWriteCallBack;
		}
		
		// Create CFFileDescriptorRef
		CFFileDescriptorContext fdCtx;
		::memset(&fdCtx, 0, sizeof(fdCtx));
		fdCtx.info = this;

		CFFileDescriptorRef cfFd = ::CFFileDescriptorCreate( kCFAllocatorDefault, nativeFd, false, 
			&RLNativeDispatcherDarwin::_fdSourceActiveCallback, &fdCtx );
		if (cfFd == nullptr) {
			KD_ASSERT(false);
			return;
		}

		// Enable callbacks
		::CFFileDescriptorEnableCallBacks(cfFd, cbTypes);

		// Generate Source for fd and add to run loop
		CFRunLoopSourceRef fdSource = ::CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, cfFd, 0);
		if (fdSource == nullptr) {
			::CFFileDescriptorInvalidate(cfFd);
			::CFRelease(cfFd);
			KD_ASSERT(false);
			return;
		}
		::CFRunLoopAddSource(m_runLoopRef, fdSource, kCFRunLoopCommonModes);

		// Save record to map
		FdRecord record;
		record.fdRef = cfFd;
		record.sourceRef = fdSource;
		record.registeredTypes = cbTypes;
		m_fdWatchMap[nativeFd] = record;
	}

	void removeEventNotifier(RLEvent handle) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_runLoopRef == nullptr) {
			return;
		}

		auto it = m_fdWatchMap.find(handle);
		if (it != m_fdWatchMap.end()) {
			const FdRecord& record = it->second;

			if (record.sourceRef) {
				::CFRunLoopRemoveSource(m_runLoopRef, record.sourceRef, kCFRunLoopCommonModes);
			}
			if (record.fdRef) {
				::CFFileDescriptorInvalidate(record.fdRef);
				::CFRelease(record.fdRef);
			}
			if (record.sourceRef) {
				::CFRelease(record.sourceRef);
			}

			m_fdWatchMap.erase(it);
		}
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_runLoopRef == nullptr) {
			return;
		}

		int64_t nextAbsoluteFireTimeMs = getNextAbsoluteFireTimeMs();

		// Filtering the same timer
		if (nextAbsoluteFireTimeMs == m_lastAbsoluteFireTimeMs) {
			return;
		}
		m_lastAbsoluteFireTimeMs = nextAbsoluteFireTimeMs;

		// Update timer fire date to forever
		if (nextAbsoluteFireTimeMs < 0) {
			if (m_timerRef != nullptr) {
				::CFRunLoopTimerSetNextFireDate(m_timerRef, ::CFAbsoluteTimeGetCurrent() + kFarFutureDelaySec);
			}
			return;
		}

		// Calculate fire date
		int64_t delayMs = nextAbsoluteFireTimeMs - now_time();
		CFTimeInterval delaySec = (delayMs > 0 ? delayMs : 0) / 1000.0;
		CFAbsoluteTime fireDate = ::CFAbsoluteTimeGetCurrent() + delaySec;

		// Create or Update timer
		if (m_timerRef == nullptr) {
			CFRunLoopTimerContext timerCtx = { 0, this, nullptr, nullptr, nullptr };
			m_timerRef = ::CFRunLoopTimerCreate( kCFAllocatorDefault, fireDate, kFarFutureDelaySec, 0, 0,
				[](CFRunLoopTimerRef timer, void* info) {
					if (info) {
						auto* dispatcher = static_cast<RLNativeDispatcherDarwin*>(info);
						KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
						KD_RUNLOOP_CHECK_SAME_THREAD(dispatcher->m_runLoopThreadId);
						dispatcher->m_lastAbsoluteFireTimeMs = -1;
						dispatcher->processEvents(false);
						dispatcher->updateTimer();
					}
				}, &timerCtx);

			if (m_timerRef) {
				::CFRunLoopAddTimer(m_runLoopRef, m_timerRef, kCFRunLoopCommonModes);
			}
		} else {
			::CFRunLoopTimerSetNextFireDate(m_timerRef, fireDate);
		}
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		return std::shared_ptr<RLLooper>(new RLNativeLooperDarwin);
	}

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed() || m_wakeUpSource == nullptr || m_runLoopRef == nullptr) {
			return;
		}

		// When wakeUp is called, dispatcher will still holds the reference of 
		// m_wakeUpSource and  m_runLoopRef, so using wakeUp in a 
		// multi-threaded environment is safe.
		::CFRunLoopSourceSignal(m_wakeUpSource);
		::CFRunLoopWakeUp(m_runLoopRef);
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
	static void _fdSourceActiveCallback(CFFileDescriptorRef f, CFOptionFlags callBackTypes, void* info) {
		if (info == nullptr) {
			return;
		}

		auto* dispatcher = static_cast<RLNativeDispatcherDarwin*>(info);
		KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
		KD_RUNLOOP_CHECK_SAME_THREAD(dispatcher->m_runLoopThreadId);

		RLEvent fd = ::CFFileDescriptorGetNativeDescriptor(f);
		std::map<RLEvent, int> activatedEvents;

		if (callBackTypes & kCFFileDescriptorReadCallBack) {
			activatedEvents[fd] |= RLEventNotifier::READ;
		}
		if (callBackTypes & kCFFileDescriptorWriteCallBack) {
			activatedEvents[fd] |= RLEventNotifier::WRITE;
		}

		if (!activatedEvents.empty()) {
			dispatcher->processEventNotifiers(activatedEvents);
		}

		//
		// Re-enable CFFileDescriptor
		//
		// CFFileDescriptor is based on kevent (kqueue) at the underlying level. 
		// Its notification pattern is semantically closer to edge-triggered/one-shot. 
		// When a read/write event occurs on the underlying file descriptor (Fd), 
		// CFRunLoop triggers a callback and automatically disables event listening 
		// for that Fd at the underlying level. Therefore, we need to re-call 
		// CFFileDescriptorEnableCallBacks within the callback to re-enable it.
		//
		// If the data is not completely read outside of processEventNotifiers, 
		// and CFFileDescriptorEnableCallBacks is called to attempt to re-enable 
		// listening, the kernel will refuse to reactivate it, causing the Fd 
		// to be unable to continue listening.
		//
		if (dispatcher->isEventNotifierActive(fd)) {
			::CFFileDescriptorEnableCallBacks(f, callBackTypes);
		}
	};
};

__NAMESPACE_KD_END
#endif // KD_OS_DARWIN


// ===============================
// RLEventDispatcherDarwin (Base on kqueue)
// ===============================
#if defined(KD_OS_DARWIN)
__NAMESPACE_KD_BEGIN

class RLEventDispatcherDarwin : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLEventDispatcherDarwin);

private:
	static constexpr uintptr_t RL_WAKEUP_FD = 0x120;
	int m_kqFd{ -1 };

public:
	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		// Create kqueue
		m_kqFd = ::kqueue();
		KD_ASSERT(m_kqFd != -1);
		::fcntl(m_kqFd, F_SETFD, FD_CLOEXEC);

		// Register EVFILT_USER for wake-up
		struct kevent kev;
		EV_SET(&kev, RL_WAKEUP_FD, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
		int res = ::kevent(m_kqFd, &kev, 1, nullptr, 0, nullptr);
		KD_ASSERT(res != -1);
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_kqFd != -1) {
			::close(m_kqFd);
			m_kqFd = -1;
		}
	}

	void addEventNotifier(const RLEventNotifier& eventNotifier) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_kqFd == -1) {
			return;
		}

		int fd = (int)eventNotifier.ev;
		struct kevent kevs[2];
		int kevCount = 0;

		// Set up events
		if (eventNotifier.type & RLEventNotifier::READ) {
			EV_SET(&kevs[kevCount++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 
				reinterpret_cast<void*>(static_cast<intptr_t>(eventNotifier.ev)));
		}
		if (eventNotifier.type & RLEventNotifier::WRITE) {
			EV_SET(&kevs[kevCount++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, 
				reinterpret_cast<void*>(static_cast<intptr_t>(eventNotifier.ev)));
		}

		// Submit events to kqueue
		if (kevCount > 0) {
			::kevent(m_kqFd, kevs, kevCount, nullptr, 0, nullptr);
		}
	}

	void removeEventNotifier(RLEvent ev) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_kqFd == -1) {
			return;
		}

		int fd = (int)(intptr_t)ev;
		struct kevent kevs[2];
		EV_SET(&kevs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
		EV_SET(&kevs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

		::kevent(m_kqFd, kevs, 2, nullptr, 0, nullptr);
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	}

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed()) {
			kd_runloop_debug("Can not wake up when dispatcher was destroyed");
			return;
		}

		int kqFd = m_kqFd;
		if (kqFd != -1) {
			struct kevent kev;
			EV_SET(&kev, RL_WAKEUP_FD, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
			::kevent(kqFd, &kev, 1, nullptr, 0, nullptr);
		}
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		return RLCommonEventLooper::creatShared();
	}

	bool processEvents(bool canWait) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			kd_runloop_debug("Can not process events when dispatcher was destroyed");
			return false;
		}
		if (m_kqFd == -1) {
			return false;
		}

		// Process tasks and timers
		processTasks();
		processTimerTasks();

		// Calculate waiting time
		struct timespec timeout;
		struct timespec* pTimeout = &timeout;

		if (canWait) {
			auto nextAbsoluteFireTimeMs = m_ctx->getDispatcher()->getNextAbsoluteFireTimeMs();
			if (nextAbsoluteFireTimeMs < 0) {
				pTimeout = nullptr; // nullptr represents indefinite blocking.
			} else {
				int64_t nextTimeoutMs = nextAbsoluteFireTimeMs - now_time();
				nextTimeoutMs = (nextTimeoutMs > 0 ? nextTimeoutMs : 0);
				pTimeout->tv_sec = nextTimeoutMs / 1000;
				pTimeout->tv_nsec = (nextTimeoutMs % 1000) * 1000000;
			}
		} else {
			// Non-blocking polling with 0 waiting time
			pTimeout->tv_sec = 0;
			pTimeout->tv_nsec = 0;
		}

		// Wait for event notifiers, active events are limited to 256
		static constexpr int MAX_ACTIVE_EVENTS = 256;
		struct kevent activeEvents[MAX_ACTIVE_EVENTS];
		int triggeredCount = ::kevent(m_kqFd, nullptr, 0, activeEvents, MAX_ACTIVE_EVENTS, pTimeout);

		// Dispatch event notifiers
		if (triggeredCount > 0) {
			std::map<RLEvent, int> activatedEventsMap;

			for (int i = 0; i < triggeredCount; ++i) {
				const auto& event = activeEvents[i];
				if (event.udata == nullptr) {
					continue;
				}
				if (event.filter == EVFILT_USER) {
					m_ctx->processPendingCommands();
					continue; // Wake up
				}

				RLEvent ev = static_cast<RLEvent>(reinterpret_cast<intptr_t>(event.udata));
				if (event.filter == EVFILT_READ) {
					activatedEventsMap[ev] |= RLEventNotifier::READ;
				} else if (event.filter == EVFILT_WRITE) {
					activatedEventsMap[ev] |= RLEventNotifier::WRITE;
				}
			}

			// Process activated event notifiers
			if (!activatedEventsMap.empty()) {
				processEventNotifiers(activatedEventsMap);
			}
		}

		return true;
	}
};

__NAMESPACE_KD_END
#endif // KD_OS_DARWIN
