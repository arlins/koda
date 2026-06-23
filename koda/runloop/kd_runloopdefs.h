/** *************************************************
Created by: Arlin (arlins.dps@gmail.com).
This file defines the common parts related to RunLoop.
*****************************************************/

#pragma once
#include <vector>
#include <deque>
#include <map>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include <set>
#include <map>
#include <queue>
#include <functional>
#include <cassert>

#include "koda/kd_global.h"
#include "koda/base/kd_utils.h"
#include "koda/base/kd_memory.h"
#include "koda/base/kd_str.h"
#include "koda/base/kd_scopeguard.h"
#include "koda/async/kd_fastmutex.h"

#if defined(KD_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef KD_DEBUG
//#define KD_RUNLOOP_ENABLE_DEBUG
#endif

#ifdef KD_RUNLOOP_ENABLE_DEBUG
#define kd_runloop_debug(fmt, ...) printf("[RUNLOOP] [%lld] " fmt "\n", ::kd::now_time(), ##__VA_ARGS__);
#else
#define kd_runloop_debug(fmt, ...) do {} while(0);
#endif // KD_RUNLOOP_ENABLE_DEBUG

#ifdef KD_DEBUG
// Check if the target thread is the same as the current thread
#define KD_RUNLOOP_CHECK_SAME_THREAD(tid) \
 { KD_ASSERT_M(tid == std::this_thread::get_id(), "The operation must be the same as the current thread"); }

// Can be called from any threads
#define KD_RUNLOOP_CHECK_ANY_THREADS() do{ } while(0); 

// We can only use raw pointer here. Using a raw pointer to the object 
// is safe because the raw pointer of object will not be released until the 
// run loop finished
#define KD_RUNLOOP_CHECK_RAW_POINTER(x)  do{ } while(0); 

#define KD_DEBUG_BASE_DISPATCHER_DEFINE(Class) \
public: \
    std::string m_dispatcherName = #Class; \
    bool _register_dispatcher_name(const char* name) { m_dispatcherName = name; return true; }

#define KD_DEBUG_DISPATCHER_DEFINE(Class) \
private: \
    bool _name_registrar_##Class = _register_dispatcher_name(#Class);

#else
#define KD_RUNLOOP_CHECK_SAME_THREAD(tid) do{ } while(0);
#define KD_RUNLOOP_CHECK_ANY_THREADS()  do{ } while(0);
#define KD_RUNLOOP_CHECK_RAW_POINTER(x)  do{ } while(0); 
#define KD_DEBUG_BASE_DISPATCHER_DEFINE(Class)
#define KD_DEBUG_DISPATCHER_DEFINE(Class)
#endif


// namespace kd
__NAMESPACE_KD_BEGIN

// =================================
// Types define
// =================================

#if defined(KD_OS_WIN) // Windows
using RLEvent = HANDLE;
#else // Non-Windows
using RLEvent = int;
#endif

struct RLContext;
using RLMutex = FastMutex;
using RLPendingCmd = std::function<void(std::shared_ptr<RLContext>)>;
using RLTask = std::function<void()>;
using RLRepeatedTask = std::function<void(uint64_t)>;
using RLTimerHandler = std::function<void(uint64_t)>;

// RLMode
// Native: Base on native run loop, supporting Windows/Darwin/Android/HarmonyOS/Glib
// Event: Similar to libuv, supporting Windows/Darwin/Linux/Unix
enum class RLMode {
	Native, 
	Event,
};

// RLTimerTask
struct RLTimerTask {
	uint64_t id;
	int64_t expiryTime;
	int64_t period;
	bool canceled{ false };
	std::shared_ptr<RLTimerHandler> handler;

	RLTimerTask(uint64_t _id, int64_t _expiryTime, int64_t _period, RLTimerHandler _handler)
		: id(_id), expiryTime(_expiryTime), period(_period) {
		handler = kd::make_shared<RLTimerHandler>(std::move(_handler));
	}

	void cancel() {
		handler = nullptr;
		canceled = true;
	}

	bool valid() {
		if (canceled || handler == nullptr || (*handler) == nullptr) {
			return false;
		}
		return true;
	}
};

// RLTimerTaskNode
struct RLTimerTaskNode {
	std::shared_ptr<RLTimerTask> timerTask;

	RLTimerTaskNode(std::shared_ptr<RLTimerTask> task)
		: timerTask(task) {
	}

	bool operator>(const RLTimerTaskNode& other) const {
		return timerTask->expiryTime > other.timerTask->expiryTime;
	}
};

// RLEventNotifier
using RLEventHandler = std::function<void(RLEvent, int type)>;
struct  RLEventNotifier {
	enum Type {
		NONE = 0x0,
		READ = 0x01, // Readable, connection received, peer closed, etc.
		WRITE = 0x02, // Writable, connection successful, etc.
	};

