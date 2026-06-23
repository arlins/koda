#pragma once
#include <iostream>
#include <thread>
#include <string>
#include <chrono>
#include <atomic>
#include <vector>
#include <numeric>
#include <algorithm>
#include "koda/base/kd_utils.h"
#include "koda/base/kd_random.h"
#include "koda/async/kd_delayscheduler.h"

using namespace kd;

//
static std::mutex kd_test_delay_debug_mtx;
#define kd_test_delay_debug1(fmt, ...) \
printf("[DELAY_TEST] [%llu] " fmt "\n", kd::now_time(), ##__VA_ARGS__);

#define kd_test_delay_debug2(fmt, ...) \
{	std::lock_guard<std::mutex> lock(kd_test_delay_debug_mtx); \
	kd_test_delay_debug1("[%s] " fmt, kd::this_thread_id().c_str(), ##__VA_ARGS__); }

// =============================
// Test Cases
// =============================
namespace kd_delay_test {

// Test main tasks
static void test_mainDelayUsage() {
	kd_test_delay_debug1("Start testing delayed tasks on main\n");

	using DelayScheduler = MainDelayScheduler;
	const char* name = "Main Task";
	long long now;

	int delay = 2000;
	now = kd::now_time();
	DelayScheduler::instance().post(delay, [=] {
		kd_test_delay_debug1("%s executed, delay = %d ms, cost = %llu ms", 
			name, delay, kd::now_time()-now);
	});

	delay = 500;
	now = kd::now_time();
	DelayScheduler::instance().post(delay, [=] {
		kd_test_delay_debug1("%s executed, delay = %d ms, cost = %llu ms", 
			name, delay, kd::now_time() - now);
	});

	delay = 1000;
	now = kd::now_time();
	DelayScheduler::instance().post(delay, [=] {
		kd_test_delay_debug1("%s executed, delay = %d ms, cost = %llu ms", 
			name, delay, kd::now_time() - now);
	});

	delay = 3500;
	now = kd::now_time();
	DelayScheduler::instance().post(delay, [=] {
		kd_test_delay_debug1("%s executed, delay = %d ms, cost = %llu ms", 
			name, delay, kd::now_time() - now);
	});

	// Zero Delay Tasks
	for (int i = 1; i <= 10; ++i) {
		delay = 0;
		now = kd::now_time();
		DelayScheduler::instance().post(delay, [=] {
			kd_test_delay_debug1("%s zero-delayed-task-%d executed, delay = %d ms, cost = %llu ms", 
				name, i, delay, kd::now_time() - now);
		});
	}

	// Cross-Thread Post to main
	std::thread([=] {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		int delay = 100;
		auto now = kd::now_time();
		DelayScheduler::instance().post(delay, [=] {
			kd_test_delay_debug1("%s from new thread executed, delay = %d ms, cost = %llu ms", 
				name, delay, kd::now_time() - now);
		});
	}).detach();
}

// Test posting main-delayed-tasks from main
static void test_mainDelayFromMain(int taskCount, std::function<void()> allTaskDone) {
	kd_test_delay_debug1("Start testing main-delayed-tasks from main");
	
	struct Context {
		std::function<void()> task;
		long long start_time;
		int max_step = 200;
		int step;
		int delay;
		std::vector<long long> time_diffs;
	};

	
	auto ctx = std::make_shared<Context>();
	ctx->start_time = kd::now_time();
	ctx->delay = kd::random::generateUInt(30, 100);

	ctx->task = [=]() {
		if ((++(ctx->step)) > ctx->max_step) {
			long long sum_diff = std::accumulate(ctx->time_diffs.begin(), ctx->time_diffs.end(), 0LL);
			auto max_diff = *std::max_element(ctx->time_diffs.begin(), ctx->time_diffs.end());
			auto min_diff = *std::min_element(ctx->time_diffs.begin(), ctx->time_diffs.end());

			printf("\n");
			kd_test_delay_debug1("Finish testing main-delayed-tasks from main");
			kd_test_delay_debug1("Avg Diff:     %lld ms", sum_diff / (long long)ctx->time_diffs.size());
			kd_test_delay_debug1("Max Diff:     %lld ms", max_diff);
			kd_test_delay_debug1("Min Diff:     %lld ms", min_diff);

			return;
		}

		// Cost is affected by delays and waiting times. 
		long long cost = kd::now_time() - ctx->start_time;
		ctx->time_diffs.push_back(cost - ctx->delay);
		kd_test_delay_debug1("Delayed task %d (%d) done, delay = %d ms, cost = %llu ms",
			ctx->step, ctx->max_step, ctx->delay, cost);

		ctx->delay = kd::random::generateUInt(30, 100);
		ctx->start_time = kd::now_time();
		kd::MainDelayScheduler::instance().post(ctx->delay, ctx->task);
	};

	// Fire
	kd::MainDelayScheduler::instance().post(ctx->delay, ctx->task);
}

// Test posting main-delayed-tasks from thread
static void test_mainDelayFromThread(int taskCountPerThread = 10000,
	int threadCount = 4, std::function<void()> allTaskDone = nullptr) {
	using namespace std::chrono;

	struct TestContext {
		int totalTaskCount;
		int threadCount;
		std::atomic<int> completedCount{ 0 };
		std::vector<long long> delayDiffTimes;
		long long startTime;

		TestContext(int perThread, int tCount)
			: threadCount(tCount) {
			totalTaskCount = perThread * tCount;
			delayDiffTimes.reserve(totalTaskCount);
			startTime = kd::now_time();
		}
	};

	auto ctx = std::make_shared<TestContext>(taskCountPerThread, threadCount);

	std::thread([ctx, taskCountPerThread, allTaskDone]() {
		using namespace std::chrono;
		kd_test_delay_debug1("Start testing main-delayed-tasks from threads");
		kd_test_delay_debug1("taskCountPerThread: %d, threadCount: %d",
			taskCountPerThread, ctx->threadCount);

		std::vector<std::thread> producers;
		for (int t = 0; t < ctx->threadCount; ++t) {
			producers.emplace_back([ctx, taskCountPerThread]() {
				for (int i = 0; i < taskCountPerThread; ++i) {
					int delay = kd::random::generateUInt(5, 500);
					auto expectedTime = steady_clock::now() + milliseconds(delay);

					// task
					auto task = [ctx, expectedTime]() {
						auto now = steady_clock::now();
						auto diff = duration_cast<milliseconds>(now - expectedTime).count();
						ctx->delayDiffTimes.push_back(diff);
						ctx->completedCount++;
						// kd_test_delay_debug2("Task done %d\n", ctx->completedCount.load());
					};

					kd::MainDelayScheduler::instance().post(delay, task);
				}
			});
		}

		// Wait for all threads
		for (auto& p : producers) p.join();
		kd_test_delay_debug1("All %d tasks have been posted.", ctx->totalTaskCount);

		while (ctx->completedCount < ctx->totalTaskCount) {
			std::this_thread::sleep_for(milliseconds(100));
			if (kd::now_time() - ctx->startTime > 10000) {
				kd_test_delay_debug1("Timeout error");
				break;
			}
		}

		// Output
		auto totalElapsed = kd::now_time() - ctx->startTime;
		long long sum_diff = std::accumulate(ctx->delayDiffTimes.begin(), ctx->delayDiffTimes.end(), 0LL);
		auto max_diff = *std::max_element(ctx->delayDiffTimes.begin(), ctx->delayDiffTimes.end());
		auto min_diff = *std::min_element(ctx->delayDiffTimes.begin(), ctx->delayDiffTimes.end());

		printf("\n");
		kd_test_delay_debug1("Finish testing main-delayed-tasks from threads");
		kd_test_delay_debug1("Completed:     %d / %d", ctx->completedCount.load(), ctx->totalTaskCount);
		kd_test_delay_debug1("Total Time:    %lld ms", totalElapsed);
		kd_test_delay_debug1("Avg Delay:     %lld ms", sum_diff / (long long)ctx->delayDiffTimes.size());
		kd_test_delay_debug1("Max Delay:     %lld ms", max_diff);
		kd_test_delay_debug1("Min Delay:     %lld ms", min_diff);

		if (allTaskDone) {
			kd::MainDelayScheduler::instance().post(1000, allTaskDone);
		}
	}).detach();
}

// Test background tasks
static void test_backgroundDelayUsage() {
	kd_test_delay_debug1("Start testing delayed tasks on background\n");

	using DelayScheduler = BackgroundDelayScheduler;
	const char* name = "BgTask";
	long long now;

	int delay = 2000;
	now = kd::now_time();
	DelayScheduler::defaultScheduler()->post(delay, [=] {
		kd_test_delay_debug1("%s executed, delay = %d ms, cost = %llu ms", 
			name, delay, kd::now_time() - now);
	});

	delay = 500;
	now = kd::now_time();
	DelayScheduler::defaultScheduler()->post(delay, [=] {
		kd_test_delay_debug1("%s executed, delay = %d ms, cost = %llu ms", 
			name, delay, kd::now_time() - now);
	});

	delay = 1000;
	now = kd::now_time();
	DelayScheduler::defaultScheduler()->post(delay, [=] {
		kd_test_delay_debug1("%s executed, delay = %d ms, cost = %llu ms", 
			name, delay, kd::now_time() - now);
	});

	delay = 3500;
	now = kd::now_time();
	DelayScheduler::defaultScheduler()->post(delay, [=] {
		long long workTimeMs = 3000;
		kd_test_delay_debug1("%s start to execute, delay = %d ms, work_time = %llu ms",
			name, delay, workTimeMs);

		//kd::thread_sleep(workTimeMs);
		auto _now = kd::now_time();
		while (true) {
			if (kd::now_time() - _now > workTimeMs) {
				break;
			}
		}

		kd_test_delay_debug1("%s finish to execute, delay = %d ms, cost = %llu ms", 
			name, delay, kd::now_time() - now);
	});

	// Zero Delay Tasks
	for (int i = 1; i <= 3; ++i) {
		delay = 0;
		now = kd::now_time();
		DelayScheduler::defaultScheduler()->post(delay, [=] {
			kd_test_delay_debug1("%s zero-task-%d executed, delay = %d ms, cost = %llu ms", 
				name, i, delay, kd::now_time() - now);
		});
	}

	// Cross-Thread Post
	std::thread([=] {
		// std::this_thread::sleep_for(std::chrono::milliseconds(200));

		int delay = 100;
		auto now = kd::now_time();
		DelayScheduler::defaultScheduler()->post(delay, [=] {
			kd_test_delay_debug1("%s from new thread executed, delay = %d ms, cost = %llu ms", 
				name, delay, kd::now_time() - now);
		});
	}).detach();

	// One-time delayed task
	auto oneTimeDelay = kd::BackgroundDelayScheduler::createShared();
	oneTimeDelay->post(100, [=] {
		kd_test_delay_debug1("One-time delayed task done");
		oneTimeDelay->stop();
	});
	// Calling stop immediately will prevent the task from executing.
	// oneTimeDelay->stop();
}

// Background thread delay task stress test
static void test_backgroundDelayStress() {
	std::thread([] {
		using namespace std::chrono;

		const int thread_count = 2;
		const int task_count_pre_thread = 1000;
		const int task_count = task_count_pre_thread * thread_count;

		std::vector<long long> delayDiffTimes;
		delayDiffTimes.reserve(task_count);
		std::atomic<int> completed_tasks{ 0 };

		kd_test_delay_debug1("Starting posting bg-delayed-tasks from multi-threaded: %d", task_count);

		auto start_test_time = steady_clock::now();
		auto scheduler = kd::BackgroundDelayScheduler::createShared();

		// Concurrent multi-threaded posting of tasks
		std::vector<std::thread> injectors;
		for (int i = 0; i < thread_count; ++i) {
			injectors.emplace_back([&]() {
				for (int j = 0; j < task_count_pre_thread; ++j) {
					int delay = kd::random::generateUInt(5, 30);
					auto target_time = steady_clock::now() + milliseconds(delay);

					scheduler->post(delay, [&, target_time]() {
						auto now = steady_clock::now();
						auto diff = duration_cast<milliseconds>(now - target_time).count();

						delayDiffTimes.push_back(std::abs(diff));
						completed_tasks++;
					});
				}
			});
		}

		for (auto& t : injectors) t.join();

		// Wait for all tasks done
		while (completed_tasks < task_count) {
			std::this_thread::sleep_for(milliseconds(50));
			if (duration_cast<seconds>(steady_clock::now() - start_test_time).count() > 10) {
				kd_test_delay_debug1("[ERROR] Test timeout!");
				break;
			}
		}

		// Output
		if (!delayDiffTimes.empty()) {
			auto totalElapsed = duration_cast<milliseconds>(steady_clock::now() - start_test_time).count();
			long long max_lat = *std::max_element(delayDiffTimes.begin(), delayDiffTimes.end());
			long long min_lat = *std::min_element(delayDiffTimes.begin(), delayDiffTimes.end());
			double sum_lat = std::accumulate(delayDiffTimes.begin(), delayDiffTimes.end(), 0.0);
			double avg_lat = sum_lat / delayDiffTimes.size();

			printf("\n");
			kd_test_delay_debug1("Finish posting bg-delayed-tasks from multi-threaded");
			kd_test_delay_debug1("Thread:          %d (%d)", thread_count, task_count_pre_thread);
			kd_test_delay_debug1("Completed:       %d / %d", completed_tasks.load(), task_count);
			kd_test_delay_debug1("Total Time:      %lld ms", totalElapsed);
			kd_test_delay_debug1("Average Delay:   %.2f ms", avg_lat);
			kd_test_delay_debug1("Max Delay:       %lld ms", max_lat);
			kd_test_delay_debug1("Min Delay:       %lld ms", min_lat);
			
		}

		// Short lifecycle creation and destruction
		printf("\n");
		kd_test_delay_debug1("Starting Lifecycle Stress Test...");
		for (int i = 0; i < 5; ++i) {
			auto temp_scheduler = kd::BackgroundDelayScheduler::createShared();
			temp_scheduler->post(5, []() {
				// task
			});

			// Destroy
			temp_scheduler->stop();
		}
		kd_test_delay_debug1("Lifecycle Test Finished (No Crash)");

		// Stop
		scheduler->stop();
		kd_test_delay_debug1("All Tests Completed");

	}).detach();
}

};
