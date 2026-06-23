/** *********************************************************************************
	Created by: Arlin (arlins.dps@gmail.com).

	The Operation library
	A lightweight, cross-platform async framework inspired by iOS NSOperationQueue.
	Includes OperationQueue (thread pool) and OperationMainQueue (main-thread dispatch)
	OperationQueue supports all platforms and OperationMainQueue supports Windows/
	Darwin (iOS/macOS...)/Android/HarmonyOS/GLib (depending on the platform/library's 
	event loop).

	NOTE: Executing operations cannot be canceled. For safety, use [TrackPointer] for
	single-threaded/UI contexts, and std::weak_ptr/shared_ptr across multiple threads.

	[!] OperationQueue
	Supports automatic or manual lifecycle management.
	- Manual (Default): Requires explicit call to `destroy()`.
	- Automatic: Enabled via `setAutoDestroyedAfterAllOpsDone(true)`.
	WARNING: Calling setAutoDestroyedAfterAllOpsDone too early may exit the
	queue prematurely. Always call it AFTER adding all operations.

	Usage:
		auto queue = OperationQueue::createShared(4);
		queue->addOperation([]{});
		queue->setAutoDestroyedAfterAllOpsDone(true); // Call after adding ops
		queue->waitUntilAllOperationsAreFinished();

	[!]  OperationNativeQueue (Independently)
	OperationNativeQueue encapsulates the operations of the native RunLoop on platforms 
	such as Windows/Darwin (iOS/macOS...)/Android/HarmonyOS/GLib. it can only be 
	integrated and cannot be used independently. You can inherit from this class to implement 
	a custom Queue. OperationMainQueue is implemented based on this class. It's important 
	to note that the lifecycle of OperationNativeQueue must be the same as that of Native 
	RunLoop.

	[!] OperationMainQueue
	Dispatches operations to the main thread from any threads. Call `startup()` to init, and
	`shutdown()` upon program exit. A main thread message loop (e.g., Win32 GetMessage loop)
	must already exist; this class does not create one.

	Usage:
		OperationMainQueue::queue().addOperation([]{
			// Do work on main-thread
		});
********************************************************************************* */

#pragma once 

#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <deque>
#include <unordered_set>
#include <map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <memory>
#include <cstdint>

#include "koda/kd_global.h"
#include "koda/base/kd_str.h"
#include "koda/base/kd_utils.h"
#include "koda/async/kd_fastmutex.h"

#if defined(KD_OS_WIN) // Windows
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#elif defined(KD_OS_DARWIN) // Apple
#include <dispatch/dispatch.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(KD_OS_ANDROID) // Android
#include <android/looper.h>
#include <unistd.h>
#include <sys/timerfd.h>
#elif defined(KD_OS_OHOS) // HarmonyOS
#include <uv.h>
#include <cstring>
#include <cstdint>
#elif defined(KD_HAS_GLIB) // Glib
#include <glib.h>
#include <cstdint>
#endif

#define KD_OP_UNSUPPORTED_OS "Main queue is unsupported on the os"

#ifdef KD_DEBUG
// #define KD_OP_ENABLE_DEBUG
#endif