	RLEvent ev;
	RLEventHandler handler;
	int type{ NONE }; // For Non-windows

	RLEventNotifier(const RLEventNotifier& o) = default;
	RLEventNotifier(RLEvent _ev, int _type, RLEventHandler _handler)
		: ev(_ev), handler(_handler), type(_type) {

	}
};

// =================================
// RLContext
// =================================
struct RLDispatcher;
struct RLContext {
	std::thread::id m_runLoopThreadId;
	std::thread::native_handle_type m_threadNativeHandle;
	
	RLMode m_mode{ RLMode::Event };
	std::atomic<bool> m_expired{ false }; // Thread has exited

	std::deque<RLTask> m_onceTasks;
	std::map<RLEvent, RLEventNotifier> m_eventNotifiers;

	std::priority_queue<RLTimerTaskNode, std::vector<RLTimerTaskNode>, std::greater<RLTimerTaskNode>> m_timerTasksQueue;
	std::map<uint64_t, std::shared_ptr<RLTimerTask>> m_activeTimerTasks;

	std::deque<RLPendingCmd> m_pendingCommands;
	std::atomic<bool> m_hasPendingCommands{ false };
	RLMutex m_pendingMutex;

private:
	// To improve runtime performance, we use a shared_ptr to hold itself. 
	// Before the ctx is destroyed, ctx.destroy() will be called to release 
	// this shared_ptr, so this approach will not cause a memory leak.
	std::shared_ptr<RLContext> m_self{ nullptr };

	friend class RunLoop;
	std::shared_ptr<RLDispatcher> m_dispatcher{ nullptr };

private:
	RLContext() : m_runLoopThreadId(std::this_thread::get_id())
		, m_threadNativeHandle(get_current_native_handle()) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		kd_runloop_debug("<%s> ::RLContext created", this_thread_id().c_str());
	}

public:
	~RLContext() {
		// RLContext is held by a shared_ptr, so it can be destructed in any thread.
		KD_RUNLOOP_CHECK_ANY_THREADS();
		kd_runloop_debug("<%s> ::~RLContext destroyed", this_thread_id().c_str());
	}

public:
	static std::shared_ptr<RLContext> createShared() {
		auto ctx = std::shared_ptr<RLContext>(new RLContext);
		ctx->m_self = ctx;
		return ctx;
	}

	// Get a weak_ptr pointing to itself. 
	// Instead of using enable_shared_from_this, we provide an 
	// interface to get the weak_ptr for clearer lifecycle management.
	// It can be called from any threads with a shared_ptr of ctx
	std::weak_ptr<RLContext> weakContext() {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		return m_self;
	}

	// Obtain the raw pointer to the dispatcher from `ctx`. 
	// It can be called from any threads with a shared_ptr of ctx.
	// The dispatcher pointer will never be null. `ctx` internally 
	// holds a shared pointer to the dispatcher.
	RLDispatcher* getDispatcher() {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		return m_dispatcher.get();
	};

	void destroy() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_expired = true;

		// Clear pending commands
		{
			std::lock_guard<RLMutex> lock(m_pendingMutex);
			m_pendingCommands.clear();
		}

		// Clear tasks and event notifiers
		m_onceTasks.clear();
		m_eventNotifiers.clear();

		// Clear all timers
		for (auto it = m_activeTimerTasks.begin(); it != m_activeTimerTasks.end(); it++) {
			it->second->cancel();
		}
		m_activeTimerTasks.clear();

		while (!m_timerTasksQueue.empty()) {
			m_timerTasksQueue.pop();
		}

		// Release self
		m_self.reset();

		// Do not release m_dispatcher in this function
		// m_dispatcher must be automatically released when ctx is released.
	}

	// Add pending commands
	bool addPendingCommand(RLPendingCmd cmd, bool forcePending = false) {
		KD_RUNLOOP_CHECK_ANY_THREADS();

		if (!forcePending && std::this_thread::get_id() == m_runLoopThreadId) {
			cmd(m_self);
			return false;
		} else {
			{
				std::lock_guard<RLMutex> lock(m_pendingMutex);
				m_pendingCommands.push_back(std::move(cmd));
			}
			m_hasPendingCommands.store(true, std::memory_order_release);
			return true;
		}
	}

	// Process pending commands
	void processPendingCommands() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (!m_hasPendingCommands.load(std::memory_order_acquire)) {
			return;
		}

		decltype(m_pendingCommands) cmds;
		{
			std::lock_guard<RLMutex> lock(m_pendingMutex);
			cmds.swap(m_pendingCommands);
			
		}
		m_hasPendingCommands.store(false, std::memory_order_release);

		for (auto& cmd : cmds) {
			if (cmd) {
				cmd(m_self);
			}
		}
	}
};

