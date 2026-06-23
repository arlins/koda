/** ********************************************************************************
  Created by: Arlin (arlins.dps@gmail.com).

  RunLoop
  RunLoop is a High-Performance, Unified, Cross-Platform Event Loop Architecture.
  RunLoop serves as a core infrastructure providing a standardized API abstraction
  layer across multiple operating systems. It enables seamless integration with both
  native system message pumps and custom autonomous event-driven environments,
  ensuring that all modes share a unified execution context (RLContext).
 
 Core Capabilities:
  - Post Tasks: Supports asynchronous execution of single-shot operations/closures.
  - Timers: High-precision monotonic timers with built-in drift-free repetition.
  - External Events: Native/OS-level handle multiplexing and signaling callbacks.

 Operational Modes:
  1. Native Mode (Parasitic / Integration Model):
  Intercepts and grafts directly onto the OS-specific native event infrastructure,
  co-existing with existing GUI or system message pumps without breaking them.

  Supporting:
  - Windows: Integration via Windows Message Loop.
  - Darwin (iOS/macOS...): Integration via CFRunLoop.
  - GLib (Linux Desktop): Integration via GMainContext.
  - Android: Integration via ALooper.
  - HarmonyOS: Integration via uv_loop_t.
  
  2. Event Mode (Autonomous / Engine Model):
  Operates as a self-contained, lightweight, react-style I/O multiplexing engine,
  highly optimized for background workers or pure non-UI environments (similar to libuv):

  Supporting:
  - Windows: Multiplexing via WaitForMultipleObjectsEx.
  - Darwin (iOS/macOS): Multiplexing via kqueue.
  - Linux: Multiplexing via epoll.
  - Unix / POSIX: Multiplexing via poll.
 ********************************************************************************** */

#pragma once
#include "koda/runloop/kd_runloopdefs.h"

#include "koda/runloop/kd_dispatcher_win32.h"
#include "koda/runloop/kd_dispatcher_darwin.h"
#include "koda/runloop/kd_dispatcher_glib.h"
#include "koda/runloop/kd_dispatcher_unix.h"
#include "koda/runloop/kd_dispatcher_linux.h"
#include "koda/runloop/kd_dispatcher_android.h"
#include "koda/runloop/kd_dispatcher_harmony.h"
#include "koda/runloop/kd_dispatcher_dummy.h"

