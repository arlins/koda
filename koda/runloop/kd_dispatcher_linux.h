/** **************************************************************
Created by: Arlin (arlins.dps@gmail.com).
This file implements RLDispatcher on Linux base on epoll
******************************************************************/

#pragma once
#include <cstdint>
#include "koda/runloop/kd_runloopdefs.h"
#include "koda/runloop/kd_dispatcher_dummy.h"

#if defined(KD_OS_LINUX)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

__NAMESPACE_KD_BEGIN

// =================================
// RLEventDispatcherLinux (Base on epoll)
// =================================
class RLEventDispatcherLinux : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLEventDispatcherLinux);

private:
	int m_epollFd{ -1 };

	// We use the efficient eventfd as the wake-up source. 
	// `eventfd` is essentially a counter, internally containing 
	// only an 8-byte unsigned integer.
	int m_wakeUpEventFd{ -1 }; 

public:
	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		// Create epoll fd
		m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
		KD_ASSERT(m_epollFd != -1);

		// Create wake-up eventfd
		m_wakeUpEventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (m_wakeUpEventFd == -1) {
			::close(m_epollFd);
			m_epollFd = -1;
			KD_ASSERT(false);
			return;
		}

		// Add wake-up  eventfd into epoll
		// We set the wake-up eventfd to edge-triggered, which 
		// means it will only be activated when the eventfd's state changes 
		// (from empty to having data, etc.). If we continuously write to the 
		// eventfd but do not read from it, it will only be activated once.
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLET; // Edge-triggered
		ev.data.fd = m_wakeUpEventFd;

		if (::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_wakeUpEventFd, &ev) == -1) {
			::close(m_wakeUpEventFd);
			::close(m_epollFd);
			m_wakeUpEventFd = -1;
			m_epollFd = -1;
			KD_ASSERT(false);
			return;
		}
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		if (m_epollFd != -1) {
			::close(m_epollFd);
			m_epollFd = -1;
		}

		if (m_wakeUpEventFd != -1) {
			::close(m_wakeUpEventFd);
			m_wakeUpEventFd = -1;
		}
	}

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed()) {
			kd_runloop_debug("Can not wake up when dispatcher was destroyed");
			return;
		}
		
		// Write data to wake up epoll_wait
		int wakeUpFd = m_wakeUpEventFd;
		if (wakeUpFd != -1) {
			uint64_t dummy = 1;
			::write(wakeUpFd, &dummy, sizeof(dummy));
		}
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		wakeUp(); // Wake up to re-enter the blocked wait using the earliest time
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		return RLCommonEventLooper::creatShared();
	}

	void addEventNotifier(const RLEventNotifier& eventNotifier) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_epollFd == -1) {
			return;
		}

		int fd = eventNotifier.ev;
		struct epoll_event ev;
		ev.events = EPOLLET | EPOLLRDHUP; // Edge-triggered with peer half-close defense
		ev.data.fd = fd;

		if (eventNotifier.type & RLEventNotifier::READ) {
			ev.events |= (EPOLLIN | EPOLLRDHUP);
		}
		if (eventNotifier.type & RLEventNotifier::WRITE) {
			ev.events |= EPOLLOUT;
		}

		// Add the file descriptor (fd) to the kernel red-black tree. 
		// If it already exists in the kernel, then use MOD.
		int op = EPOLL_CTL_ADD;
		if (::epoll_ctl(m_epollFd, op, fd, &ev) == -1) {
			if (errno == EEXIST) {
				op = EPOLL_CTL_MOD;
				::epoll_ctl(m_epollFd, op, fd, &ev);
			}
		}
	}

	void removeEventNotifier(RLEvent ev) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_epollFd == -1) {
			return;
		}

		int fd = ev;
		::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
	}

	bool processEvents(bool canWait) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			kd_runloop_debug("Can not process events when dispatcher was destroyed");
			return false;
		}
		if (m_epollFd == -1) {
			return false;
		}

		// Process tasks and timers
		processTasks();
		processTimerTasks();

		// Calculate waiting time
		int timeoutMs = 0;
		if (canWait) {
			int64_t nextAbsoluteFireTimeMs = m_ctx->getDispatcher()->getNextAbsoluteFireTimeMs();
			if (nextAbsoluteFireTimeMs < 0) {
				timeoutMs = -1; // Wait forever
			} else {
				int64_t nextTimeoutMs = nextAbsoluteFireTimeMs - ::kd::now_time();
				timeoutMs = (nextTimeoutMs > 0 ? static_cast<int>(nextTimeoutMs) : 0);
			}
		}

		// Wait for events
		static constexpr int MAX_ACTIVE_EVENTS = 256;
		struct epoll_event activeEvents[MAX_ACTIVE_EVENTS];
		int triggeredCount = ::epoll_wait(m_epollFd, activeEvents, MAX_ACTIVE_EVENTS, timeoutMs);

		// Dispatch the events
		if (triggeredCount > 0) {
			std::map<RLEvent, int> activatedEventsMap;

			for (int i = 0; i < triggeredCount; ++i) {
				const auto& event = activeEvents[i];
				int fd = event.data.fd;

				if (fd == m_wakeUpEventFd) {
					uint64_t counter = 0;
					::read(m_wakeUpEventFd, &counter, sizeof(counter));
					m_ctx->processPendingCommands();
					continue; // Wake up
				}

				if ((event.events & EPOLLIN) || (event.events & EPOLLPRI) || (event.events & EPOLLRDHUP)) {
					activatedEventsMap[fd] |= RLEventNotifier::READ;
				}
				if (event.events & EPOLLOUT) {
					activatedEventsMap[fd] |= RLEventNotifier::WRITE;
				}
			}

			// Process event notifiers
			if (!activatedEventsMap.empty()) {
				processEventNotifiers(activatedEventsMap);
			}
		}

		return true;
	}
};

__NAMESPACE_KD_END
#endif // KD_OS_LINUX