#ifdef KD_OP_ENABLE_DEBUG
#define kd_op_debug(fmt, ...) \
{ printf("[OP] [%lld] [%s] " fmt "\n", kd::now_time(), kd::this_thread_id().c_str(), ##__VA_ARGS__); }

#define kd_op_debug2(fmt, ...) \
{	static std::mutex __op_debug_mtx___; \
	std::lock_guard<std::mutex> lock(__op_debug_mtx___); \
	kd_op_debug(fmt, ##__VA_ARGS__); \
}

#define KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD() \
KD_ASSERT_M(isQueueThread(), "Thread must be the thread which Queue created");

#define KD_REQUIRE_MAIN_THREAD() \
KD_ASSERT_M(OperationMainQueue::isMainThread(), "Main thread is required");

#define KD_OP_DEBUG_WAITING_COUNT_DEFINE \
public: \
std::atomic<int> m_debug_waitForeverThreads { 0 }; \
std::atomic<int> m_debug_waitUntilThreads{ 0 };

#define KD_OP_DEBUG_WAITING_FOREVER_COUNT() \
q->m_debug_waitForeverThreads++; \
kd::ScopeGuard debug_wf_sg([=] { \
	q->m_debug_waitForeverThreads--; \
});

#define KD_OP_DEBUG_WAITING_UNTIL_COUNT() \
q->m_debug_waitUntilThreads++; \
kd::ScopeGuard debug_wu_sg([=] { \
	q->m_debug_waitUntilThreads--; \
});

#define KD_OP_DEBUG_OUTPUT_WAITING_COUNT(name) \
kd_op_debug2("%s, forever-until-run = %d%d%d", name, \
	m_debug_waitForeverThreads.load(), m_debug_waitUntilThreads.load(), \
	(int)(m_workThreadIds.size()) - m_debug_waitForeverThreads.load() - m_debug_waitUntilThreads.load());

#else 
#define kd_op_debug(fmt, ...) do {} while (0);
#define kd_op_debug2(fmt, ...) do {} while (0);
#define KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD() do {} while (0);
#define KD_REQUIRE_MAIN_THREAD() do {} while (0);
#define KD_OP_DEBUG_WAITING_COUNT_DEFINE
#define KD_OP_DEBUG_WAITING_FOREVER_COUNT() do {} while (0);
#define KD_OP_DEBUG_WAITING_UNTIL_COUNT() do {} while (0);
#define KD_OP_DEBUG_OUTPUT_WAITING_COUNT(name) do {} while (0);
#endif // KD_OP_ENABLE_DEBUG


__NAMESPACE_KD_BEGIN

using QueueMutex = kd::FastMutex;

// ==============================
// Operation
// ==============================
class Operation {
public:
	virtual ~Operation() {
	}

	Operation(uint64_t oid, std::function<void()> func, unsigned int delayMs)
		: m_oid(oid)
		, m_func(func) {
		m_fireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
	};

	void run() {
		try {
			if (m_func) {
				m_func();
			}
		} catch (...) {
			kd_op_debug("[ERROR] Perform operation runtime error");
		}
	}

	uint64_t m_oid;
	std::function<void()> m_func;
	std::chrono::steady_clock::time_point m_fireTime;
};

// ==============================
// Operation queue
// ==============================
class OperationQueue : public std::enable_shared_from_this<OperationQueue> {
	KD_DISABLE_COPY(OperationQueue);
	KD_DISABLE_MOVE(OperationQueue);
	KD_OP_DEBUG_WAITING_COUNT_DEFINE;

private:
	ThreadPriority m_priority{ ThreadPriority::Normal };
	std::string m_name;
	std::unordered_set<std::thread::id> m_workThreadIds;

	bool m_stop{ false };
	bool m_autoDestroyedAfterAllOpsDone{ false };
	unsigned int m_pendingOps{ 0 };

	std::mutex m_queueMutex;
	std::condition_variable m_queueCondition;
	std::deque<std::shared_ptr<Operation>> m_operations;
	std::map<std::thread::id, std::chrono::steady_clock::time_point> m_waitingOps;

	std::condition_variable m_allOpsDoneCondition;

public: // static
	static unsigned int preferredThreadsNumber() {
		return std::thread::hardware_concurrency();
	}

	static std::shared_ptr<OperationQueue>
		createShared(unsigned int threadCount = 1,
			ThreadPriority priority = ThreadPriority::Normal, const std::string& name = "") {
		std::shared_ptr<OperationQueue> ptr(new OperationQueue());
		ptr->_init(threadCount, priority, name);
		return ptr;
	}

	static std::shared_ptr<OperationQueue> queueByName(const std::string& name) {
		return globalQueuesHolder().sharedOpsQueueByName(name);
	}

private:
	OperationQueue() {
	};

	void _init(unsigned int threadCount, ThreadPriority priority, const std::string& name) {
		// Hold the queue internally
		globalQueuesHolder().holdOpsQueue(shared_from_this());

		m_priority = priority;
		m_autoDestroyedAfterAllOpsDone = false;

		m_name = name;
		if (m_name.length() == 0) {
			static std::atomic<unsigned int> queue_index{ 0 };
			m_name = "KD_DefaultOpsQueue_" + std::to_string(++queue_index);
		}

		if (threadCount == 0) {
			threadCount = 1;
		}
		kd_op_debug2("Start queue, name = %s, threadCount = %d", m_name.c_str(), threadCount);

		// Start work threads
		struct ThreadStartupGate {
			std::mutex mtx;
			std::condition_variable cv;
			bool ready{ false };
		};

		auto threadGate = ::kd::make_shared<ThreadStartupGate>();
		for (size_t i = 0; i < threadCount; ++i) {
			std::thread worker([sp = shared_from_this(), threadGate]() {
				{	// Waiting for thread priority settings done
					std::unique_lock<std::mutex> lock(threadGate->mtx);
					threadGate->cv.wait(lock, [&] { return threadGate->ready; });
				}

				// Start the thread
				OperationQueue::thread_processOperations(sp);
			});

			// Setup thread priority and detach the thread
			set_thread_priority(worker, m_priority);
			worker.detach();
		}

		// Wake up threads
		{
			std::lock_guard<std::mutex> lock(threadGate->mtx);
			threadGate->ready = true;
		}
		threadGate->cv.notify_all();
	}

public:
	virtual ~OperationQueue() {
		kd_op_debug2("~OperationQueue, name = %s", m_name.c_str());
	}

	std::string name() {
		return m_name;
	}

	void destroy() {
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			if (m_stop) {
				return;
			}
			m_stop = true;
			m_pendingOps = 0;
			m_operations.clear();
		}
		kd_op_debug2("Destroy queue, name = %s", m_name.c_str());
		m_queueCondition.notify_all();
		m_allOpsDoneCondition.notify_all();
	}

	bool isDestroyed() {
		std::lock_guard<std::mutex> lock(m_queueMutex);
		return m_stop;
	}

	void setAutoDestroyedAfterAllOpsDone(bool destroyed) {
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			m_autoDestroyedAfterAllOpsDone = destroyed;
		}
		m_queueCondition.notify_all();
	}

	uint64_t addOperation(std::function<void()> task, unsigned int delayMs = 0) {
		if (!task) {
			return 0;
		}

		static std::atomic<uint64_t> static_global_id{ 0 };
		uint64_t oid = 0;
		bool shouldNotifyQueue = false;
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			if (m_stop) {
				kd_op_debug2("Can not add the operation when queue was destroyed");
				return 0;
			}
			// KD_OP_DEBUG_OUTPUT_WAITING_COUNT("Adding Op");

			// Insert the op to suitable pos
			auto op = ::kd::make_shared<Operation>((++static_global_id), std::move(task), delayMs);
			oid = op->m_oid;

			if (m_operations.empty()) {
				shouldNotifyQueue = true;
				m_operations.push_back(op);
			} else if (op->m_fireTime >= m_operations.back()->m_fireTime) {
				m_operations.push_back(op);
			} else {
				auto it = m_operations.rbegin();
				for (; it != m_operations.rend(); ++it) {
					if (op->m_fireTime >= (*it)->m_fireTime) {
						break;
					}
				}
				// base(): converts reverse iterator to iterator
				m_operations.insert(it.base(), op);
				if (m_operations.front() == op) {
					shouldNotifyQueue = true;
				}
			}

			// Increase pending ops
			m_pendingOps++;
		}

		// Notify
		if (shouldNotifyQueue) {
			m_queueCondition.notify_one();
		}

		return oid;
	}

	void removeOperation(uint64_t oid) {
		bool shouldNotifyAllOpsDone = false;
		bool shouldNotifyQueue = false;
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			for (auto it = m_operations.begin(); it != m_operations.end(); it++) {
				if (it->get()->m_oid == oid) {
					shouldNotifyQueue = (it == m_operations.begin());
					m_operations.erase(it);

					if (m_pendingOps >= 1) {
						m_pendingOps--;
					}
					break;
				}
			}

			shouldNotifyAllOpsDone = (m_pendingOps == 0 && m_operations.empty());
		}

		// Notify
		if (shouldNotifyAllOpsDone) {
			m_allOpsDoneCondition.notify_all();
		}
		if (shouldNotifyQueue) {
			m_queueCondition.notify_one();
		}
	}

	void cancelAllOperations() {
		{
			std::unique_lock<std::mutex> lock(m_queueMutex);
			if (m_stop) {
				return;
			}

			size_t clearedOps = m_operations.size();
			if ((size_t)m_pendingOps < clearedOps) {
				KD_ASSERT_M(false, "Cancel all operations runtime error");
				m_pendingOps = 0;
			} else {
				m_pendingOps = m_pendingOps - (unsigned int)clearedOps;
			}

			m_operations.clear();
		}

		m_queueCondition.notify_all();
		m_allOpsDoneCondition.notify_all();
	}

	void waitUntilAllOperationsAreFinished() {
		std::unique_lock<std::mutex> lock(m_queueMutex);
		if (m_stop) {
			return;
		}
		if (m_workThreadIds.find(std::this_thread::get_id()) != m_workThreadIds.end()) {
			KD_ASSERT_M(false, "Can not wait in the worker threads of the queue");
			return; // Avoiding deadlock
		}

		m_allOpsDoneCondition.wait(lock, [this] {
			return m_stop || (m_pendingOps == 0 && m_operations.empty());
		});
	}

