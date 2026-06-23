/** ****************************************************
Created by: Arlin (arlins.dps@gmail.com).
This file implements RLDispatcher for HarmonyOS
********************************************************/

#pragma once
#include <map>
#include <atomic>
#include "koda/runloop/kd_runloopdefs.h"
#include "koda/runloop/kd_dispatcher_dummy.h"

#if defined(KD_OS_OHOS)
#include <uv.h>
#endif

// =================================
// RLSharedUvLoopers
// =================================
#if defined(KD_OS_OHOS)
__NAMESPACE_KD_BEGIN

class RLSharedUvLoopers {
private:
	struct Loop {
		uv_loop_t* uv_loop{nullptr};
		int refs{ 0 };
	};

	using LoopsMap = std::map<std::thread::id, Loop>;
	static LoopsMap& _uvLoops() {
		static LoopsMap* inst = new LoopsMap;
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

	static RLMutex& _uvLoopsMutex() {
		static RLMutex* inst = new RLMutex;
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

public:
	static void retainLoop(std::thread::id tid, uv_loop_t* uv_loop) {
		if (uv_loop == nullptr) {
			return;
		}

		std::lock_guard<RLMutex> lock(_uvLoopsMutex());
		auto& loops = _uvLoops();
		auto it = loops.find(tid);
		if (it != loops.end()) {
			it->second.refs++;
		} else {
			loops[tid] = Loop{ uv_loop, 1 };
		}
	}

	static int releaseLoop(std::thread::id tid, uv_loop_t* uv_loop) {
		if (uv_loop == nullptr) {
			KD_ASSERT_M(false, "Release loop error: invalid loop");
			return -1;
		}

		std::lock_guard<RLMutex> lock(_uvLoopsMutex());
		auto& loops = _uvLoops();
		auto it = loops.find(tid);

		if (it != loops.end() && it->second.uv_loop == uv_loop) {
			int refs = (--(it->second.refs));
			KD_ASSERT(refs >= 0);
			if (refs <= 0) {
				refs = 0;
				loops.erase(it);
			}

			return refs;
		}

		KD_ASSERT_M(false, "Release loop error: invalid loop");
		return -1;
	}

	static uv_loop_t* getLoop(std::thread::id tid) {
		std::lock_guard<RLMutex> lock(_uvLoopsMutex());
		auto& loops = _uvLoops();
		auto it = loops.find(tid);

		if (it != loops.end()) {
			return it->second.uv_loop;
		}

		return nullptr;
	}
};

__NAMESPACE_KD_END
#endif // KD_OS_OHOS


// =================================
// RLNativeLooperHarmony (Base on uv_loop)
// =================================
#if defined(KD_OS_OHOS)
__NAMESPACE_KD_BEGIN

class RLNativeLooperHarmony : public RLLooper {
private:
	std::atomic<bool> m_exit{ false };
	std::atomic<int> m_exitCode{ 0 };

	uv_loop_t* m_uvLoop{ nullptr };
	uv_async_t* m_exitLoopAsync{ nullptr };

public:
	void construct(std::weak_ptr<RLContext> ctx) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		RLLooper::construct(ctx);

		int ret = -1;

		// Create or get uv_loop
		auto uvLoop = RLSharedUvLoopers::getLoop(m_runLoopThreadId);
		if (uvLoop != nullptr) {
			m_uvLoop = uvLoop;
			RLSharedUvLoopers::retainLoop(m_runLoopThreadId, uvLoop);
		} else {
			m_uvLoop = new uv_loop_t();
			ret = ::uv_loop_init(m_uvLoop);
			if (ret != 0) {
				KD_ASSERT_M(false, "Error: uv_loop_init failed");
				delete m_uvLoop;
				m_uvLoop = nullptr;
				return;
			}

			// Retain the loop
			RLSharedUvLoopers::retainLoop(m_runLoopThreadId, m_uvLoop);
		}

		// Create exit-looper uv_async
		m_exitLoopAsync = new uv_async_t();
		ret = ::uv_async_init(m_uvLoop, m_exitLoopAsync, [](uv_async_t* handle) {});
		if (ret != 0) {
			KD_ASSERT_M(false, "Error: uv_async_init failed");
			delete m_exitLoopAsync;
			m_exitLoopAsync = nullptr;
			return;
		}

		// Init uv_loop of dispatcher
		if (m_uvLoop) {
			auto ctxSp = ctx.lock();
			if (ctxSp) {
				ctxSp->getDispatcher()->initUvLoop(m_uvLoop);
			}
		}
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		// Close exit-looper uv_async
		if (m_exitLoopAsync) {
			::uv_close(reinterpret_cast<uv_handle_t*>(m_exitLoopAsync), [](uv_handle_t* handle) {
				delete reinterpret_cast<uv_async_t*>(handle);
			});
			m_exitLoopAsync = nullptr;
		}

		// Destroy uv_loop
		if (m_uvLoop) {
			// Release the loop
			auto refs = RLSharedUvLoopers::releaseLoop(m_runLoopThreadId, m_uvLoop);

			// Clear loop if refs = 0
			if (refs == 0) {
				// Uninit dispatcher uv_loop
				auto ctxSp = m_ctx.lock();
				if (ctxSp) {
					ctxSp->getDispatcher()->uninitUvLoop(m_uvLoop);
				}

				// Close uv_loop
				int try_close_times = 0;
				int ret = -1;

				ret = ::uv_loop_close(m_uvLoop);
				while (ret == UV_EBUSY) {
					// UV_EBUSY means some handles are still active inside the loop
					// Execute a non-blocking loop to let the loop clean up resources.
					::uv_run(m_uvLoop, UV_RUN_NOWAIT);
					ret = ::uv_loop_close(m_uvLoop);

					try_close_times++;
					if (try_close_times >= 1000) {
						// There are resources leak on uv_loop, so we will abandon 
						// deleting uv_loop to ensure no crash.
						KD_ASSERT_M(false, "Fatal: Dynamic leak detected");
						return; // Abandon deleting uv_loop
					}
				}

				// Delete uv_loop
				delete m_uvLoop;
			}
		}
		m_uvLoop = nullptr;

		// Destroy base class
		RLLooper::destroy();
	}

	int exec() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		auto ctx = m_ctx.lock();
		if (ctx == nullptr || ctx->getDispatcher()->destroyed()) {
			return -1;
		}
		if (m_uvLoop == nullptr || m_exitLoopAsync == nullptr) {
			return -1;
		}

		m_exit = false;
		m_exitCode = 0;

		// Exec the loop
		int ret = -1;
		while (!m_exit && !ctx->getDispatcher()->destroyed()) {
			ret = ::uv_run(m_uvLoop, UV_RUN_ONCE);
			if (ret == 0) {
				break;
			}
		}

		return m_exitCode.load();
	}