// ===========================================
// RLLooper
// Destructor can be called from any threads. 
// All other APIs will be called from the thread where the run loop resides. 
// ===========================================
struct RLLooper {
	RLLooper() : m_runLoopThreadId(std::this_thread::get_id()) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	}

	virtual ~RLLooper() {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		kd_runloop_debug("<%s> ::~RLLooper destroyed", this_thread_id().c_str());
	};

	virtual void construct(std::weak_ptr<RLContext> ctx) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_ctx = ctx; 
	}

	virtual void destroy() { 
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_ctx.reset(); 
	}

	virtual int exec() = 0;
	virtual void exit(int exitCode) = 0; // quit exec

protected:
	std::thread::id m_runLoopThreadId;
	std::weak_ptr<RLContext> m_ctx;
};

// ===========================================
// RLDispatcher
// 
// wakeUp/postTask/destroyed/Destructor can be called from any threads. 
// All other APIs are called only from the thread where the run loop resides.
// ===========================================
struct RLDispatcher {
	KD_DEBUG_BASE_DISPATCHER_DEFINE(RLDispatcher);

public:
	RLDispatcher() : m_runLoopThreadId(std::this_thread::get_id()) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
	}

	virtual ~RLDispatcher() {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		kd_runloop_debug("<%s> ::~RLDispatcher destroyed (%s)", this_thread_id().c_str(), m_dispatcherName.c_str());
	};

	void init(std::shared_ptr<RLContext> ctx) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_ctx = ctx.get();
		construct();
	}

	void uninit() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_destroyed = true;
		destroy();
	}

	bool destroyed() {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		// Although this function may be used by multiple threads, `ctx` will definitely 
		// not be released when this function is called, so using `ctx` here is safe.
		if (m_ctx == nullptr || m_ctx->m_expired) {
			return true;
		}
		return m_destroyed.load();
	}

	void postTask(RLTask task) {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (task == nullptr || destroyed()) {
			return;
		}

		// Although this function may be used by multiple threads, `ctx` will definitely 
		// not be released when this function is called, so using `ctx` here is safe.
		m_ctx->addPendingCommand([task](std::shared_ptr<RLContext> ctx) {
			ctx->m_onceTasks.push_back(task);
		});
		wakeUp(); // Wake up to process task
	}

public:
	// Process tasks
	void processTasks() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			return;
		}

		// Process pending commands
		m_ctx->processPendingCommands();

		// Swap once tasks
		decltype(m_ctx->m_onceTasks) onceTasks;
		onceTasks.swap(m_ctx->m_onceTasks);  

		// Process once tasks
		for (auto& task : onceTasks) {
			if (task) {
				task();
			}
		}
	}

	// Process event notifiers
	void processEventNotifiers(std::map<RLEvent, int> activatedEvents) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			return;
		}

		// Process pending commands
		m_ctx->processPendingCommands();

		for (auto& ev : activatedEvents) {
			auto it = m_ctx->m_eventNotifiers.find(ev.first);
			if (it != m_ctx->m_eventNotifiers.end()) {
				const RLEventNotifier& eventNotifier = it->second;
				if (eventNotifier.handler) { // handler(ev, type)
					eventNotifier.handler(ev.first, ev.second);
				}
			}
		}
	}

	// Process timer tasks
	bool processTimerTasks() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			return false;
		}

		// Process pending commands
		m_ctx->processPendingCommands();

		int64_t nowMs = now_time();
		bool timerTasksProcessed = false;

		while (!m_ctx->m_timerTasksQueue.empty()) {
			auto heapNode = m_ctx->m_timerTasksQueue.top();
			auto timerId = heapNode.timerTask->id;

			if (!heapNode.timerTask->valid()) {
				m_ctx->m_timerTasksQueue.pop();
				KD_ASSERT_M(m_ctx->m_activeTimerTasks.find(timerId) == m_ctx->m_activeTimerTasks.end(),
					"Invalid tasks should not exist in the active tasks at this time");
				continue; // Invalid task
			}

			if (heapNode.timerTask->expiryTime > nowMs) {
				break;
			}

			// Update flag
			timerTasksProcessed = true;

			// Remove timer task from queue
			m_ctx->m_timerTasksQueue.pop();
			m_ctx->m_activeTimerTasks.erase(heapNode.timerTask->id);
			RLTimerHandler timerHandler = std::move(*(heapNode.timerTask->handler));

			// Add repeated timer task to queue
			if (heapNode.timerTask->period > 0) {
				heapNode.timerTask->expiryTime = nowMs + heapNode.timerTask->period;
				heapNode.timerTask->handler = kd::make_shared<RLTimerHandler>(timerHandler);

				m_ctx->m_activeTimerTasks[heapNode.timerTask->id] = heapNode.timerTask;
				m_ctx->m_timerTasksQueue.push(heapNode);
			}

			// Call timer handler
			if (timerHandler) {
				timerHandler(timerId);
			}
		} // while

		// Update timer for repeated timer tasks
		if (timerTasksProcessed) {
			updateTimer();
		}

		return timerTasksProcessed;
	}

	// Get next absolute fire time, -1 means forever
	int64_t getNextAbsoluteFireTimeMs() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			return -1;
		}

		// Process pending commands
		m_ctx->processPendingCommands();

		// Clean up invalid timers 
		while (!m_ctx->m_timerTasksQueue.empty()) {
			auto heapNode = m_ctx->m_timerTasksQueue.top();
			auto timerId = heapNode.timerTask->id;

			if (!heapNode.timerTask->valid()) {
				m_ctx->m_timerTasksQueue.pop();
				KD_ASSERT_M(m_ctx->m_activeTimerTasks.find(timerId) == m_ctx->m_activeTimerTasks.end(),
					"Invalid tasks should not exist in the active tasks at this time");
				continue;
			}

			break;
		}

		if (m_ctx->m_timerTasksQueue.empty()) {
			return -1; // No timer task
		}

		return m_ctx->m_timerTasksQueue.top().timerTask->expiryTime;
	}

	void getEvents(std::vector<RLEvent>& eventHandles, int maxSize = 0) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			return;
		}

		// Process pending commands
		m_ctx->processPendingCommands();

		// Get events
		for (auto& pair : m_ctx->m_eventNotifiers) {
			eventHandles.push_back(pair.first);
			if (maxSize > 0 && (int)eventHandles.size() >= maxSize) {
				return;
			}
		}
	}

	bool isEventNotifierActive(RLEvent ev) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			return false;
		}

		// Process pending commands
		m_ctx->processPendingCommands();
		return m_ctx->m_eventNotifiers.find(ev) != m_ctx->m_eventNotifiers.end();
	}

