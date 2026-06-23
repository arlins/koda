#pragma once
#include <vector>
#include <map>
#include "koda/base/kd_str.h"
#include "koda/base/kd_utils.h"
#include "koda/async/kd_delayscheduler.h"
#include "koda/runloop/kd_runloop.h"

// debug
static std::mutex kd_runloop_test_mtx;
#define runloop_test_debug(fmt, ...) \
{ printf("[RL_TEST] [%lld] <%s> " fmt "\n", kd::now_time(), kd::this_thread_id().c_str(), ##__VA_ARGS__);}

#define runloop_test_debug2(fmt, ...) \
{	std::lock_guard<std::mutex> lock(kd_runloop_test_mtx); \
	runloop_test_debug(fmt, ##__VA_ARGS__); }

// kd_runloop_test
namespace kd_runloop_test {
struct TestConfig {
	int onceTaskCount{ 20 };
	int onceTaskTimeInterval{ 500 };

	int eventSignalCount{ 3 };
	int eventSignalTimeInterval{ 800 };
	bool manualResetEvent{ true };
	long long lastEventSignalTimeMs{0};

	int timerTaskCount{ 8 };
	int timerTaskIntervalMs{ 1000 };
	int removeTimerTaskDelayMs{ 2500 };
	int timerTaskMaxTimes{ 20 };

public:
	static std::shared_ptr<TestConfig> onceTasks() {
		auto cfg = std::make_shared<TestConfig>();
		cfg->onceTaskCount = 8;
		cfg->eventSignalCount = 0;
		cfg->timerTaskCount = 0;
		cfg->removeTimerTaskDelayMs = -1;

		return cfg;
	}

	static std::shared_ptr<TestConfig> eventNotifiers() {
		auto cfg = std::make_shared<TestConfig>();
		cfg->onceTaskCount = 0;
		cfg->eventSignalCount = 3;
		cfg->timerTaskCount = 0;
		cfg->removeTimerTaskDelayMs = -1;

		return cfg;
	}

	static std::shared_ptr<TestConfig> timerTasks() {
		auto cfg = std::make_shared<TestConfig>();
		cfg->onceTaskCount = 0;
		cfg->eventSignalCount = 0;
		cfg->timerTaskCount = 5;
		cfg->removeTimerTaskDelayMs = -1;

		return cfg;
	}

	static std::shared_ptr<TestConfig> allTasks() {
		auto cfg = std::make_shared<TestConfig>();
		cfg->onceTaskCount = 8;
		cfg->eventSignalCount = 3;
		cfg->timerTaskCount = 5;
		cfg->removeTimerTaskDelayMs = -1;

		return cfg;
	}

	void disableOnceTasks() {
		onceTaskCount = 0;
	}

	void disableTimerTasks() {
		timerTaskCount = 0;
	}

	void disableEventNotifiers() {
		eventSignalCount = 0;
	}
};

static int kMaxTime = std::numeric_limits<int>::max();

inline void do_runloop_testworks(std::thread::id tid, std::shared_ptr<TestConfig> cfg) {
	// Test once tasks
	if (cfg->onceTaskCount > 0) {
		for (int i = 0; i < cfg->onceTaskCount; i++) {
			kd::BackgroundDelayScheduler::defaultScheduler()->post(i * cfg->onceTaskTimeInterval, [=] {
				int delayMs = (i % 2 == 0 ? 0 : 300);
				auto nowMs = kd::now_time();

				auto action = [=] (std::thread::id from) {
					std::string name = (delayMs > 0 ? "_Delayed" : "");
					runloop_test_debug2("OnceTask_%d%s done, from = %s, delay = %d ms, diff = %lld ms", i, name.c_str(), kd::thread_id_str(from).c_str(),
						delayMs, kd::now_time() - nowMs - delayMs);

					if (i == cfg->onceTaskCount - 1) {
						runloop_test_debug2("All Once Tasks have been completed");
					}
				};

				if (kd::random::generateUInt(0,100) % 2 == 0) {
					// Submit tasks in other threads
					std::thread::id from = std::this_thread::get_id();
					kd::RunLoop::postTask(tid, [=] {
						action(from);
					}, delayMs);
				} else {
					// Post the task in the thread where the run loop resides.
					kd::RunLoop::postTask(tid, [=] {
						std::thread::id from = std::this_thread::get_id();
						kd::RunLoop::postTask(tid, [=] {
							action(from);
						}, delayMs);
					});
				}
				//runloop_test_debug2("OnceTask_%d added", i);
			});
		}
	}

	// Test timer tasks
	if (cfg->timerTaskCount > 0) {
		struct TimerContext {
			int idx;
			uint64_t timerId;
			long long lastFireTime;
			long long intervalMs;
			bool repeated{ false };

			int times{ 0 };
			int maxTimes{ 20 };
		};

		auto timerContextList = std::make_shared<std::map<uint64_t, TimerContext>>();

		// Add timer task
		for (int i = 0; i < cfg->timerTaskCount; i++) {
            uint32_t intervalMs = cfg->timerTaskIntervalMs * (i + 1);
			bool repeat = (i == 0 ? true : false);

			auto timerTaskId = kd::RunLoop::addTimer(tid, intervalMs, repeat, [=](int64_t timerId) {
				auto it = (*timerContextList).find(timerId);
				if (it != (*timerContextList).end()) {
					auto& ctx = (*it).second;
					ctx.times++;
					runloop_test_debug2("TimerTask_%d (%d) done, repeated = %d, interval = %d ms, diff = %d ms", timerId, ctx.times,
						repeat, int(intervalMs), int(kd::now_time() - ctx.lastFireTime - ctx.intervalMs));

					ctx.lastFireTime = kd::now_time();

					if (ctx.times >= ctx.maxTimes) {
						runloop_test_debug2("About to remove TimerTask_%d with max times (%d)", timerId, ctx.times);
						kd::RunLoop::removeTimer(tid, ctx.timerId);
					}
				} else {
					runloop_test_debug2("TimerTask_%d done, repeated = %d, interval = %lld ms (ctx error)", timerId, repeat, intervalMs);
				}
			});
			// kd_runloop_debug("TimerTask_%lld (index = %d) added", timerTaskId, i);
			(*timerContextList)[timerTaskId] = TimerContext{ i, timerTaskId, kd::now_time(), intervalMs, repeat, 0, cfg->timerTaskMaxTimes};
		}

		// Remove timer tasks
		if (!(*timerContextList).empty() && cfg->removeTimerTaskDelayMs > 0) {
			kd::BackgroundDelayScheduler::defaultScheduler()->post(cfg->removeTimerTaskDelayMs, [=] {
				int idx = -1;
				for (auto it = (*timerContextList).begin(); it != (*timerContextList).end(); it++) {
					auto& ctx = (*it).second;
					kd::RunLoop::removeTimer(tid, ctx.timerId);
				}
			});
		}
	}

	// Test EventNotifier
#ifdef KD_OS_WIN
	if (cfg->eventSignalCount > 0) {
		kd::RLEvent evHandle = CreateEventA(NULL, cfg->manualResetEvent, FALSE, NULL);
		auto times = std::make_shared<int>(0);

		// Add EventNotifier
		kd::RLEventNotifier eventNotifier(evHandle, kd::RLEventNotifier::NONE, [=](RLEvent handle, int type) {
			(*times)++;
			runloop_test_debug2("EventNotifier is signaled: times = %d, diff = %lld ms", (*times), kd::now_time() - cfg->lastEventSignalTimeMs);
			if (cfg->manualResetEvent) {
				ResetEvent(handle);
			}
		});
		kd::RunLoop::addEventNotifier(tid, eventNotifier);

		// Signal
		for (int i = 0; i < cfg->eventSignalCount; i++) {
			kd::BackgroundDelayScheduler::defaultScheduler()->post(300 + cfg->eventSignalTimeInterval * i, [=] {
				runloop_test_debug2("Signal EventNotifier, manualReset = %d", cfg->manualResetEvent);
				cfg->lastEventSignalTimeMs = kd::now_time();
				SetEvent(evHandle);
			});
		}

		// Remove EventNotifier
		int detchedDelay = cfg->eventSignalCount * cfg->eventSignalTimeInterval;
		kd::BackgroundDelayScheduler::defaultScheduler()->post(detchedDelay, [=] {
			runloop_test_debug2("Remove EventNotifier");
			kd::RunLoop::removeEventNotifier(tid, evHandle);
			CloseHandle(evHandle);
		});
	}

#elif defined(KD_OS_UNIX)
	if (cfg->eventSignalCount > 0) {
		int fds[2];
		if (pipe(fds) != 0) {
			runloop_test_debug2("Failed to create pipe");
			return;
		}
		fcntl(fds[0], F_SETFL, O_NONBLOCK);
		fcntl(fds[1], F_SETFL, O_NONBLOCK);

		kd::RLEvent evHandle = fds[0]; // Read fd
		auto times = std::make_shared<int>(0);

		// Add EventNotifier
        kd::RLEventNotifier eventNotifier(evHandle, kd::RLEventNotifier::READ, [=](RLEvent handle, int type) {
            (*times)++;
			runloop_test_debug2("EventNotifier is signaled: times = %d, diff = %lld ms", (*times), kd::now_time() - cfg->lastEventSignalTimeMs);
			
			ssize_t ret = 0;
            char buf[64];
            do {
                ret = read(handle, buf, sizeof(buf));
            } while(ret > 0 || (ret == -1 && errno == EINTR) );
        });
		kd::RunLoop::addEventNotifier(tid, eventNotifier);

		// Signal handle
		for (int i = 0; i < cfg->eventSignalCount; i++) {
			kd::BackgroundDelayScheduler::defaultScheduler()->post(300 + cfg->eventSignalTimeInterval * i, [=] {
				cfg->lastEventSignalTimeMs = kd::now_time();
				runloop_test_debug2("Signal EventNotifier, manualReset = %d", cfg->manualResetEvent);
				char c = '1';
				write(fds[1], &c, 1);
			});
		}

		// Remove EventNotifier
		int removeDelay = cfg->eventSignalCount * cfg->eventSignalTimeInterval;
		kd::BackgroundDelayScheduler::defaultScheduler()->post(removeDelay, [=] {
			runloop_test_debug2("Remove EventNotifier");
			kd::RunLoop::removeEventNotifier(tid, evHandle);

			close(fds[0]);
			close(fds[1]);
		});
	}
#endif // KD_OS_WIN

}

inline void exit_runloop(bool by_sysapi, std::thread::id tid, int code, std::vector<void*> argv) {
	runloop_test_debug2("Exit RunLoop of thread <%s>, by_sysapi = %d, code = %d, argc = %d",
		kd::thread_id_str(tid).c_str(), by_sysapi, code, argv.size());
	if (!by_sysapi) {
		kd::QuitRunLoop(tid, code);
		return;
	}

	// Exit using sys API
#ifdef KD_OS_WIN
	PostThreadMessageW(*(reinterpret_cast<DWORD*>(argv[0])), WM_QUIT, code, 0);
	return;
	kd::RunLoop::postTask(tid, [=] {
		PostQuitMessage(code);
	});
#else
	kd::QuitRunLoop(tid, code);
#endif // KD_OS_WIN
}

inline void start_new_event_loop(std::thread::id tid, int exitCode = -1, int quitDelayMs = kMaxTime) {
	bool is_same_thread = (tid == std::this_thread::get_id());
	auto event_loop_exec = [=] {
		auto ev = kd::EventLoop::createShared();

		// Exit EventLoop
		kd::BackgroundDelayScheduler::defaultScheduler()->post(quitDelayMs, [ev, exitCode] {
			if (ev) {
				runloop_test_debug2("Require to exit EventLoop_%p (%d)", ev.get(), exitCode);
				ev->exit(exitCode);
			} else {
				runloop_test_debug2("Require to exit EventLoop_%p (%d) ERROR", ev.get(), exitCode);
			}
		});

		// Start EventLoop
		runloop_test_debug2("Enter EventLoop_%p (%s, %d, %d ms)", ev.get(),
			(is_same_thread ? "call-exec-directly" : "post-to-exec"), exitCode, quitDelayMs);
		int ret = ev->exec();
		runloop_test_debug2("Leave EventLoop_%p, code = %d", ev.get(), ret);

		// Release EventLoop on other thread
		// runloop_test_debug2("About to release EventLoop_%p on other thread after 2000 ms", ev.get());
		kd::BackgroundDelayScheduler::defaultScheduler()->post(2000, [ev]() mutable {
			//runloop_test_debug2("Release EventLoop_%p", ev.get());
			ev.reset();
		});
	};

	if (is_same_thread) {
		event_loop_exec();
	} else {
		kd::RunLoop::postTask(tid, [=] { event_loop_exec(); });

	}
}
};  // namespace kd_runloop_test

// Test
namespace kd_runloop_test {

// Test: Use RunLoop in an outer event loop
inline void test_runloop_on_main() {
	// Init RunLoop
	static std::once_flag once;
	std::call_once(once, [=] {
		kd::RunLoop::init(kd::RLMode::Native);
	});

	auto tid = std::this_thread::get_id();
	decltype(TestConfig::allTasks()) cfg = nullptr;
	//cfg = TestConfig::onceTasks();
	//cfg = TestConfig::eventNotifiers();
	//cfg = TestConfig::timerTasks();
	cfg = TestConfig::allTasks();
	cfg->timerTaskMaxTimes = 10;

	do_runloop_testworks(tid, cfg);
}

// Test: Multiple event loops on one thread
inline void test_native_eventloop_on_thread() {
	std::shared_ptr<std::thread::id> tid = std::make_shared<std::thread::id>();
	auto promise = std::make_shared<std::promise<void>>();
#ifdef KD_OS_WIN
	std::shared_ptr<DWORD> winTid = std::make_shared<DWORD>(0);
#endif // KD_OS_WIN

	std::thread workThread = std::thread([=] {
		kd::RunLoop::init(kd::RLMode::Native);

#ifdef KD_OS_WIN
		* winTid = GetCurrentThreadId();
#endif // KD_OS_WIN
		* tid = std::this_thread::get_id();
		promise->set_value();

		start_new_event_loop(*tid, 102, 10000);
	});

	promise->get_future().wait();
	workThread.detach();
	printf("\n\n");
	runloop_test_debug2("Thread <%s> with EventLoop(NativeMode) started", kd::thread_id_str(*tid).c_str());

	// Do work
	decltype(TestConfig::allTasks()) cfg = nullptr;
	//cfg = TestConfig::onceTasks();
	//cfg = TestConfig::eventNotifiers();
	//cfg = TestConfig::timerTasks();
	cfg = TestConfig::allTasks();
	do_runloop_testworks(*tid, cfg);
	
	// Start new EventLoops in same thread
	bool testNestestEventLoop = false;
	if (testNestestEventLoop) {
		int postDelay = 1000;
		int quitDelay = 5000;
		kd::BackgroundDelayScheduler::defaultScheduler()->post(postDelay, [=] {
			start_new_event_loop(*tid, 201, quitDelay);
		});

		postDelay += 1000;
		kd::BackgroundDelayScheduler::defaultScheduler()->post(postDelay, [=] {
			start_new_event_loop(*tid, 202, quitDelay);
		});

		postDelay += 1000;
		kd::BackgroundDelayScheduler::defaultScheduler()->post(postDelay, [=] {
			start_new_event_loop(*tid, 203, quitDelay);
		});

		postDelay += 1000;
		kd::BackgroundDelayScheduler::defaultScheduler()->post(postDelay, [=] {
#ifdef KD_OS_WIN
			std::vector<void*> argv{ (void*)(winTid.get()) };
#else
			std::vector<void*> argv{};
#endif
			int exitCode = 206;
			//runloop_test_debug2("About to exit run loop: code = %d", exitCode)
			//exit_runloop(true, *tid, exitCode, argv);
		});
	}
}

// Test: Event mode of event loop on one thread
inline void test_event_eventloop_on_thread() {
	std::shared_ptr<std::thread::id> tid = std::make_shared<std::thread::id>();
	auto promise = std::make_shared<std::promise<void>>();

	std::thread workThread = std::thread([=] {
		kd::RunLoop::init(kd::RLMode::Event);

		* tid = std::this_thread::get_id();
		promise->set_value();

		start_new_event_loop(*tid, 601, 10000);
	});

	promise->get_future().wait();
	workThread.detach();
	printf("\n\n");
	runloop_test_debug2("Thread <%s> with EventLoop (EventMode) started", kd::thread_id_str(*tid).c_str());

	// Do work
	decltype(TestConfig::allTasks()) cfg = nullptr;
	//cfg = TestConfig::onceTasks();
	//cfg = TestConfig::eventNotifiers();
	//cfg = TestConfig::timerTasks();
	cfg = TestConfig::allTasks();
	do_runloop_testworks(*tid, cfg);
}

static void test_runloop_stress(std::thread::id tid, kd::RLMode mode) {
	runloop_test_debug2("Start testing run loop stress, mode = %s", (mode == RLMode::Native? "Native" : "Event"));

	std::thread([=] {
		
		int threadCount = 4;

		struct TaskContext {
			std::mutex mtx;
			int threadCount{ 0 };
			int countPerThread{0};
			int totalCount{ 0 };
			int processedCount{ 0 };
			int removeCount{ 0 };
			std::vector<uint64_t> ids;
			std::vector<long long> timeDiffs;

			TaskContext(int tc, int cpt) {
				threadCount = tc;
				countPerThread = cpt;
				totalCount = threadCount * countPerThread;
			}

			int expectedProcessedCount() {
				if (removeCount > totalCount) {
					return 0;
				}
				return totalCount - removeCount;
			}
		};

		// Test once tasks
		auto onceTasksCtx = std::make_shared<TaskContext>(threadCount, 5000);
		for (int i = 0; i < onceTasksCtx->threadCount; i++) {
			std::thread([=] {
				for (int j = 0; j < onceTasksCtx->countPerThread; j++) {
					auto delayMs = kd::random::generateUInt(0, 100);
					delayMs = (delayMs > 80 ? 0 : 10);
					auto nowMs = kd::now_time();

					kd::RunLoop::postTask(tid, [=] {
						onceTasksCtx->timeDiffs.push_back(kd::now_time() - nowMs - delayMs);
						onceTasksCtx->processedCount++;
					}, delayMs);

					auto sleepMs = kd::random::generateUInt(1, 10);
					if (sleepMs % 5 == 0) {
						kd::thread_sleep(sleepMs);
					}
				}
			}).detach();
		}

		// Test timer tasks
		auto timerTasksCtx = std::make_shared<TaskContext>(threadCount, 1000);
		timerTasksCtx->removeCount = 100;

		for (int i = 0; i < timerTasksCtx->threadCount; i++) {
			// Add
			std::thread([=] {
				for (int j = 0; j < timerTasksCtx->countPerThread; j++) {
					auto delayMs = kd::random::generateUInt(100, 3000);
					if (i == timerTasksCtx->threadCount -1) {
						delayMs = kd::random::generateUInt(1000, 3000);
					}

					auto nowMs = kd::now_time();
					auto id = kd::RunLoop::addTimer(tid, delayMs, false, [=] (uint64_t timerId) {
						timerTasksCtx->timeDiffs.push_back(kd::now_time() - nowMs - delayMs);
						timerTasksCtx->processedCount++;
					});

					{
						std::lock_guard<std::mutex> lock(timerTasksCtx->mtx);
						timerTasksCtx->ids.push_back(id);
					}

					auto sleepMs = kd::random::generateUInt(0, 10);
					if (sleepMs % 3 == 0) {
						kd::thread_sleep(sleepMs);
					}
				}
			}).detach();

			// Remove
			kd::BackgroundDelayScheduler::defaultScheduler()->post(100, [=] {
				if ((int)timerTasksCtx->ids.size() > timerTasksCtx->removeCount) {
					for (int i = 0; i < timerTasksCtx->removeCount; i++) {
						kd::RunLoop::removeTimer(tid, timerTasksCtx->ids[i]);
					}
				}
			});
		}

		// Test EventNotifiers
		auto eventNotifiersCtx = std::make_shared<TaskContext>(threadCount, 600);

#ifdef KD_OS_WIN
		for (int i = 0; i < eventNotifiersCtx->threadCount; i++) {
			std::thread([=] {
				for (int j = 0; j < eventNotifiersCtx->countPerThread; j++) {
					kd::RLEvent evHandle = CreateEventA(NULL, false, FALSE, NULL);

					// Add
					auto delayMs = kd::random::generateUInt(10, 20);
					auto nowMs = kd::now_time();
					kd::RLEventNotifier eventNotifier(evHandle, kd::RLEventNotifier::NONE, [=](RLEvent handle, int type) {
						eventNotifiersCtx->timeDiffs.push_back(kd::now_time() - nowMs - delayMs);
						eventNotifiersCtx->processedCount++;

						kd::RunLoop::removeEventNotifier(tid, handle);
						CloseHandle(handle);
					});
					kd::RunLoop::addEventNotifier(tid, eventNotifier);

					// Signal
					kd::BackgroundDelayScheduler::defaultScheduler()->post(delayMs, [=] {
						SetEvent(evHandle);
					});
					
					auto sleepMs = kd::random::generateUInt(0, 10);
					if (sleepMs % 3 == 0) {
						kd::thread_sleep(sleepMs);
					}
				}
			}).detach();
		}
#elif defined(KD_OS_UNIX)
		for (int i = 0; i < eventNotifiersCtx->threadCount; i++) {
			std::thread([=] {
				for (int j = 0; j < eventNotifiersCtx->countPerThread; j++) {
					int fds[2];
					if (pipe(fds) != 0) {
						runloop_test_debug2("Failed to create pipe");
						return;
					}
					fcntl(fds[0], F_SETFL, O_NONBLOCK);
					fcntl(fds[1], F_SETFL, O_NONBLOCK);

					kd::RLEvent evHandle = fds[0]; // Read fd

					// Add
					auto delayMs = kd::random::generateUInt(10, 20);
					auto nowMs = kd::now_time();
					kd::RLEventNotifier eventNotifier(evHandle, kd::RLEventNotifier::READ, [=, fd0 = fds[0], fd1 = fds[1]](RLEvent handle, int type) {
						ssize_t ret = 0;
						char buf[64];
						do {
							ret = read(handle, buf, sizeof(buf));
						} while (ret > 0 || (ret == -1 && errno == EINTR));

						eventNotifiersCtx->timeDiffs.push_back(kd::now_time() - nowMs - delayMs);
						eventNotifiersCtx->processedCount++;

						kd::RunLoop::removeEventNotifier(tid, handle);
						close(fd0);
						close(fd1);
					});
					kd::RunLoop::addEventNotifier(tid, eventNotifier);

					// Signal
					kd::BackgroundDelayScheduler::defaultScheduler()->post(delayMs, [=] {
						char c = '1';
						write(fds[1], &c, 1);
					});

					auto sleepMs = kd::random::generateUInt(0, 10);
					if (sleepMs % 3 == 0) {
						kd::thread_sleep(sleepMs);
					}
				}
			}).detach();
		}
#endif // KD_OS_WIN

		// Check processing
		auto checkingMs = kd::now_time();
		auto generateLogStr = [=] {
			std::string log;
			log += kd::format_str("Tasks = %d / %d", onceTasksCtx->processedCount, onceTasksCtx->expectedProcessedCount());
			log += kd::format_str(", Timers = %d / %d", timerTasksCtx->processedCount, timerTasksCtx->expectedProcessedCount());
			log += kd::format_str(", Events = %d / %d", eventNotifiersCtx->processedCount, eventNotifiersCtx->expectedProcessedCount());

			return log;
		};

		while (true) {
			runloop_test_debug2("%s", generateLogStr().c_str());
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (kd::now_time() - checkingMs > 10000) {
				op_test_debug1("Wait timeout");
				break;
			}

			if (onceTasksCtx->processedCount == onceTasksCtx->expectedProcessedCount()
				&& timerTasksCtx->processedCount == timerTasksCtx->expectedProcessedCount()
				&& eventNotifiersCtx->processedCount == eventNotifiersCtx->expectedProcessedCount()) {
				op_test_debug1("All done");
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				break;
			}
		}
		runloop_test_debug2("%s", generateLogStr().c_str());

		// Final
		printf("\n");
		op_test_debug1("Finish testing run loop stress,mode = %s", (mode == RLMode::Native ? "Native" : "Event"));

		// OnceTasks
		printf("\n");
		op_test_debug1("[Tasks] Processed:    %d / %d", onceTasksCtx->processedCount, onceTasksCtx->expectedProcessedCount());
		if (onceTasksCtx->timeDiffs.size() > 0) {
			auto& time_diffs = onceTasksCtx->timeDiffs;
			long long sum_diff = std::accumulate(time_diffs.begin(), time_diffs.end(), 0LL);
			long long max_diff = *std::max_element(time_diffs.begin(), time_diffs.end());
			long long min_diff = *std::min_element(time_diffs.begin(), time_diffs.end());
			
			op_test_debug1("[Tasks] Avg Diff:     %lld ms", sum_diff / (long long)time_diffs.size());
			op_test_debug1("[Tasks] Max Diff:     %lld ms", max_diff);
			op_test_debug1("[Tasks] Min Diff:     %lld ms", min_diff);
		} else {
			op_test_debug1("[Tasks] Error: empty time diffs");
		}

		// TimerTasks
		printf("\n");
		op_test_debug1("[Timers] Processed:    %d / %d", timerTasksCtx->processedCount, timerTasksCtx->expectedProcessedCount());
		if (timerTasksCtx->timeDiffs.size() > 0) {
			auto& time_diffs = timerTasksCtx->timeDiffs;
			long long sum_diff = std::accumulate(time_diffs.begin(), time_diffs.end(), 0LL);
			long long max_diff = *std::max_element(time_diffs.begin(), time_diffs.end());
			long long min_diff = *std::min_element(time_diffs.begin(), time_diffs.end());

			op_test_debug1("[Timers] Avg Diff:     %lld ms", sum_diff / (long long)time_diffs.size());
			op_test_debug1("[Timers] Max Diff:     %lld ms", max_diff);
			op_test_debug1("[Timers] Min Diff:     %lld ms", min_diff);
		} else {
			op_test_debug1("[Timers] Error: empty time diffs");
		}

		// EventNotifiers
		printf("\n");
		op_test_debug1("[Events] Processed:    %d / %d", eventNotifiersCtx->processedCount, eventNotifiersCtx->expectedProcessedCount());
		if (eventNotifiersCtx->timeDiffs.size() > 0) {
			auto& time_diffs = eventNotifiersCtx->timeDiffs;
			long long sum_diff = std::accumulate(time_diffs.begin(), time_diffs.end(), 0LL);
			long long max_diff = *std::max_element(time_diffs.begin(), time_diffs.end());
			long long min_diff = *std::min_element(time_diffs.begin(), time_diffs.end());

			op_test_debug1("[Events] Avg Diff:     %lld ms", sum_diff / (long long)time_diffs.size());
			op_test_debug1("[Events] Max Diff:     %lld ms", max_diff);
			op_test_debug1("[Events] Min Diff:     %lld ms", min_diff);
		} else {
			op_test_debug1("[Events] Error: empty time diffs");
		}

		printf("\n\n");
	}).detach();
}

static void test_runloop_stress(kd::RLMode mode) {
	std::thread workThread = std::thread([=] {
		kd::RunLoop::init(mode);

		auto tid = std::this_thread::get_id();
		kd::BackgroundDelayScheduler::defaultScheduler()->post(100, [=] {
			test_runloop_stress(tid, mode);
		});
		start_new_event_loop(tid, 101, 15000);
	});

	workThread.detach();
}

}