	void exit(int exitCode) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_exitCode = exitCode;
		m_exit = true;

		if (m_uvLoop && m_exitLoopAsync) {
			::uv_async_send(m_exitLoopAsync);
		}
	}
};

__NAMESPACE_KD_END
#endif // KD_OS_OHOS


// =================================
// RLNativeDispatcherHarmony (Base on uv_loop)
// =================================
#if defined(KD_OS_OHOS)
__NAMESPACE_KD_BEGIN

class RLNativeDispatcherHarmony : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLNativeDispatcherHarmony);

private:
	uv_loop_t* m_uv_loop{ nullptr };
	uv_timer_t* m_timerHandle{ nullptr };
	uv_async_t m_wakeUpAsyncHandle{ nullptr };
	bool m_wakeUpAsyncHandleInited{ false };

	std::map<uv_poll_t* , RLEvent> m_pollFdlMap;
	int64_t m_lastAbsoluteFireTimeMs{ -1 };

public:
	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	};

	void initUvLoop(uv_loop_t* uv_loop) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (uv_loop == nullptr || m_uv_loop != nullptr) {
			return;
		}

		m_uv_loop = uv_loop;

		// Init async handle for wakeUp
		m_wakeUpAsyncHandle.data = this;
		int ret = ::uv_async_init(m_uv_loop, &m_wakeUpAsyncHandle, [](uv_async_t* handle) {
			auto* dispatcher = static_cast<RLNativeDispatcherHarmony*>(handle->data);
			KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
			if (dispatcher && !dispatcher->destroyed()) {
				dispatcher->processEvents(false);
			}
		});
		m_wakeUpAsyncHandleInited = (ret == 0);
		KD_ASSERT(ret == 0);

		// Init timer handle
		m_timerHandle = new uv_timer_t();
		m_timerHandle->data = this;
		ret = ::uv_timer_init(m_uv_loop, m_timerHandle);
		KD_ASSERT(ret == 0);

		// Initial flush
		processEvents(false);
		updateTimer();
	}

	void uninitUvLoop(uv_loop_t* uv_loop) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		KD_ASSERT(uv_loop == m_uv_loop);
		if (m_uv_loop == nullptr) {
			return;
		}

		// Close async handle
		if (m_wakeUpAsyncHandleInited) {
			::uv_close(reinterpret_cast<uv_handle_t*>(&m_wakeUpAsyncHandle), [](uv_handle_t* handle) {});
			m_wakeUpAsyncHandleInited = false;
		}

		// Close timer handle
		if (m_timerHandle) {
			::uv_timer_stop(m_timerHandle);
			::uv_close(reinterpret_cast<uv_handle_t*>(m_timerHandle), [](uv_handle_t* handle) {
				delete reinterpret_cast<uv_timer_t*>(handle);
			});
			m_timerHandle = nullptr;
		}

		// Close poll handles
		for (auto& pair : m_pollFdlMap) {
			uv_poll_t* pollHandle = pair.first;
			pollHandle->data = nullptr;

			::uv_poll_stop(pollHandle);
			::uv_close(reinterpret_cast<uv_handle_t*>(pollHandle), [](uv_handle_t* handle) {
				delete reinterpret_cast<uv_poll_t*>(handle);
			});
		}
		m_pollFdlMap.clear();

		m_uv_loop = nullptr;
		m_lastAbsoluteFireTimeMs = -1;
	}

	void addEventNotifier(const RLEventNotifier& eventNotifier) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_uv_loop == nullptr) {
			return;
		}

		RLEvent nativeFd = eventNotifier.ev;

		// Setup types
		int uvEvents = 0;
		if (eventNotifier.type & RLEventNotifier::READ) {
			uvEvents |= UV_READABLE;
		}
		if (eventNotifier.type & RLEventNotifier::WRITE) {
			uvEvents |= UV_WRITABLE;
		}

		if (uvEvents == 0) {
			return;
		}

		// Create poll handle
		uv_poll_t* pollHandle = new uv_poll_t();
		int ret = ::uv_poll_init(m_uv_loop, pollHandle, nativeFd);
		if (ret != 0) {
			delete pollHandle;
			return;
		}
		pollHandle->data = this; // Init data

		// Start poll
		ret = ::uv_poll_start(pollHandle, uvEvents, [](uv_poll_t* handle, int status, int events) {
			if (handle->data == nullptr || status < 0) {
				return;
			}

			RLNativeDispatcherHarmony* dispatcher = (RLNativeDispatcherHarmony*)(handle->data);
			KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
			if (dispatcher->destroyed()) {
				return;
			}

			// Check if handle exists
			auto it = dispatcher->m_pollFdlMap.find(handle);
			if (it == dispatcher->m_pollFdlMap.end()) {
				return;
			}

			// Process event notifiers
			RLEvent nativeFd = it->second;
			std::map<RLEvent, int> activatedEvents;
			if (events & UV_READABLE) {
				activatedEvents[nativeFd] |= RLEventNotifier::READ;
			}
			if (events & UV_WRITABLE) {
				activatedEvents[nativeFd] |= RLEventNotifier::WRITE;
			}

			if (!activatedEvents.empty()) {
				dispatcher->processEventNotifiers(activatedEvents);
			}
		});

		if (ret == 0) {
			m_pollFdlMap[pollHandle] = nativeFd;
		} else {
			pollHandle->data = nullptr;
			delete pollHandle;
		}
	}

	void removeEventNotifier(RLEvent ev) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_uv_loop == nullptr) {
			return;
		}

		for (auto it = m_pollFdlMap.begin(); it != m_pollFdlMap.end(); it++) {
			if (it->second == ev) {
				uv_poll_t* pollHandle = it->first;
				pollHandle->data = nullptr;

				::uv_poll_stop(pollHandle);
				::uv_close(reinterpret_cast<uv_handle_t*>(pollHandle), [](uv_handle_t* handle) {
					delete reinterpret_cast<uv_poll_t*>(handle);
				});

				m_pollFdlMap.erase(it);
				break;
			}
		}
		
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_uv_loop == nullptr || m_timerHandle == nullptr) {
			return;
		}

		int64_t nextAbsoluteFireTimeMs = getNextAbsoluteFireTimeMs();
		if (nextAbsoluteFireTimeMs == m_lastAbsoluteFireTimeMs) {
			return;
		}
		m_lastAbsoluteFireTimeMs = nextAbsoluteFireTimeMs;

		::uv_timer_stop(m_timerHandle);

		if (nextAbsoluteFireTimeMs < 0) {
			return;
		}

		int64_t delayMs = nextAbsoluteFireTimeMs - now_time();
		delayMs = delayMs > 0 ? delayMs : 0;

		// Reset timer
		::uv_timer_start(m_timerHandle, [](uv_timer_t* handle) {
			auto* dispatcher = static_cast<RLNativeDispatcherHarmony*>(handle->data);
			KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
			if (dispatcher && !dispatcher->destroyed()) {
				dispatcher->processEvents(false);
				dispatcher->updateTimer();
			}
		}, static_cast<uint64_t>(delayMs), 0);
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		return std::shared_ptr<RLLooper>(new RLNativeLooperHarmony);
	};

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed() || !m_wakeUpAsyncHandleInited) {
			return;
		}

		::uv_async_send(&m_wakeUpAsyncHandle);
	}

	bool processEvents(bool canWait) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		KD_ASSERT_M(!canWait, "canWait must be false in native mode");
		if (destroyed()) {
			return false;
		}

		// Trigger events
		processTasks();
		processTimerTasks();

		return true;
	}
};

__NAMESPACE_KD_END
#endif // KD_OS_OHOS