private:
	static void thread_processOperations(std::shared_ptr<OperationQueue> q) {
		// Add work thread id
		{
			std::lock_guard<std::mutex> lock(q->m_queueMutex);
			q->m_workThreadIds.insert(std::this_thread::get_id());
		}

#if defined(KD_OS_DARWIN) || defined(KD_OS_ANDROID)
		// Ensure thread priorities on Apple and Android
		set_current_thread_priority(q->m_priority);
#endif

		while (!q->m_stop) {
			// Waiting for a ready op
			std::shared_ptr<Operation> op = nullptr;
			bool taskPopped = false;
			{
				std::unique_lock<std::mutex> lock(q->m_queueMutex);

				while (true) {
					if (q->m_stop
						|| (q->m_autoDestroyedAfterAllOpsDone && q->m_pendingOps == 0 && q->m_operations.empty())) {
						break; // Stopped or all ops done
					}

					if (q->m_operations.empty()) {
						// No ops now, all threads need to wait indefinitely.
						KD_OP_DEBUG_WAITING_FOREVER_COUNT();
						q->m_queueCondition.wait(lock);
					} else {
						auto now = std::chrono::steady_clock::now();
						auto earliestOp = q->m_operations.front();

						// Check earliest op is ready or not
						if (now >= earliestOp->m_fireTime) {
							// Earliest op is ready, pop to execute the op.
							op = earliestOp;
							q->m_operations.pop_front();
							taskPopped = true;

							// Wake up a thread to wait for next op
							if (!q->m_operations.empty()) {
								q->m_queueCondition.notify_one();
							}

							// Break to run the op
							break;
						} else {
							bool isWaitingOp = false;
							for (auto& it : q->m_waitingOps) {
								if (it.second == earliestOp->m_fireTime) {
									isWaitingOp = true;
									break;
								}
							}

							if (isWaitingOp) {
								// There are already threads waiting for this op, perform waiting indefinitely
								KD_OP_DEBUG_WAITING_FOREVER_COUNT();
								q->m_queueCondition.wait(lock);
							} else {
								// No thread is waiting for this op, perform waiting for a limited time
								auto fireTime = earliestOp->m_fireTime;
								q->m_waitingOps[std::this_thread::get_id()] = fireTime;
								KD_OP_DEBUG_WAITING_UNTIL_COUNT();
								q->m_queueCondition.wait_until(lock, fireTime);

								auto it = q->m_waitingOps.find(std::this_thread::get_id());
								if (it != q->m_waitingOps.end()) {
									q->m_waitingOps.erase(it);
								}
							}
						}
					}
				} // while(true)

			} // Waiting for a ready op

			// Perform the op
			if (op) {
				op->run();
				op.reset(); // Release op ASAP.
			}

			// Check state
			bool shouldNotifyAllOpsDone = false;
			bool queueDestroyed = false;
			{
				std::unique_lock<std::mutex> lock(q->m_queueMutex);
				if (q->m_stop) {
					break; // Quit loop
				}

				// Reduce pending Ops 
				if (taskPopped) {
					if (q->m_pendingOps >= 1) {
						q->m_pendingOps--;
					}
				}

				// Checking all ops done and destroy the queue if needed
				if (q->m_pendingOps == 0 && q->m_operations.empty()) {
					shouldNotifyAllOpsDone = true;

					if (q->m_autoDestroyedAfterAllOpsDone && !q->m_stop) {
						q->m_stop = true;
						q->m_pendingOps = 0;
						q->m_operations.clear();
						queueDestroyed = true;
					}
				}
			}

			// Notify
			if (shouldNotifyAllOpsDone || queueDestroyed) {
				kd_op_debug2("Trigger all-ops-done signal");
				q->m_allOpsDoneCondition.notify_all();
			}
			if (queueDestroyed) {
				kd_op_debug2("Destroy queue automatically after all ops done");
				q->m_queueCondition.notify_all();
			}
		}  // while

		// Exit thread.
		std::shared_ptr<OperationQueue> unholdQueue;
		{
			std::lock_guard<std::mutex> lock(q->m_queueMutex);
			q->m_workThreadIds.erase(std::this_thread::get_id());

			// Release the queue held internally by last thread.
			if (q->m_workThreadIds.size() == 0) {
				kd_op_debug2("Release the queue held internally, name = %s", q->m_name.c_str());
				unholdQueue = globalQueuesHolder().unholdOpsQueue(uintptr_t(q.get()));
			}
		}

		// Do not use anything related to queue after this.
		// Queue will be released automatically
	}

