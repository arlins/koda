/** **********************************************************
	Created by: Arlin (arlins.dps@gmail.com).

	This module  implements a Cross-Platform High-precision Task Scheduling System
	for delayed task. According to tests, this scheduler has an accuracy of 1 ~ 5 milliseconds.
	It provides two specialized schedulers for handling delayed tasks:

	1. MainDelayScheduler:
		- The scheduler implements running delayed tasks on the main thread.
			and it can be used from any thread

		- It uses a background thread to handle the timing, and then throws the delayed task
			back to the main thread for execution. In actual testing, it is more stable and performs
			better than using a Timer on the main thread.

	2. BackgroundDelayScheduler:
		- Runs tasks on a dedicated high-priority background thread (OperationQueue).
		   It implements running delayed tasks using condition_variable::wait_until for precise
		   sleep/wake cycles in a background thread. All tasks share an background thread,
		   and it can be used from any thread

		- Once a BackgroundDelayScheduler is created, stop must be called when
			it is no longer in use; otherwise, the memory of the BackgroundDelayScheduler
			will not be released, leading to internal leaks.

	[!] Important Note:
	MainDelayScheduler is based on OperationMainQueue, before using MainDelayScheduler,
	please be sure to initialize OperationMainQueue using OperationMainQueue::startup()
	when entering the program, and clean it up using OperationMainQueue::shutdown()
	when exiting the program.

	[!] Regarding task cancellation
	This library does not support canceling tasks.
	First, tasks execute asynchronously. While it's possible to
	cancel an task by removing it from the waiting queue,
	it's impossible to cancel if the task is currently executing.

	Therefore, ensure that the task is handled safely when it's executed.
	If the task shouldn't be executed, you should handle invalid cases internally
	within the task.

	If your use case is single-threaded (such as the UI main thread), [TrackPointer]
	in this library is a good tool to ensure safe usage. For multi-threaded scenarios,
	please use STL's std::weak_ptr and std::shared_ptr.
 ********************************************************** */

#pragma once

#include <queue>
#include <chrono>
#include <functional>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include "koda/kd_global.h"
#include "koda/async/kd_operation.h"
#include "koda/base/kd_random.h"

#if defined(KD_OS_WIN)
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

#ifdef KD_DEBUG
// #define KD_DELAY_ENABLE_DEBUG
#endif

#ifdef KD_DELAY_ENABLE_DEBUG
#define kd_delay_debug(fmt, ...) printf("[DELAY] " fmt "\n", ##__VA_ARGS__)
#else
#define kd_delay_debug(fmt, ...) do {} while(0);
#endif // KD_DELAY_ENABLE_DEBUG


__NAMESPACE_KD_BEGIN

// =====================================
// BackgroundDelayScheduler
// This scheduler implements running delayed tasks 
// in a background thread. All tasks share an background thread, 
// and it can be used from any thread
// =====================================
class BackgroundDelayScheduler : public std::enable_shared_from_this<BackgroundDelayScheduler> {
	// TaskNode
	struct TaskNode {
		std::chrono::steady_clock::time_point expire;
		std::function<void()> task;

		// Min-heap comparator: earliest expire time at the top
		struct Comparator {
			bool operator()(const std::shared_ptr<TaskNode>& lhs, const std::shared_ptr<TaskNode>& rhs) const {
				return lhs->expire > rhs->expire;
			}
		};
	};

public:
	static std::shared_ptr<BackgroundDelayScheduler> defaultScheduler() {
		static std::shared_ptr<BackgroundDelayScheduler>* inst;
		static std::once_flag once_flag_;

		std::call_once(once_flag_, [] {
			inst = new std::shared_ptr<BackgroundDelayScheduler>(new BackgroundDelayScheduler());
			KD_LEAKY_SINGLETON_DEFINE(inst);
			(*inst)->_init(1, ThreadPriority::Low);
		});

		return *inst;
	}

	static std::shared_ptr<BackgroundDelayScheduler> createShared(unsigned int threadCount = 1,
		ThreadPriority priority = ThreadPriority::Low) {
		std::shared_ptr<BackgroundDelayScheduler> inst(new BackgroundDelayScheduler());
		inst->_init(threadCount, priority);
		kd_delay_debug("BackgroundDelayScheduler <%s> created", inst->m_name.c_str());
		return inst;
	}

private:
	BackgroundDelayScheduler() {
	}

	void _init(unsigned int threadCount, ThreadPriority priority) {
		threadCount = (threadCount > 20 ? 20 : threadCount);
		m_name = "kd_delayscheduler_" + random::generateRandomString(6);
		auto queue = OperationQueue::createShared(threadCount, priority, m_name);

		// We hold the shared_ptr of `this` until the thread exits, 
		// so this scheduler can be used safely throughout 
		// the entire worker thread lifecycle
		queue->addOperation([this, self = shared_from_this()]() {
			this->_runLoop(self);
		});
		queue->setAutoDestroyedAfterAllOpsDone(true);
	}

public:
	~BackgroundDelayScheduler() {
		stop();
		kd_delay_debug("~BackgroundDelayScheduler <%s>", m_name.c_str());
	}

public:
	void stop() {
		{
			std::unique_lock<std::mutex> lock(m_mtx);
			m_stop = true;
		}
		m_cv.notify_one();
	}

