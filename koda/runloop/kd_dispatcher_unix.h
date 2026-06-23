/** *************************************************************************
Created by: Arlin (arlins.dps@gmail.com).
This file implements RLDispatcher on general Unix systems based on poll.
************************************************************************** */

#pragma once
#include <cstdint>
#include "koda/runloop/kd_runloopdefs.h"
#include "koda/runloop/kd_dispatcher_dummy.h"

#if defined(KD_OS_UNIX)
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

__NAMESPACE_KD_BEGIN

// =============================
// RLEventDispatcherUnix (Base on poll)
// =============================
class RLEventDispatcherUnix : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLEventDispatcherUnix);

private:
	int m_wakeUpPipeFds[2]{ -1, -1 }; // 0-read, 1-write

public:
	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		// Create wake-up pipe
		if (::pipe(m_wakeUpPipeFds) == -1) {
			m_wakeUpPipeFds[0] = -1;
			m_wakeUpPipeFds[1] = -1;
			KD_ASSERT(false);
			return;
		}

		// Init pipe
		for (int i = 0; i < 2; ++i) {
			int flags = ::fcntl(m_wakeUpPipeFds[i], F_GETFL, 0);
			if (flags != -1) {
				::fcntl(m_wakeUpPipeFds[i], F_SETFL, flags | O_NONBLOCK);
			}
			::fcntl(m_wakeUpPipeFds[i], F_SETFD, FD_CLOEXEC);
		}
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		if (m_wakeUpPipeFds[0] != -1) {
			::close(m_wakeUpPipeFds[0]);
			m_wakeUpPipeFds[0] = -1;
		}

		if (m_wakeUpPipeFds[1] != -1) {
			::close(m_wakeUpPipeFds[1]);
			m_wakeUpPipeFds[1] = -1;
		}
	}

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed()) {
			kd_runloop_debug("Can not wake up when dispatcher was destroyed");
			return;
		}

		// Write data to wake up poll
		int wakeUpFd = m_wakeUpPipeFds[1];
		if (wakeUpFd != -1) {
			char dummy = 'W';
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
	}

	void removeEventNotifier(RLEvent ev) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	}

	bool processEvents(bool canWait) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			kd_runloop_debug("Can not process events when dispatcher was destroyed");
			return false;
		}

		// Process tasks and timers
		processTasks();
		processTimerTasks();

		// Prepare all fds to poll
		std::vector<struct pollfd> pollFds;
		pollFds.reserve(m_ctx->m_eventNotifiers.size() + 1);

		// Place the wake-up pipe in position 0
		if (m_wakeUpPipeFds[0] != -1) {
			struct pollfd pfd;
			pfd.fd = m_wakeUpPipeFds[0];
			pfd.events = POLLIN;
			pfd.revents = 0;
			pollFds.push_back(pfd);
		}

		// Wrap the event notifiers
		for (const auto& pair : m_ctx->m_eventNotifiers) {
			const RLEventNotifier& notifier = pair.second;
			struct pollfd pfd;
			pfd.fd = notifier.ev;
			pfd.events = 0;
			pfd.revents = 0;

			if (notifier.type & RLEventNotifier::READ) {
				pfd.events |= (POLLIN | POLLPRI);
			}
			if (notifier.type & RLEventNotifier::WRITE) {
				pfd.events |= POLLOUT;
			}

			pollFds.push_back(pfd);
		}

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
		int triggeredCount = ::poll(pollFds.data(), static_cast<nfds_t>(pollFds.size()), timeoutMs);

		// Dispatch the events
		if (triggeredCount > 0) {
			std::map<RLEvent, int> activatedEvents;

			for (const auto& pfd : pollFds) {
				if (pfd.revents == 0) {
					continue;
				}

				if (pfd.fd == m_wakeUpPipeFds[0]) {
					if (pfd.revents & POLLIN) {
						// Clear buffers in pipe
						char buffer[64];
						ssize_t ret = -1;
						do  {
							ret = ::read(m_wakeUpPipeFds[0], buffer, sizeof(buffer));
						} while (ret > 0 || (ret == -1 && errno == EINTR));

						m_ctx->processPendingCommands();
					}
					continue; // Wake up
				}

				int rlEventType = 0;
				if (pfd.revents & (POLLIN | POLLPRI | POLLERR | POLLHUP)) {
					rlEventType |= RLEventNotifier::READ;
				}
				if (pfd.revents & POLLOUT) {
					rlEventType |= RLEventNotifier::WRITE;
				}

				if (rlEventType != 0) {
					activatedEvents[pfd.fd] = rlEventType;
				}
			}

			// Process event notifiers
			if (!activatedEvents.empty()) {
				processEventNotifiers(activatedEvents);
			}
		}

		return true;
	}
};

__NAMESPACE_KD_END
#endif // KD_OS_UNIX