private: // Operation queues holder
	class OperationQueueHolder {
		QueueMutex m_mutex;
		std::map<uintptr_t, std::shared_ptr<OperationQueue>> m_ops_queues;

	public:
		void holdOpsQueue(std::shared_ptr<OperationQueue> q) {
			if (!q) {
				KD_ASSERT(false);
				return;
			}

			std::lock_guard<QueueMutex> lock(m_mutex);
			uintptr_t ptr = uintptr_t(q.get());
			auto it = m_ops_queues.find(ptr);
			if (it == m_ops_queues.end()) {
				m_ops_queues[ptr] = q;
			} else {
				KD_ASSERT(false);
			}
		}

		std::shared_ptr<OperationQueue> unholdOpsQueue(uintptr_t ptr) {
			std::lock_guard<QueueMutex> lock(m_mutex);
			std::shared_ptr<OperationQueue> queue;
			auto it = m_ops_queues.find(ptr);
			if (it != m_ops_queues.end()) {
				queue = it->second;
				m_ops_queues.erase(it);
			}

			return queue;
		}

		std::shared_ptr<OperationQueue> sharedOpsQueue(uintptr_t ptr) {
			std::lock_guard<QueueMutex> lock(m_mutex);
			auto it = m_ops_queues.find(ptr);
			if (it != m_ops_queues.end()) {
				return it->second;
			} else {
				return nullptr;
			}
		}

		std::shared_ptr<OperationQueue> sharedOpsQueueByName(const std::string& name) {
			std::lock_guard<QueueMutex> lock(m_mutex);
			for (auto& queue : m_ops_queues) {
				std::shared_ptr<OperationQueue> queue_ptr = queue.second;
				if (queue_ptr && queue_ptr->m_name == name) {
					return queue_ptr;
				}
			}

			return nullptr;
		}
	};

	static OperationQueueHolder& globalQueuesHolder() {
		static OperationQueueHolder* inst = new OperationQueueHolder();
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}
};

// ============================
// OperationNativeQueue
// ============================
class OperationNativeQueue {
	KD_DISABLE_COPY(OperationNativeQueue);
	KD_DISABLE_MOVE(OperationNativeQueue);

protected:
	OperationNativeQueue() {
		m_queueThreadId = std::this_thread::get_id();
	}

	virtual ~OperationNativeQueue() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		_uninit();
	}

	bool isQueueThread() {
		return std::this_thread::get_id() == m_queueThreadId;
	}

	std::thread::id queueThreadId() {
		return m_queueThreadId;
	}

	uint64_t addOperation(std::function<void()> task, int delayMs = 0) {
		if (!task) {
			return 0;
		}

		static std::atomic<uint64_t> static_global_id{ 0 };
		uint64_t oid = (++static_global_id);
		bool shouldWakeUp = false;

		{
			std::lock_guard<QueueMutex> lock(m_mutex);
			m_hasPendingCommands = true;
			m_pendingCommands.push_back([q = this, task_ = std::move(task), delayMs, oid] {
				auto op = ::kd::make_shared<Operation>(oid, std::move(task_), delayMs);
				if (delayMs > 0) {
					// Insert the delayed op to suitable pos
					if (q->m_delayedOperations.empty()) {
						q->m_delayedOperations.push_back(op);
					} else if (op->m_fireTime >= q->m_delayedOperations.back()->m_fireTime) {
						q->m_delayedOperations.push_back(op);
					} else {
						auto it = q->m_delayedOperations.rbegin();
						for (; it != q->m_delayedOperations.rend(); ++it) {
							if (op->m_fireTime >= (*it)->m_fireTime) {
								break;
							}
						}
						// base(): converts reverse iterator to iterator
						q->m_delayedOperations.insert(it.base(), op);
					}
				} else {
					// Insert the non-delayed op to back
					q->m_operations.push_back(op);
				}
			});

			shouldWakeUp = _checkAndUpdatePendingWakeUpState();
		}

		if (shouldWakeUp) {
			_wakeUp();
		}
		
		return oid;
	}

	void removeOperation(uint64_t oid) {
		bool shouldWakeUp = false;

		{
			std::lock_guard<QueueMutex> lock(m_mutex);
			m_pendingCommands.push_back([q = this, oid] {
				for (auto it = q->m_operations.begin(); it != q->m_operations.end(); it++) {
					if ((*it)->m_oid == oid) {
						q->m_operations.erase(it);
						return;
					}
				}

				for (auto it = q->m_delayedOperations.begin(); it != q->m_delayedOperations.end(); it++) {
					if ((*it)->m_oid == oid) {
						q->m_delayedOperations.erase(it);
						return;
					}
				}
			});
			
			shouldWakeUp = _checkAndUpdatePendingWakeUpState();
		}

		if (shouldWakeUp) {
			_wakeUp();
		}
	}

	void cancelAllOperations() {
		bool shouldWakeUp = false;

		{
			std::lock_guard<QueueMutex> lock(m_mutex);
			m_pendingCommands.push_back([q = this] {
				q->m_operations.clear();
				q->m_delayedOperations.clear();
			});
			
			shouldWakeUp = _checkAndUpdatePendingWakeUpState();
		}

		if (shouldWakeUp) {
			_wakeUp();
		}
	}

protected:
#ifdef KD_OS_OHOS // HarmonyOS
	void _init(uv_loop_t* uvLoop) {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		_platform_init(uvLoop);
	}
#else
	void _init() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		_platform_init();
	}
