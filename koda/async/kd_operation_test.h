// =====================================
// Test operation
// =====================================
#pragma once
#include <memory>
#include <deque>
#include "koda/base/kd_utils.h"
#include "koda/async/kd_operation.h"
#include "koda/base/kd_random.h"
#include "koda/async/kd_delayscheduler.h"

static std::mutex op_test_debug_mtx;
#define op_test_debug1(fmt, ...) \
{ printf("[OP_TEST] [%lld] [%s] " fmt "\n", kd::now_time(), kd::this_thread_id().c_str(), ##__VA_ARGS__);}

#define op_test_debug2(fmt, ...) \
{	std::lock_guard<std::mutex> lock(op_test_debug_mtx); \
    op_test_debug1(fmt, ##__VA_ARGS__); }

// kd_op_test
namespace kd_op_test {

// Test auto-destroyed queue
static void test_ops_destroyed_automatically() {
	int threadCount = 3;
	int taskCount = 5;
	int delayCount = 5;
	bool testCancelTask = true;

	auto queue = kd::OperationQueue::createShared(threadCount, kd::ThreadPriority::Normal, "queue1");
	op_test_debug2("Start testing auto-destroyed queue(%d): taskCount = %d, delayCount = %d",
		threadCount, taskCount, delayCount);
	
	std::vector<uint64_t> op_ids;
	std::vector<uint64_t> delayed_op_ids;

	// Normal tasks
	for (int i = 0; i < taskCount; ++i) {
		int workMs = 500 + 200 * i;
		auto now = kd::now_time();
		auto oid = queue->addOperation([=] {
			op_test_debug2("Task%d start (work %d ms, diff = %d ms)", i, workMs, kd::now_time() - now);
			kd::thread_sleep(workMs);
			op_test_debug2("Task%d done (work %d ms)", i, workMs);
		});
		op_test_debug2("Task%d (oid%lld) added (work %d ms)", i, oid, workMs);
		op_ids.push_back(oid);
	}

	// Add task by queue name
	auto queue_by_name = kd::OperationQueue::queueByName("queue1");
	if (queue_by_name) {
		queue_by_name->addOperation([=] {
			op_test_debug2("QueueName-Task1 done");
			queue_by_name->addOperation([] {
				op_test_debug2("QueueName-Task2 done");
			}, 100);
		});
	}

	// Delayed tasks
	for (int i = 0; i < delayCount; ++i) {
		int delayMs = (delayCount - i) * 1000;
		auto now = kd::now_time();
		auto oid = queue->addOperation([=] {
			op_test_debug2("DelayedTask%d done (delay %d ms, diff = %d ms)", i, delayMs, kd::now_time() - now - delayMs);
		}, delayMs);
		op_test_debug2("DelayedTask%d (oid%d) added (delay %d ms)", i, oid, delayMs);
		delayed_op_ids.push_back(oid);
	}

	// Cancel tasks
	if (testCancelTask) {
		std::thread([=] {
			kd::thread_sleep(800);
			//queue->cancelAllOperations();
			//op_test_debug2("Cancel all tasks");
			//return;

			if (op_ids.size() > 0) {
				auto oid = op_ids.back();
				queue->removeOperation(oid);
				op_test_debug2("Cancel Task: oid%lld", oid);
			}

			if (delayed_op_ids.size() > 0) {
				auto oid = delayed_op_ids.back();
				queue->removeOperation(oid);
				op_test_debug2("Cancel DelayedTask: oid%lld", oid);
			}
		}).detach();
	}

	// Wait for all tasks done
	std::thread([=] {
		op_test_debug2("Start waiting for all tasks done: %s", queue->name().c_str());
		queue->waitUntilAllOperationsAreFinished();
		op_test_debug2("All tasks done in queue: %s", queue->name().c_str());
	}).detach();

	// Setup auto-destroyed queue
	queue->setAutoDestroyedAfterAllOpsDone(true);
}

// Test manual-destroyed queue
static void test_queue_destroyed_manually() {
	int count = 5;
	auto queue = kd::OperationQueue::createShared(1);
	op_test_debug1("Start testing queue(1) destroyed manually with taskCount %d", count);
	
	for (int i = 0; i < count; ++i) {
		int ms = 1000 + 500 * i;
		op_test_debug2("Task-%d added (sleep %d ms)", i, ms);
		queue->addOperation([=] {
			if (i== 0) {
				op_test_debug2("Task-00001 added");
				queue->addOperation([]{
					op_test_debug2("Task-00001 done");
				}, 1000);
			}

			op_test_debug2("Task-%d start (sleep %d ms)", i, ms);
			kd::thread_sleep(ms);
			op_test_debug2("Task-%d done (sleep %d ms)", i, ms);

			if (i== 0) {
				op_test_debug2("Task-00002 added");
				queue->addOperation([]{
					op_test_debug2("Task-00002 done");
				}, 1000);
			}
		});
	}

	// Manually destroy the queue
	std::thread([queue] {
		kd::thread_sleep(800);
		op_test_debug2("Manually destroy the queue, only task 0 will be executed");
		queue->destroy();
		op_test_debug2("queue destroyed");
	}).detach();
}

// Test queue delay
static void test_queueRunDelay() {
	op_test_debug1("Starting OperationQueue run delay...");

	int threadCount = 3;
	int opsPerThread = 200;
	int totalOps = threadCount * opsPerThread;
	std::shared_ptr<std::atomic<int>> runTimeDiffs = std::make_shared<std::atomic<int>>(0);
	std::shared_ptr<std::atomic<int>> completionCount = std::make_shared<std::atomic<int>>(0);
	auto queue = kd::OperationQueue::createShared(3, kd::ThreadPriority::High, "StressQueue");

	// Add tasks
	for (int i = 0; i < threadCount; i++) {
		std::thread([=] {
			for (int i = 0; i < opsPerThread; ++i) {
				int delayAddMs = kd::random::generateUInt(10, 50);
				std::this_thread::sleep_for(std::chrono::milliseconds(delayAddMs));

				int delayMs = kd::random::generateUInt(0, 100);
				if (delayMs < 10) { delayMs = 0; }

				auto nowMs = kd::now_time();
				queue->addOperation([=] {
					runTimeDiffs->fetch_add(int(kd::now_time() - nowMs) - delayMs);
					(*completionCount)++;
				}, delayMs);
			}
		}).detach();
	}

	// Wait all tasks done
	std::thread([=] {
		auto start_wait = kd::now_time();
		while ((*completionCount).load() < totalOps) {
			op_test_debug1("Completion Count: %d / %d", (*completionCount).load(), totalOps);
			kd::thread_sleep(200);
			if (kd::now_time() - start_wait > 20000) {
				op_test_debug1("Wait timeout");
				break;
			}
		}
		op_test_debug1("Completion Count: %d / %d", (*completionCount).load(), totalOps);

		// Final
		printf("\n\n");
		op_test_debug1("Test Finished");
		op_test_debug1("Completion Count: %d / %d", (*completionCount).load(), totalOps);
		op_test_debug1("Run Time Diff: %.3f ms (%d / %d)", (*runTimeDiffs).load() / (double)totalOps, (*runTimeDiffs).load(), totalOps);
		printf("\n\n");
	}).detach();
}

// OperationQueue stress test
// threadCount=3, forever-until-run (10):
//		300
//		210, 201
//		111, 102, 120
//		021, 012, 003, 030
static void test_queueStress() {
	op_test_debug1("Starting OperationQueue Stress Test...");

	int threadCount = 3;
	int opsPerThread = 200;
	int totalOps = threadCount * opsPerThread;
	std::shared_ptr<std::atomic<long long>> runTimeDiffs = std::make_shared<std::atomic<long long>>(0);
	std::shared_ptr<std::atomic<int>> completionCount = std::make_shared<std::atomic<int>>(0);
	auto queue = kd::OperationQueue::createShared(3, kd::ThreadPriority::High, "StressQueue");

	// Add tasks
	for (int i = 0; i < threadCount; i++) {
		std::thread([=] {
			for (int i = 0; i < opsPerThread; ++i) {
				int delayAddMs = kd::random::generateUInt(10, 50);
				std::this_thread::sleep_for(std::chrono::milliseconds(delayAddMs));

				int delayMs = kd::random::generateUInt(0, 100);
				if (delayMs < 10) {
					delayMs = 0;
				}

				auto nowMs = kd::now_time();
				queue->addOperation([=] {
					runTimeDiffs->fetch_add(kd::now_time() - nowMs - delayMs);
					int workMs = kd::random::generateUInt(0, 50);
					if (workMs > 10) {
						std::this_thread::sleep_for(std::chrono::milliseconds(workMs));
					}
					(*completionCount)++;
				}, delayMs);
			}
		}).detach();
	}

	// Wait all tasks done
	std::thread([=] {
		auto start = std::chrono::high_resolution_clock::now();
		auto start_wait = kd::now_time();
		while ((*completionCount).load() < totalOps) {
			op_test_debug1("Completion Count: %d / %d", (*completionCount).load(), totalOps);
			kd::thread_sleep(200);
			if (kd::now_time() - start_wait > 20000) {
				op_test_debug1("Wait timeout");
				break;
			}
		}
		op_test_debug1("Completion Count: %d / %d", (*completionCount).load(), totalOps);

		// Final
		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> diff = end - start;
		printf("\n\n");
		op_test_debug1("Test Finished");
		op_test_debug1("Completion Count: %d / %d", (*completionCount).load(), totalOps);
		op_test_debug1("Delay diff: %d ms", (*runTimeDiffs).load() / totalOps);
		op_test_debug1("Time taken: %.3f seconds (Avg: %.6f s/op)", diff.count(), diff.count() / totalOps);
		printf("\n\n");

		// 030: all threads are waiting for delayed tasks
		int taskCount030 = 5;
		kd::thread_sleep(1000);

		for (int i = 0; i < taskCount030; i++) {
			kd::BackgroundDelayScheduler::defaultScheduler()->post(i * 10, [=] {
				int delayMs = (taskCount030 - i) * 1000;
				queue->addOperation([=] {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					op_test_debug1("030Task-%d (delay %d ms) done", i, delayMs);
				}, delayMs);

				if (i == taskCount030 - 1) {
					queue->setAutoDestroyedAfterAllOpsDone(true);
				}
			});
		}

	}).detach();
}

// Test main queue
static void test_mainQueue() {
	op_test_debug1("Start testing main queue usage: tasks order and delay tasks");

	// Test main-queue tasks order
	std::thread([] {
		int taskCount = 10;
		for (int i = 0; i < taskCount; ++i) {
			auto now = kd::now_time();
			kd::OperationMainQueue::queue().addOperation([=] {
				op_test_debug1("Perform main queue Task-%d (diff = %d ms)", i, kd::now_time() - now);
			});
		}

		// [1000, 2000, 3000, ...]
		int delayedTaskCount = 5;
		for (int i = 0; i < delayedTaskCount; ++i) {
			int delayMs = (delayedTaskCount - i) * 1000;
			auto now = kd::now_time();
			op_test_debug1("DelayTask-%d added (delay = %d ms)", i, delayMs);
			kd::OperationMainQueue::queue().addOperation([=] {
				op_test_debug1("DelayTask-%d done (delay = %d ms, diff = %d ms)", 
					i, delayMs, kd::now_time() - now - delayMs);
			}, delayMs);
		}

		// [1300, 3500]
		{
			std::thread([] {
				int sleepMs = 1200;
				int delayMs = 100;

				kd::thread_sleep(sleepMs);
				auto now = kd::now_time();
				op_test_debug1("DelayTask-%d added (delay = %d ms)", delayMs, delayMs);
				kd::OperationMainQueue::queue().addOperation([=] {
					op_test_debug1("DelayTask-%d done (delay = %d ms, total_delay = %d diff = %d ms)",
						delayMs, delayMs, sleepMs + delayMs, kd::now_time() - now - delayMs);

					{
						int delayMs2 = 2200; // 1300 + 2200 = 3500ms
						auto now = kd::now_time();
						op_test_debug1("DelayTask-%d added (delay = %d ms)", delayMs2, delayMs2);
						kd::OperationMainQueue::queue().addOperation([=] {
							op_test_debug1("DelayTask-%d done (delay = %d ms, total_delay = %d diff = %d ms)",
								delayMs2, delayMs2, sleepMs + delayMs + delayMs2, kd::now_time() - now - delayMs2);
						}, delayMs2);
					}
				}, delayMs);
			}).detach();
		}

		// [4500]
		{
			std::thread([] {
				int sleepMs = 2500;
				int delayMs = 2000;

				kd::thread_sleep(sleepMs);
				auto now = kd::now_time();
				op_test_debug1("DelayTask-%d added (delay = %d ms)", delayMs, delayMs);
				kd::OperationMainQueue::queue().addOperation([=] {
					op_test_debug1("DelayTask-%d done (delay = %d ms, total_delay = %d diff = %d ms)",
						delayMs, delayMs, sleepMs + delayMs, kd::now_time() - now - delayMs);
				}, delayMs);
			}).detach();
		}

		printf("\n");
	}).detach();
}

static void perform_operation(bool useNative, std::function<void()> task, int delayMs) {
	if (useNative) {
#if defined(KD_OS_DARWIN) && defined(KD_HAS_OBJC)
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(delayMs * NSEC_PER_MSEC)),
		dispatch_get_main_queue(), ^ {
			task();
		});
#else
		kd::OperationMainQueue::queue().addOperation(task, delayMs);
#endif
	} else {
		kd::OperationMainQueue::queue().addOperation(task, delayMs);
	}
}

// Test main queue ops stress from main thread
static void test_mainQueueStressFromMain(bool useNative = false) {
	op_test_debug1("Start testing main queue from main, native = %d", useNative);

	struct Context {
		std::function<void()> task;
		long long start_time;
		int max_step = 100;
		int step;
		int delay;
		std::vector<long long> time_diffs;
	};

	auto ctx = std::make_shared<Context>();

	ctx->start_time = kd::now_time();
	ctx->delay = kd::random::generateUInt(10, 100);
	ctx->task = [=]() {
		if (ctx->step >= ctx->max_step) {
			long long sum_diff = std::accumulate(ctx->time_diffs.begin(), ctx->time_diffs.end(), 0LL);
			auto max_diff = *std::max_element(ctx->time_diffs.begin(), ctx->time_diffs.end());
			auto min_diff = *std::min_element(ctx->time_diffs.begin(), ctx->time_diffs.end());

			printf("\n\n");
			op_test_debug1("Finish testing main queue from main, native = %d", useNative);
			op_test_debug1("Avg Diff:     %lld ms", sum_diff / (long long)ctx->time_diffs.size());
			op_test_debug1("Max Diff:     %lld ms", max_diff);
			op_test_debug1("Min Diff:     %lld ms", min_diff);

			return;
		}

		ctx->step ++;

		// Cost is affected by delays and waiting times. 
		long long cost = kd::now_time() - ctx->start_time;
		long long diffMs = cost - ctx->delay;
		ctx->time_diffs.push_back(diffMs);
		//op_test_debug1("Task %d (%d) done, delay = %d ms, diff = %lld ms", ctx->step, ctx->max_step, ctx->delay, diffMs);

		ctx->delay = kd::random::generateUInt(30, 100);
		ctx->start_time = kd::now_time();
		perform_operation(useNative, ctx->task, ctx->delay);
	};

	// Fire
	perform_operation(useNative, ctx->task, ctx->delay);

	// Checking
	std::thread([=]{
		auto checkingMs = kd::now_time();
		while (ctx->step < ctx->max_step) {
			op_test_debug1("Tasks processed: %d / %d ...", ctx->step, ctx->max_step);
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (kd::now_time() - checkingMs > 20000) {
				op_test_debug1("Wait timeout");
				break;
			}
		}
		op_test_debug1("Tasks processed: %d / %d ...", ctx->step, ctx->max_step);

	}).detach();
}

// Test main queue ops stress from threads
static void test_mainQueueStressFromThread(bool useNative = false) {
	op_test_debug1("Start testing main queue from threads, native = %d", useNative);

	std::thread([=] {
		const int threadCount = 4;
		const int opsPerThread = 100;
		const int totalOps = threadCount * opsPerThread;

		struct Context {
			std::vector<long long> time_diffs;
			std::atomic<int> processedCount{ 0 };
		};

		auto ctx = std::make_shared<Context>();

		auto performOperation = [=](int tid, int idx) {
			int delayMs = kd::random::generateUInt(0, 100);
			if (delayMs < 10) {
				delayMs = 0;
			}
			delayMs = 0;

			auto start = kd::now_time();
			//op_test_debug1("Thread%d - Task%d (%d) added, delay = %d ms", tid, idx, opsPerThread, delayMs);
			
			auto task = [=]{
				ctx->processedCount++;
				long long diff = kd::now_time() - start - delayMs;
				ctx->time_diffs.push_back(diff);
				// op_test_debug1("Thread%d - Task%d (%d) done, delay = %d ms, diff = %lld ms",
				//	tid, idx, opsPerThread, delayMs, diff);
			};

			perform_operation(useNative, task, delayMs);
		};

		// addOperation to the main thread from threads
		for (int t = 0; t < threadCount; ++t) {
			std::thread([=] {
				for (int i = 0; i < opsPerThread; ++i) {
					int sleepMs = kd::random::generateUInt(0, 100);
					if (sleepMs > 10) {
						kd::thread_sleep(sleepMs);
					}

					performOperation(t, i);
				}
				op_test_debug1("Thread %d finished dispatching all tasks", t);
			}).detach();
		}

		// Check processing
		auto checkingMs = kd::now_time();
		while (ctx->processedCount.load() < totalOps) {
			op_test_debug1("Tasks processed: %d / %d ...", ctx->processedCount.load(), totalOps);
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (kd::now_time() - checkingMs > 10000) {
				op_test_debug1("Wait timeout");
				break;
			}
		}
		op_test_debug1("Tasks processed: %d / %d ...", ctx->processedCount.load(), totalOps);

		// Final
		printf("\n");
		op_test_debug1("Finish testing main queue from threads,native = %d", useNative);
		if (ctx->time_diffs.size() > 0) {
			long long sum_diff = std::accumulate(ctx->time_diffs.begin(), ctx->time_diffs.end(), 0LL);
			long long max_diff = *std::max_element(ctx->time_diffs.begin(), ctx->time_diffs.end());
			long long min_diff = *std::min_element(ctx->time_diffs.begin(), ctx->time_diffs.end());

			op_test_debug1("Avg Diff:     %lld ms", sum_diff / (long long)ctx->time_diffs.size());
			op_test_debug1("Max Diff:     %lld ms", max_diff);
			op_test_debug1("Min Diff:     %lld ms", min_diff);
		} else {
			op_test_debug1("Error: empty time diffs");
		}

	}).detach();
}

}; // namespace op_test