__NAMESPACE_KD_BEGIN
// =================================
// RunLoop
// =================================
class RunLoop {
private:
	using ThreadRunLoopsMap = std::map<std::thread::id, std::shared_ptr<RLContext>>;
	static ThreadRunLoopsMap& _threadRunLoops() {
		static ThreadRunLoopsMap* inst = new ThreadRunLoopsMap;
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

	static RLMutex& _runLoopsMutex() {
		static RLMutex* inst = new RLMutex;
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

	// Remove expired or thread's RLContext automatically by RAII
	struct RLAutoCleaner {
		~RLAutoCleaner() {
			std::lock_guard<RLMutex> lock(_runLoopsMutex());
			auto tid = currentThreadId();
			auto& runloops = _threadRunLoops();
			ThreadRunLoopsMap::iterator it;

			// Remove ctx of current thread
			it = runloops.find(tid);
			if (it != runloops.end()) {
				auto ctx = it->second;
				KD_RUNLOOP_CHECK_SAME_THREAD(ctx->m_runLoopThreadId);

				// Destroy the ctx
				ctx->m_expired = true;
				ctx->getDispatcher()->uninit(); // Uninitialize dispatcher
				ctx->destroy();
				
				// Erase the ctx
				runloops.erase(it);
				kd_runloop_debug("Remove RLContext automatically when thread <%s> exits", kd::thread_id_str(tid).c_str());
			}
		}
	};

	static void _createDispatcher(std::shared_ptr<RLContext> ctx,  RLMode mode) {
		std::shared_ptr<RLDispatcher> dispatcher = nullptr;

		if (mode == RLMode::Event) {
			// Event (4): Windows/Darwin/Linux/Unix
#if defined(KD_OS_WIN)
			dispatcher = ::kd::make_shared<RLEventDispatcherWin32>();
#elif defined(KD_OS_DARWIN)
			dispatcher = ::kd::make_shared<RLEventDispatcherDarwin>();
#elif defined(KD_OS_LINUX)
			dispatcher = ::kd::make_shared<RLEventDispatcherLinux>();
#elif defined(KD_OS_UNIX)
			dispatcher = ::kd::make_shared<RLEventDispatcherUnix>();
#endif

		} else if (mode == RLMode::Native) {
			// Native (5): Windows/Darwin/Android/HarmonyOS/Glib
#if defined(KD_OS_WIN)
			dispatcher = ::kd::make_shared<RLNativeDispatcherWin32>();
#elif defined(KD_OS_DARWIN_NATIVE)
			dispatcher = ::kd::make_shared<RLNativeDispatcherDarwin>();
#elif defined(KD_OS_ANDROID)
			dispatcher = ::kd::make_shared<RLNativeDispatcherAndroid>();
#elif defined(KD_OS_OHOS)
			dispatcher = ::kd::make_shared<RLNativeDispatcherHarmony>();
#elif defined(KD_HAS_GLIB)
			dispatcher = ::kd::make_shared<RLNativeDispatcherGLib>();
#endif

		} else {
			KD_ASSERT_M(false, "Invaild mode");
		}

		if (dispatcher == nullptr) {
			KD_ASSERT_M(false, "RunLoop is unsupported on this OS");
			dispatcher = ::kd::make_shared<RLDispatcherDummy>();
		}

		ctx->m_dispatcher = dispatcher;
		dispatcher->init(ctx);
	}

	// Get the context of tid
	// If tid is current thread and context does not exists, 
	// it will create context for current thread, otherwise return null
	static std::shared_ptr<RLContext> _getContext(std::thread::id tid) {
		std::lock_guard<RLMutex> lock(_runLoopsMutex());
		auto& runloops = _threadRunLoops();
		auto it = runloops.find(tid);

		if (it != runloops.end()) {
			return it->second;
		}

		// Only create ctx on current thread
		if (tid == std::this_thread::get_id()) {
			static thread_local RLAutoCleaner ctxAutoCleaner; // Auto clean ctx when thread exists

			auto ctx = RLContext::createShared();
			runloops[tid] = ctx;
			return ctx;
		}

		return nullptr;
	}

public:
	static std::thread::id currentThreadId() {
		return std::this_thread::get_id();
	}

	// Init the run loop
	// This method can only be called by a single thread once
	// Native: Base on native run loop, supporting Windows/Darwin/Android/HarmonyOS/Glib
	// Event: Similar to libuv, supporting Windows/Darwin/Linux/Unix
	// To execute a loop, please use the EventLoop
	static void init(RLMode mode) {
		auto ctx = _getContext(currentThreadId());
		if (ctx == nullptr) {
			return;
		}
		if (ctx->getDispatcher() != nullptr) {
			KD_ASSERT_M(false, "RunLoop can only be initialized once from target thread");
			return;
		}

		ctx->m_mode = mode;
		_createDispatcher(ctx, mode);
		kd_runloop_debug("RunLoop initialized, mode = %d, dispatcher = %s", mode, ctx->getDispatcher()->m_dispatcherName.c_str());
	}

#if defined(KD_OS_OHOS)
	// Init uv_loop of HarmonyOS, it should be called after init()
	static void initUvLoop(uv_loop_t* uv_loop) {
		auto ctx = _getContext(currentThreadId());
		if (ctx == nullptr || ctx->getDispatcher() == nullptr) {
			KD_ASSERT_M(false, "init() should be called before calling initUvLoop()");
			return;
		}

		KD_RUNLOOP_CHECK_SAME_THREAD(ctx->m_runLoopThreadId);
		ctx->getDispatcher()->initUvLoop(uv_loop);
	}

	// Uninit uv_loop of HarmonyOS, it should be called before uv_loop destroyed
	void uninitUvLoop(uv_loop_t* uv_loop) {
		auto ctx = _getContext(currentThreadId());
		if (ctx == nullptr || ctx->getDispatcher() == nullptr) {
			return;
		}

		KD_RUNLOOP_CHECK_SAME_THREAD(ctx->m_runLoopThreadId);
		ctx->getDispatcher()->uninitUvLoop(uv_loop);
	}
#endif // KD_OS_OHOS

	// Process events once on current thread
	static bool processEvents(bool canWait) {
		auto ctx = _getContext(currentThreadId());
		if (ctx == nullptr || ctx->getDispatcher() == nullptr) {
			KD_ASSERT(false);
			return false;
		}

		KD_RUNLOOP_CHECK_SAME_THREAD(ctx->m_runLoopThreadId);
		return ctx->getDispatcher()->processEvents(canWait);
	}

	// Wake up waiting
	static void wakeUp() {
		auto ctx = _getContext(currentThreadId());
		if (ctx == nullptr || ctx->getDispatcher() == nullptr) {
			KD_ASSERT(false);
			return;
		}

		ctx->getDispatcher()->wakeUp();
	}

	// Check if run loop of thread expired
	static bool isExpired(std::thread::id tid) {
		auto ctx = _getContext(tid);
		if (ctx == nullptr || ctx->getDispatcher() == nullptr) {
			return true;
		}

		return ctx->m_expired.load();
	}

public:
	// Post tasks
	static void postTask(std::thread::id tid, RLTask task, uint32_t delayMs = 0) {
		auto ctx = _getContext(tid);
		if (ctx == nullptr || ctx->getDispatcher() == nullptr || ctx->m_expired || task == nullptr ) {
			// The thread either does not have a RunLoop or has exited.
			return;
		}

		if (delayMs <= 1) { // Non-delay task
			ctx->getDispatcher()->postTask(std::move(task));
		} else { // Delayed task
			addTimer(tid, delayMs, false, [timerTask = std::move(task)](int64_t) {
				timerTask();
			});
		}
	}

	// Add event notifier
	static void addEventNotifier(std::thread::id tid, const RLEventNotifier& eventNotifier) {
		auto ctx = _getContext(tid);
		if (ctx == nullptr || ctx->getDispatcher() == nullptr || ctx->m_expired || eventNotifier.handler == nullptr) {
			return;
		}

		auto isPending = ctx->addPendingCommand([eventNotifier](std::shared_ptr<RLContext> ctx) {
			auto it = ctx->m_eventNotifiers.find(eventNotifier.ev);
			if (it != ctx->m_eventNotifiers.end()) {
				return;
			}

			ctx->m_eventNotifiers.emplace(eventNotifier.ev, eventNotifier);
			ctx->getDispatcher()->addEventNotifier(eventNotifier);
		});

		if (isPending) {
			ctx->getDispatcher()->wakeUp();
		}
	}

	// Remove event notifier
	static void removeEventNotifier(std::thread::id tid, RLEvent ev) {
		auto ctx = _getContext(tid);
		if (ctx == nullptr || ctx->getDispatcher() == nullptr) {
			return;
		}

		auto isPending = ctx->addPendingCommand([ev](std::shared_ptr<RLContext> ctx) {
			auto it = ctx->m_eventNotifiers.find(ev);
			if (it != ctx->m_eventNotifiers.end()) {
				ctx->m_eventNotifiers.erase(it);
			}
			ctx->getDispatcher()->removeEventNotifier(ev);
		});
		
		if (isPending) {
			ctx->getDispatcher()->wakeUp();
		}
	}

	// Add timer, 0 means failed to add timer
	static uint64_t addTimer(std::thread::id tid, uint32_t intervalMs, bool repeat, RLTimerHandler handler) {
		auto ctx = _getContext(tid);
		if (ctx == nullptr || ctx->getDispatcher() == nullptr || ctx->m_expired || handler == nullptr) {
			return 0;
		}

		static std::atomic<uint64_t> static_global_id{ 0 };
		uint64_t timerId = (++static_global_id);
		
		// Create timer task
		int64_t expiryTime = now_time() + intervalMs;
		int64_t period = ( (intervalMs > 1 && repeat) ? intervalMs : -1);
		auto timerTask = kd::make_shared<RLTimerTask>(timerId, expiryTime, period, handler);

		// Add pending command
		auto isPending = ctx->addPendingCommand([timerTask] (std::shared_ptr<RLContext> ctx) {
			ctx->m_activeTimerTasks[timerTask->id] = timerTask;
			ctx->m_timerTasksQueue.push(RLTimerTaskNode(timerTask));
			ctx->getDispatcher()->updateTimer();
		});
		
		if (isPending) {
			ctx->getDispatcher()->wakeUp();
		}

		return timerId;
	};

	// Remove timer
	static void removeTimer(std::thread::id tid, uint64_t timerId) {
		auto ctx = _getContext(tid);
		if (ctx == nullptr || ctx->getDispatcher() == nullptr) {
			return;
		}

		auto isPending = ctx->addPendingCommand([timerId] (std::shared_ptr<RLContext> ctx) {
			auto it = ctx->m_activeTimerTasks.find(timerId);
			if (it == ctx->m_activeTimerTasks.end()) {
				return;
			}

			it->second->cancel();
			ctx->m_activeTimerTasks.erase(it);
			ctx->getDispatcher()->updateTimer();
		});
		
		if (isPending) {
			ctx->getDispatcher()->wakeUp();
		}
	}

private:
	friend class EventLoop;
	static std::shared_ptr<RLContext> getRunLoopContext(std::thread::id tid) {
		return _getContext(tid);
	}
};

// ===========================================
// EventLoop
// EventLoop is a convenient wrapper for running and manipulating 
// RunLoop in the current thread. It supports nested use within 
// the same thread. exec() can only be called in current thread, exit() can 
// be called from any threads
// ===========================================
using RLLooperCreator = std::function<std::shared_ptr<RLLooper>()>;

class EventLoop : public std::enable_shared_from_this<EventLoop> {
private:
	using ThreadEventLoopsMap = std::map<std::thread::id, std::vector<std::weak_ptr<EventLoop>>>;
	static ThreadEventLoopsMap& _threadEventLoops() {
		static ThreadEventLoopsMap* inst = new ThreadEventLoopsMap();
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

	static RLMutex& _eventLoopsMutex() {
		static RLMutex* inst = new RLMutex;
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

	static std::vector<std::shared_ptr<EventLoop>> _activatedEventLoops(std::thread::id tid) {
		std::vector<std::shared_ptr<EventLoop>> copy_loops_list;
		{
			std::lock_guard<RLMutex> lock(_eventLoopsMutex());
			auto& loops_map = _threadEventLoops();
			auto loops_list_it = loops_map.find(tid);
			if (loops_list_it == loops_map.end()) {
				return copy_loops_list;
			}

			auto& loops_list = loops_list_it->second;
			auto it = loops_list.begin();

			while (it != loops_list.end()) {
				auto sp = it->lock();
				if (sp) { // Add activated loops
					copy_loops_list.push_back(sp);
					++it;
				} else { // Clear expired loops
					it = loops_list.erase(it);
				}
			}
		}

		return copy_loops_list;
	}

	static void _addEventLoop(std::thread::id tid, std::weak_ptr<EventLoop> ev_loop) {
		std::lock_guard<RLMutex> lock(_eventLoopsMutex());
		auto& loops_map = _threadEventLoops();
		auto& loops_list = loops_map[tid];
		loops_list.push_back(ev_loop);
	}

	static void _removeEventLoop(std::thread::id tid, std::weak_ptr<EventLoop> ev_loop) {
		std::lock_guard<RLMutex> lock(_eventLoopsMutex());
		auto& loops_map = _threadEventLoops();
		auto loops_list_it = loops_map.find(tid);
		if (loops_list_it == loops_map.end()) {
			return;
		}

		auto& loops_list = loops_list_it->second;
		auto it = loops_list.begin();

		while (it != loops_list.end()) {
			if (it->expired() || kd::is_same_weak_ptr(*it, ev_loop)) {
				// Remove expired loops or matched loop
				it = loops_list.erase(it);
			} else {
				++it;
			}
		}
	}

	static void _exitAllEventLoops(std::thread::id tid, int exitCode) {
		auto loops_list = _activatedEventLoops(tid);
		kd_runloop_debug("<%s> Exit all %d event loops on thread <%s>",
			this_thread_id().c_str(), (int)loops_list.size(), thread_id_str(tid).c_str());

		// Exit all EventLoops 
		for (auto it = loops_list.rbegin(); it != loops_list.rend(); it++) {
			if ((*it).get() != nullptr) {
				(*it)->exit(exitCode);
			}
		}
	}

public:
	virtual ~EventLoop() {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		{
			std::lock_guard<decltype(m_mutex)> lock(m_mutex);
			m_looper = nullptr;
		}
		_removeEventLoop(m_runLoopThreadId, m_weakSelf);
		kd_runloop_debug("<%s> ::~EventLoop_%p destroyed", this_thread_id().c_str(), this);
	}

public:
	// Create a new event loop
	// In event mode, nativeLooperCreator must be empty
	// In native mode, if nativeLooperCreator is empty, EventLoop will
	// create a default native looper base on native run loop on target OS
	static std::shared_ptr<EventLoop> createShared(RLLooperCreator nativeLooperCreator = nullptr) {
		std::shared_ptr<EventLoop> inst(new EventLoop());
		inst->_init(std::move(nativeLooperCreator));
		return inst;
	}

	// Quit all event loop executing on the thread of tid
	static void quitAll(std::thread::id tid, int exitCode) {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		_exitAllEventLoops(tid, exitCode);
	}

	// Execute event loop on current thread
	int exec() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		auto ctx = RunLoop::getRunLoopContext(m_runLoopThreadId);
		if (ctx == nullptr || ctx->getDispatcher() == nullptr || ctx->m_expired) {
			return -1;
		}

		// Create a new looper
		std::shared_ptr<RLLooper> looper = nullptr;
		if (ctx->m_mode == RLMode::Native) {
			if (m_nativeLooperCreator) {
				looper = m_nativeLooperCreator();
			} else {
				looper = ctx->getDispatcher()->createDefaultLooper();
			}
		} else {
			looper = ctx->getDispatcher()->createDefaultLooper();
		}
		if (looper == nullptr) {
			KD_ASSERT_M(false, "Looper can not be empty");
			return -1;
		}

		// Init the looper
		looper->construct(ctx);
		{
			std::lock_guard<decltype(m_mutex)> lock(m_mutex);
			m_looper = looper;
		}
		
		// Exec the looper
		int exitCode = looper->exec();
		
		// Exit
		{
			std::lock_guard<decltype(m_mutex)> lock(m_mutex);
			m_looper = nullptr;
		}
		looper->destroy();
		looper = nullptr;

		return exitCode;
	}

	// Exit the event loop, it can be called from any threads
	void exit(int exitCode = 0) {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		std::weak_ptr<RLLooper> looperWp;
		{
			std::lock_guard<decltype(m_mutex)> lock(m_mutex);
			looperWp = m_looper;
			m_looper = nullptr;
		}
		
		// Async exit the looper on thread of ctx
		RunLoop::postTask(m_runLoopThreadId, [exitCode, looperWp] {
			auto looper = looperWp.lock();
			if (looper) {
				looper->exit(exitCode);
			}
		});
	}

protected:
	EventLoop() = default;
	void _init(RLLooperCreator nativeLooperCreator) {
		KD_ASSERT_M(nativeLooperCreator == nullptr,
			"EventLoop does not need looper creator in event mode");

		m_runLoopThreadId = std::this_thread::get_id();
		m_weakSelf = std::weak_ptr<EventLoop>(shared_from_this());
		m_nativeLooperCreator = std::move(nativeLooperCreator);
		_addEventLoop(m_runLoopThreadId, shared_from_this());
	}

protected:
	std::thread::id m_runLoopThreadId;
	std::weak_ptr<EventLoop> m_weakSelf;
	RLLooperCreator m_nativeLooperCreator;

	std::shared_ptr<RLLooper> m_looper;
	RLMutex m_mutex;
};

// QuitRunLoop
inline void QuitRunLoop(std::thread::id tid, int returnCode) {
	KD_RUNLOOP_CHECK_ANY_THREADS();
	EventLoop::quitAll(tid, returnCode);
}

__NAMESPACE_KD_END