public: // Sub class APIs
	virtual void construct() {}
	virtual void destroy() {}

#if defined(KD_OS_OHOS) // HarmonyOS
	virtual void initUvLoop(uv_loop_t* uv_loop) { KD_ASSERT(false); };
	virtual void uninitUvLoop(uv_loop_t* uv_loop) { KD_ASSERT(false); };
#endif // KD_OS_OHOS

	virtual void addEventNotifier(const RLEventNotifier& eventNotifier) = 0;
	virtual void removeEventNotifier(RLEvent ev) = 0;
	virtual void updateTimer() = 0;

	virtual std::shared_ptr<RLLooper> createDefaultLooper() = 0;
	virtual bool processEvents(bool canWait) = 0;

	// In native mode, wakeUp is used to wake up to call processEvents() 
	// In event mode,  wakeUp is used to wake up waiting processEvents()
	virtual void wakeUp() = 0;

protected:
	std::thread::id m_runLoopThreadId;
	std::atomic<bool> m_destroyed{ false };

	// We can directly use the raw pointer `ctx` inside the dispatcher, 
	// which is safe to use within the dispatcher without needing to 
	// check if `ctx` is null. This is because `ctx` internally holds a shared 
	// pointer to the dispatcher, and the dispatcher will not be released 
	// until `ctx` is released.
	RLContext* m_ctx;
};


// ===========================================
// RLCommonEventLooper for event mode
// ===========================================
class RLCommonEventLooper : public RLLooper {
	std::atomic<bool> m_exit{ false };
	std::atomic<int> m_exitCode{ 0 };

private:
	RLCommonEventLooper() = default;

public:
	static std::shared_ptr<RLLooper> creatShared() {
		return std::shared_ptr<RLLooper>(new RLCommonEventLooper());
	}

	int exec() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		auto ctx = m_ctx.lock();
		if (ctx == nullptr || ctx->getDispatcher() == nullptr || ctx->getDispatcher()->destroyed()) {
			return -1;
		}

		m_exit = false;
		m_exitCode = 0;

		while (!m_exit.load() && !ctx->getDispatcher()->destroyed()) {
			ctx->getDispatcher()->processEvents(true);
		}

		return m_exitCode.load();
	}

	void exit(int exitCode) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		auto ctx = m_ctx.lock();
		if (ctx == nullptr || ctx->getDispatcher() == nullptr || ctx->getDispatcher()->destroyed()) {
			return;
		}

		m_exitCode = exitCode;
		m_exit = true;
		ctx->getDispatcher()->wakeUp(); // Wake up to exit
	}
};

__NAMESPACE_KD_END