	void post(int delayMs, std::function<void()> task) {
		if (!task) {
			return;
		}

		delayMs = (delayMs < 0 ? 0 : delayMs);
		auto node = ::kd::make_shared<TaskNode>();
		node->expire = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
		node->task = std::move(task);

		bool notifyNeeded = false;
		{
			std::lock_guard<std::mutex> lock(m_mtx);
			if (m_tasks.size() > 0) {
				auto topNode = m_tasks.top();
				if (topNode->expire > node->expire) {
					notifyNeeded = true;
				}
			} else {
				notifyNeeded = true;
			}
			m_tasks.push(std::move(node));
		}

		if (notifyNeeded) {
			m_cv.notify_one();
		}
	}

private:
	static void _runLoop(std::shared_ptr<BackgroundDelayScheduler> scheduler) {
		if (scheduler == nullptr) {
			KD_ASSERT_M(false, "Scheduler can not be empty");
			return;
		}

		kd_delay_debug("BackgroundDelayScheduler <%s> start", scheduler->m_name.c_str());

#if defined(KD_OS_WIN)
		// Improve Windows timing accuracy
		::timeBeginPeriod(1);
#endif

		// Currently, tasks are retrieved and executed one at a time. 
		// A solution for batch retrieval and execution of tasks has been tested.
		// While this approach improves throughput, it causes a larger difference 
		// between the actual and expected execution times of tasks.This is because 
		// batch execution increases the waiting time for all subsequent tasks, 
		// meaning the real-time performance of task execution is slightly 
		// worse than the current solution.

		// Do the task run loop
		while (!scheduler->m_stop) {
			std::unique_lock<std::mutex> lock(scheduler->m_mtx);
			if (scheduler->m_tasks.empty()) {
				scheduler->m_cv.wait(lock, [scheduler] {
					return scheduler->m_stop || !scheduler->m_tasks.empty(); });

				if (scheduler->m_stop) {
					break; // Quit
				}
			} else {
				auto now = std::chrono::steady_clock::now();
				auto topTaskExpire = scheduler->m_tasks.top()->expire;

				if (topTaskExpire <= now) {
					std::shared_ptr<TaskNode> node = scheduler->m_tasks.top();
					scheduler->m_tasks.pop();
					lock.unlock(); // Unlock the mutex before running task

					// Run task
					if (node && node->task) {
						try {
							node->task();
						} catch (...) {
							kd_delay_debug("Background delayed task runtime error");
						}
					}
				} else {
					scheduler->m_cv.wait_until(lock, topTaskExpire, [scheduler] {
						return scheduler->m_stop || !scheduler->m_tasks.empty(); });

					if (scheduler->m_stop) {
						break;  // Quit
					}
				}
			}
		}

#if defined(KD_OS_WIN)
		::timeEndPeriod(1);
#endif
		kd_delay_debug("BackgroundDelayScheduler <%s> exits", scheduler->m_name.c_str());
	}

private:
	// Min-heap: the task with earliest expire time is at the top of the heap.
	std::priority_queue<std::shared_ptr<TaskNode>,
		std::vector<std::shared_ptr<TaskNode>>,
		TaskNode::Comparator> m_tasks;
	std::mutex m_mtx;
	std::condition_variable m_cv;
	std::atomic<bool> m_stop{ false };
	std::string m_name;
};

// ===================================
// MainDelayScheduler
// This scheduler implements running delayed tasks 
// on the main thread. It can be used from any thread
// ===================================
class MainDelayScheduler {
public:
	static MainDelayScheduler& instance() {
		static MainDelayScheduler* inst = new MainDelayScheduler();
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

	void post(int delayMs, std::function<void()> task) {
		if (!task) {
			return;
		}

		if (delayMs <= 1) {
			OperationMainQueue::queue().addOperation(std::move(task));
			return;
		}

		m_delayScheduler->post(delayMs, [task_ = std::move(task)]() {
			OperationMainQueue::queue().addOperation(std::move(task_));
		});
	}

private:
	MainDelayScheduler() {
		m_delayScheduler = BackgroundDelayScheduler::createShared();
	};

	~MainDelayScheduler() {
		m_delayScheduler->stop();
	}

	std::shared_ptr<BackgroundDelayScheduler> delayScheduler() {
		return m_delayScheduler;
	}

private:
	std::shared_ptr<BackgroundDelayScheduler> m_delayScheduler{ nullptr };
};

__NAMESPACE_KD_END