#endif

	void _uninit() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		{
			std::lock_guard<QueueMutex> lock(m_mutex);
			m_hasPendingWakeUp = false;
			m_lastPendingWakeUpTimeMs = 0;
			m_waitingDelayedFireTimeMs = 0;
			m_hasPendingCommands = false;
			m_pendingCommands.clear();
			m_operations.clear();
			m_delayedOperations.clear();
		}
		_platform_uninit();
	}

	void _wakeUp() {
		bool done = _platform_wakeUp();
		if (!done) { // Fallback
			std::lock_guard<QueueMutex> lock(m_mutex);
			m_hasPendingWakeUp = false;
			m_lastPendingWakeUpTimeMs = 0;
		}
	}

	bool _checkAndUpdatePendingWakeUpState() {
		bool shouldWakeUp = false;
		auto nowMs = now_time();

		if (m_hasPendingWakeUp) {
			if (m_lastPendingWakeUpTimeMs == 0 
				|| nowMs - m_lastPendingWakeUpTimeMs > 50) {
				kd_op_debug("[WARNING] A pending wake-up is out of date");
				m_lastPendingWakeUpTimeMs = now_time();
				shouldWakeUp = true;
			} else {
				shouldWakeUp = false;
			}
		} else {
			m_lastPendingWakeUpTimeMs = now_time();
			m_hasPendingWakeUp = true;
			shouldWakeUp = true;
		}

		return shouldWakeUp;
	}

	void _processOperations() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();

		// Process commands
		decltype(m_pendingCommands) cmds;
		{
			std::lock_guard<QueueMutex> lock(m_mutex);
			cmds.swap(m_pendingCommands);
			m_hasPendingCommands = false;
			m_hasPendingWakeUp = false; // Reset wake up flag
		}
		for (auto& cmd : cmds) {
			if (cmd) {
				cmd();
			}
		}

		// Run non-delayed ops
		decltype(m_operations) ops;
		ops.swap(m_operations);

		for (auto& op : ops) {
			op->run();
		}
		ops.clear();

		// Run delayed ops
		auto now = std::chrono::steady_clock::now();
		decltype(m_delayedOperations) expiredDelayedOps;

		while (!m_delayedOperations.empty()) {
			auto op = m_delayedOperations.front();
			if (op->m_fireTime <= now + std::chrono::milliseconds(2)) {
				expiredDelayedOps.push_back(op);
				m_delayedOperations.pop_front();
				continue;
			}

			break;
		}

		for (auto& op : expiredDelayedOps) {
			op->run();
		}
		expiredDelayedOps.clear();
		_processDelayedOperation();

		// Check if wake-up is needed
		// The wake-up call here is a fallback to ensure there are still 
		// pending commands but the wake-up is lost; For performance, we 
		// don't use locks and it is safe
		if (m_hasPendingCommands.load() && !(m_hasPendingWakeUp.load())) {
			kd_op_debug("[WARNING] Wake up after processing ops");
			_wakeUp();
		} 
	}

	void _processDelayedOperation() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		if (m_delayedOperations.empty()) {
			m_waitingDelayedFireTimeMs = 0;
			return;
		}

		auto op = m_delayedOperations.front();
		long long fireTimeMs = time_to_longlong(op->m_fireTime);
		if (m_waitingDelayedFireTimeMs <= 0 || std::abs(fireTimeMs - m_waitingDelayedFireTimeMs) > 2) {
			m_waitingDelayedFireTimeMs = fireTimeMs;
			bool ret = _platform_delayProcessOperations(fireTimeMs);
			if (!ret) {
				m_waitingDelayedFireTimeMs = 0;
			}
		}
	}

protected:
	std::thread::id m_queueThreadId;

	QueueMutex m_mutex;
	std::atomic<bool> m_hasPendingCommands {false};
	std::deque<std::function<void()>> m_pendingCommands;
	std::deque<std::shared_ptr<Operation>> m_operations;
	std::deque<std::shared_ptr<Operation>> m_delayedOperations;
	
	long long m_waitingDelayedFireTimeMs{ 0 };
	long long m_lastPendingWakeUpTimeMs {0};
	std::atomic<bool> m_hasPendingWakeUp{ false };

	// Platforms
#if defined(KD_OS_OHOS) // HarmonyOS
protected:
	uv_loop_t* m_uvLoop{ nullptr };
	uv_async_t m_wakeAsync{};
	uv_timer_t m_delayTimer{};
	bool m_asyncInitialized{ false };
	bool m_timerInitialized{ false };

protected:
	void _platform_init(uv_loop_t* uvLoop) {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		KD_ASSERT_M(uvLoop != nullptr, "uvLoop must not be null");
		m_timerInitialized = false;
		m_asyncInitialized = false;

		m_uvLoop = uvLoop;
		if (!m_uvLoop) {
			return;
		}

		m_wakeAsync.data = this;
		int ret = uv_async_init(m_uvLoop, &m_wakeAsync, [](uv_async_t* handle) {
			auto* q = reinterpret_cast<OperationNativeQueue*>(handle->data);
			if (q) {
				q->_processOperations();
			}
		});
		m_asyncInitialized = (ret == 0);
	}

	void _platform_uninit() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();

		if (m_uvLoop) {
			if (m_asyncInitialized) {
				uv_close(reinterpret_cast<uv_handle_t*>(&m_wakeAsync), nullptr);
				m_asyncInitialized = false;
			}
			if (m_timerInitialized) {
				uv_close(reinterpret_cast<uv_handle_t*>(&m_delayTimer), nullptr);
				m_timerInitialized = false;
			}
		}
		m_uvLoop = nullptr;
	}

	bool _platform_wakeUp() {
		if (!m_uvLoop || !m_asyncInitialized) {
			KD_ASSERT(false);
			return false;
		}

		kd_op_debug("Wake up native-queue");
		return uv_async_send(&m_wakeAsync) == 0;
	}

	bool _platform_delayProcessOperations(long long fireTimeMs) {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		if (!m_uvLoop) {
			KD_ASSERT(false);
			return false;
		}

		// Init timer
		if (!m_timerInitialized) {
			int initRet = uv_timer_init(m_uvLoop, &m_delayTimer);
			if (initRet != 0) {
				return false;
			}
			m_delayTimer.data = this;
			m_timerInitialized = true;
		}

		auto nowMs = now_time();
		uint64_t intervalMs = (fireTimeMs > nowMs) ? static_cast<uint64_t>(fireTimeMs - nowMs) : 0;

		// Start timer
		int ret = uv_timer_start(&m_delayTimer, [](uv_timer_t* handle) {
			auto* q = reinterpret_cast<OperationNativeQueue*>(handle->data);
			if (q) {
				q->m_waitingDelayedFireTimeMs = 0;
				q->_processOperations();
			}
		}, intervalMs, 0); // 0 represents One-shot (single trigger).

		kd_op_debug("Start timer: %lld, ret = %d", fireTimeMs, ret);
		return ret == 0;
	}

#elif defined(KD_OS_WIN) // Win
protected:
	static constexpr UINT WM_WAKEUP_QUEUE = WM_USER + 100;
	static constexpr UINT_PTR TIMER_DO_OPERATION = 100;
	static constexpr char OP_MSG_WND_CLASS_NAME[] = "KDOperationMainQueueMsgWnd";

	HWND m_msgWnd{ NULL };

protected:
	void _platform_init() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		::timeBeginPeriod(1);

		static std::once_flag once_flag_;
		std::call_once(once_flag_, [] {
			WNDCLASSEXA wc = { 0 };
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = &OperationNativeQueue::_msgWinProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.lpszClassName = OP_MSG_WND_CLASS_NAME;
			RegisterClassExA(&wc);
		});

		m_msgWnd = CreateWindowExA(0, OP_MSG_WND_CLASS_NAME, "", 0,
			0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);
		KD_ASSERT(m_msgWnd != NULL);
		::SetWindowLongPtr(m_msgWnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(this));
	}

	void _platform_uninit() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		::timeEndPeriod(1);

		if (::IsWindow(m_msgWnd)) {
			::KillTimer(m_msgWnd, TIMER_DO_OPERATION);
			::DestroyWindow(m_msgWnd);
			m_msgWnd = NULL;
		}
	}

	bool _platform_wakeUp() {
		if (!IsWindow(m_msgWnd)) {
			KD_ASSERT(false);
			return false;
		}
		
		kd_op_debug("Wake up native-queue");
		return PostMessageA(m_msgWnd, WM_WAKEUP_QUEUE, NULL, NULL);
	}

	bool _platform_delayProcessOperations(long long fireTimeMs) {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		if (!IsWindow(m_msgWnd)) {
			KD_ASSERT(false);
			return false;
		}

		long long nowMs = now_time();
		UINT intervalMs = (fireTimeMs > nowMs) ? static_cast<UINT>(fireTimeMs - nowMs) : 0;

		// Restart timer
		KillTimer(m_msgWnd, TIMER_DO_OPERATION);
		SetTimer(m_msgWnd, TIMER_DO_OPERATION, intervalMs, NULL);
		kd_op_debug("Start timer: %lld", fireTimeMs);

		return true;
	}

protected:
	static LRESULT CALLBACK _msgWinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		OperationNativeQueue* queue = reinterpret_cast<OperationNativeQueue*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
		if (queue == nullptr) {
			return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
		}

		if (uMsg == WM_WAKEUP_QUEUE) {
			queue->_processOperations();
			return 0;
		}
		if (uMsg == WM_TIMER) {
			UINT_PTR timerId = (UINT_PTR)(wParam);
			if (timerId == TIMER_DO_OPERATION) {
				KillTimer(hwnd, timerId);
				queue->m_waitingDelayedFireTimeMs = 0;
				queue->_processOperations();
				return 0;
			}
		}

		return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

#elif defined(KD_OS_DARWIN) // Apple
protected:
	static constexpr CFTimeInterval kFarFutureDelaySec = 3153600000.0;
	CFRunLoopRef m_runLoop{ nullptr };
	CFRunLoopSourceRef m_wakeUpSource{ nullptr };
	CFRunLoopTimerRef m_delayTimer{ nullptr };

protected:
	void _platform_init() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		m_delayTimer = nullptr;

		m_runLoop = CFRunLoopGetCurrent();
		if (m_runLoop == nullptr) {
			return;
		}
		CFRetain(m_runLoop);

		// Create wake-up source
		CFRunLoopSourceContext sourceCtx;
		::memset(&sourceCtx, 0, sizeof(sourceCtx));
		sourceCtx.info = this;
		sourceCtx.perform = [](void* info) {
			if (info) {
				OperationNativeQueue* queue = (OperationNativeQueue*)info;
				queue->_processOperations();
			}
		};

		m_wakeUpSource = ::CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &sourceCtx);
		if (m_wakeUpSource) {
			::CFRunLoopAddSource(m_runLoop, m_wakeUpSource, kCFRunLoopCommonModes);
		}
	}

	void _platform_uninit() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();

		// Release timer
		if (m_delayTimer != nullptr) {
			if (m_runLoop) {
				CFRunLoopRemoveTimer(m_runLoop, m_delayTimer, kCFRunLoopCommonModes);
			}

			CFRunLoopTimerInvalidate(m_delayTimer);
			CFRelease(m_delayTimer);
			m_delayTimer = nullptr;
		}

		// Release wake-up source
		if (m_wakeUpSource != nullptr) {
			if (m_runLoop) {
				::CFRunLoopRemoveSource(m_runLoop, m_wakeUpSource, kCFRunLoopCommonModes);
			}
			::CFRelease(m_wakeUpSource);
			m_wakeUpSource = nullptr;
		}

		// Release run loop
		if (m_runLoop) {
			CFRelease(m_runLoop);
			m_runLoop = nullptr;
		}
	}

	bool _platform_wakeUp() {
		if (m_runLoop == nullptr || m_wakeUpSource == nullptr) {
			KD_ASSERT(false);
			return false;
		}

		::CFRunLoopSourceSignal(m_wakeUpSource);
		CFRunLoopWakeUp(m_runLoop);
		return true;
	}

	bool _platform_delayProcessOperations(long long fireTimeMs) {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		if (!m_runLoop) {
			KD_ASSERT(false);
			return false;
		}

		// Calc fire date
		auto nowMs = now_time();
		uint32_t intervalMs = (fireTimeMs > nowMs) ? static_cast<uint32_t>(fireTimeMs - nowMs) : 0;
		CFTimeInterval delaySec = static_cast<CFTimeInterval>(intervalMs) / 1000.0;
		CFAbsoluteTime nextFireTime = CFAbsoluteTimeGetCurrent() + delaySec;
		kd_op_debug("Start timer: %lld", fireTimeMs);

		// Create or update timer
		if (!m_delayTimer) {
			CFRunLoopTimerContext context = { 0, this, nullptr, nullptr, nullptr };
			m_delayTimer = CFRunLoopTimerCreate(
				kCFAllocatorDefault,
				nextFireTime,
				kFarFutureDelaySec, 
				0, 
				0, 
				[](CFRunLoopTimerRef timer, void* info) {
					auto* q = reinterpret_cast<OperationNativeQueue*>(info);
					if (q) {
						q->m_waitingDelayedFireTimeMs = 0;
						q->_processOperations();
					}
				},
				&context
			);
			if (!m_delayTimer) {
				return false;
			}

			CFRunLoopAddTimer(m_runLoop, m_delayTimer, kCFRunLoopCommonModes);
		} else {
			CFRunLoopTimerSetNextFireDate(m_delayTimer, nextFireTime);
		}

		return true;
	}

#elif defined(KD_OS_ANDROID) // Android
protected:
	ALooper* m_aLooper{ nullptr };
	int m_wakeFd[2]{ -1, -1 };
	int m_timerFd{ -1 };

	enum class FdType {
		WakeUp = 1,
		Timer = 2
	};

	struct LooperFdContext {
		OperationNativeQueue* queue;
		FdType type;
	};

	LooperFdContext m_wakeContext{ nullptr, FdType::WakeUp };
	LooperFdContext m_timerContext{ nullptr, FdType::Timer };

protected:
	void _platform_init() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();

		m_wakeContext.queue = this;
		m_timerContext.queue = this;

		// Init looper
		m_aLooper = ALooper_forThread();
		if (m_aLooper == nullptr) {
			m_aLooper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACK);
		}
		if (m_aLooper == nullptr) {
			KD_ASSERT_M(false, "ALooper of current thread must be existing");
			return;
		}
		ALooper_acquire(m_aLooper);

		// Register wake up fd into looper
		if (pipe2(m_wakeFd, O_CLOEXEC | O_NONBLOCK) == 0) {
			int ret = ALooper_addFd(m_aLooper, m_wakeFd[0], ALOOPER_POLL_CALLBACK,
				ALOOPER_EVENT_INPUT, &OperationNativeQueue::_looperCallback, &m_wakeContext);
			if (ret == -1) {
				close(m_wakeFd[0]);
				close(m_wakeFd[1]);
				m_wakeFd[0] = -1;
				m_wakeFd[1] = -1;
			}
		}
	}

	void _platform_uninit() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		if (m_aLooper) {
			if (m_wakeFd[0] != -1) {
				ALooper_removeFd(m_aLooper, m_wakeFd[0]);
				close(m_wakeFd[0]);
				close(m_wakeFd[1]);
				m_wakeFd[0] = -1;
				m_wakeFd[1] = -1;
			}

			if (m_timerFd != -1) {
				ALooper_removeFd(m_aLooper, m_timerFd);
				close(m_timerFd);
				m_timerFd = -1;
			}

			ALooper_release(m_aLooper);
			m_aLooper = nullptr;
		}
	}

	bool _platform_wakeUp() {
		if (m_aLooper == nullptr || m_wakeFd[1] == -1) {
			KD_ASSERT(false);
			return false;
		}

		kd_op_debug("Wake up native-queue");
		char ch = 1;
		return write(m_wakeFd[1], &ch, 1) == 1;
	}

	bool _platform_delayProcessOperations(long long fireTimeMs) {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		if (m_aLooper == nullptr) {
			KD_ASSERT(false);
			return false;
		}

		// Create timer
		if (m_timerFd == -1) {
			// Register timer fd into looper
			m_timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
			if (m_timerFd == -1) {
				return false;
			}

			int ret = ALooper_addFd(m_aLooper, m_timerFd, ALOOPER_POLL_CALLBACK,
				ALOOPER_EVENT_INPUT, &OperationNativeQueue::_looperCallback, &m_timerContext);
			if (ret == -1) {
				close(m_timerFd);
				m_timerFd = -1;
				return false;
			}
		}

		// Start timer
		auto nowMs = now_time();
		uint32_t intervalMs = (fireTimeMs > nowMs) ? static_cast<uint32_t>(fireTimeMs - nowMs) : 0;

		struct itimerspec timerSpec;
		memset(&timerSpec, 0, sizeof(timerSpec));
		timerSpec.it_value.tv_sec = intervalMs / 1000;
		timerSpec.it_value.tv_nsec = (intervalMs % 1000) * 1000000LL;

		// Setting Linux's timerfd to all zeros will cause the timer to shut down.
		// To make it trigger immediately at the next tick, we give it a tiny 1 ms delay.
		if (timerSpec.it_value.tv_sec == 0 && timerSpec.it_value.tv_nsec == 0) {
			timerSpec.it_value.tv_nsec = 1000000; // 1ms
		}

		auto ret = timerfd_settime(m_timerFd, 0, &timerSpec, nullptr);
		kd_op_debug("Start timer: %lld, ret = %lld", fireTimeMs, (long long)ret);
		return (ret != -1);
	}

protected:
	static int _looperCallback(int fd, int events, void* data) {
		auto* ctx = reinterpret_cast<LooperFdContext*>(data);
		if (!ctx || !ctx->queue) {
			KD_ASSERT(false);
			return 1;
		}

		OperationNativeQueue* q = ctx->queue;
		KD_ASSERT_M(q->isQueueThread(), "Callback must be in thread of queue");
		
		if (ctx->type == FdType::WakeUp) {
			// Consume the data in wake up fd
			ssize_t ret = -1;
			char buf[256];
			do {
				ret = read(fd, buf, sizeof(buf));
			} while (ret > 0 || (ret == -1 && errno == EINTR));

			q->_processOperations();
		} else if (ctx->type == FdType::Timer) {
			// Consume the timer's expiration count
			uint64_t exp;
			read(fd, &exp, sizeof(exp));

			q->m_waitingDelayedFireTimeMs = 0;
			q->_processOperations();
		}

		// Returning 1 indicates that the file descriptor (FD) will continue 
		// to be monitored and will not be removed from the Looper.
		return 1;
	}

#elif defined(KD_HAS_GLIB) // GLib
protected:
	bool m_mainContextOwned{ false };
	GMainContext* m_mainContext{ nullptr };
	GSource* m_timerSource{ nullptr };
	
protected:
	void _platform_init() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();

		m_timerSource = nullptr;
		m_mainContext = g_main_context_get_thread_default();
		if (m_mainContext == nullptr) {
			m_mainContext = g_main_context_new();
			m_mainContextOwned = true;
			g_main_context_push_thread_default(m_mainContext);
		} else {
			g_main_context_ref(m_mainContext);
		}
	}

	void _platform_uninit() {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();

		if (m_mainContext) {
			if (m_timerSource != nullptr) {
				g_source_destroy(m_timerSource);
				g_source_unref(m_timerSource);
				m_timerSource = nullptr;
			}

			if (m_mainContextOwned) {
				g_main_context_pop_thread_default(m_mainContext);
			}
			g_main_context_unref(m_mainContext);
			m_mainContext = nullptr;
		}
	}

	bool _platform_wakeUp() {
		if (!m_mainContext) {
			KD_ASSERT(false);
			return false;
		}

		kd_op_debug("Wake up native-queue");
		g_main_context_ref(m_mainContext);
		g_main_context_invoke_full(
			m_mainContext,
			G_PRIORITY_DEFAULT_IDLE,
			[](gpointer data) -> gboolean {
				auto* q = reinterpret_cast<OperationNativeQueue*>(data);
				if (q) {
					q->_processOperations();
				}
				return G_SOURCE_REMOVE;
			},
			this,
			[](gpointer data) {
				OperationNativeQueue* q = reinterpret_cast<OperationNativeQueue*>(data);
				if (q) {
					g_main_context_unref(q->m_mainContext);
				}
			}
		);

		return true;
	}

	bool _platform_delayProcessOperations(long long fireTimeMs) {
		KD_REQUIRE_CREATE_NATIVE_QUEUE_THREAD();
		if (!m_mainContext) {
			KD_ASSERT(false);
			return false;
		}

		// Kill old timer
		if (m_timerSource != nullptr) {
			g_source_destroy(m_timerSource);
			g_source_unref(m_timerSource);
			m_timerSource = nullptr;
		}

		// Start new timer
		kd_op_debug("Start timer: %lld", fireTimeMs);
		auto nowMs = now_time();
		guint intervalMs = (fireTimeMs > nowMs) ? static_cast<guint>(fireTimeMs - nowMs) : 0;

		m_timerSource = g_timeout_source_new(intervalMs);
		if (!m_timerSource) {
			return false;
		}

		g_source_ref(m_timerSource);
		g_source_set_callback(m_timerSource, [](gpointer data) -> gboolean {
			auto* q = reinterpret_cast<OperationNativeQueue*>(data);
			if (!q) {
				return G_SOURCE_REMOVE;
			}

			if (q->m_timerSource != nullptr) {
				g_source_unref(q->m_timerSource);
				q->m_timerSource = nullptr;
			}

			q->m_waitingDelayedFireTimeMs = 0;
			q->_processOperations();

			return G_SOURCE_REMOVE;
		}, this, nullptr);

		g_source_attach(m_timerSource, m_mainContext);
		return true;
	}

#else
protected:
	void _platform_init() { KD_ASSERT_M(false, KD_OP_UNSUPPORTED_OS); };
	void _platform_uninit() { KD_ASSERT_M(false, KD_OP_UNSUPPORTED_OS); };
	bool _platform_wakeUp() { KD_ASSERT_M(false, KD_OP_UNSUPPORTED_OS); return false;};
	bool _platform_delayProcessOperations(long long) { KD_ASSERT_M(false, KD_OP_UNSUPPORTED_OS); return false;};
#endif // KD_OS_WIN

}; // OperationNativeQueue


// ======================================
// OperationMainQueue
// ======================================
class OperationMainQueue : public OperationNativeQueue {
	KD_DISABLE_COPY(OperationMainQueue);
	KD_DISABLE_MOVE(OperationMainQueue);

public: // static
	static OperationMainQueue& queue() {
		static OperationMainQueue* inst = nullptr;

		static std::once_flag once_flag_;
		std::call_once(once_flag_, [] {
			inst = new OperationMainQueue();
			KD_LEAKY_SINGLETON_DEFINE(inst);

#ifdef KD_OS_OHOS
			inst->_init(uv_default_loop());
#else
			inst->_init();
#endif
		});

		return *inst;
	}

	static void startup() {
		static std::once_flag once_flag_;
		std::call_once(once_flag_, [] {
			// Create main queue on start-up thread
			OperationMainQueue& q = queue();
			(void)q;
		});
	}

	static void shutdown() {
		KD_REQUIRE_MAIN_THREAD();
		// Nothing to do now.
	}

	static bool isMainThread() {
		return queue().isQueueThread();
	}

	static std::thread::id mainThreadId() {
		return queue().queueThreadId();
	}

public:
	uint64_t addOperation(std::function<void()> op, int delayMs = 0) {
		return OperationNativeQueue::addOperation(std::move(op), delayMs);
	}

	void removeOperation(uint64_t oid) {
		OperationNativeQueue::removeOperation(oid);
	}

	void cancelAllOperations() {
		OperationNativeQueue::cancelAllOperations();
	}

private:
	OperationMainQueue() : OperationNativeQueue() {
	}
};

// PerformOnMainThread
inline void PerformOnMainThread(std::function<void()> op, int delayMs = 0) {
	if (OperationMainQueue::isMainThread()) {
		op();
	} else {
		OperationMainQueue::queue().addOperation(std::move(op), delayMs);
	}
}

__NAMESPACE_KD_END
