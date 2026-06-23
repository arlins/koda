#pragma once
#include "koda/base/kd_utils.h"
#include "koda/base/kd_random.h"
#include "koda/async/kd_spinlock.h"
#include "koda/async/kd_fastmutex.h"
#include "koda/async/kd_namedmutex.h"
#include "koda/async//kd_namedevent.h"

// debug
static std::mutex mtx_test_debug_mtx;
#define mtx_test_debug1(fmt, ...) \
{ printf("[MTX_TEST] " fmt "\n", ##__VA_ARGS__);}

#define mtx_test_debug2(fmt, ...) \
{	std::lock_guard<std::mutex> lock(mtx_test_debug_mtx); \
    mtx_test_debug1(fmt, ##__VA_ARGS__); }

// KDTTest_Locker1
#define KDTTest_Locker1(Tag, LockClass, count) \
{	LockClass locker; \
	auto now = kd::now_time(); \
	for (int i = 0; i < count; i++) { std::lock_guard<LockClass> lock(locker); } \
	mtx_test_debug1("%s * %d = %llu ms", Tag, count, kd::now_time() - now); }

// KDTTest_Locker2
#define KDTTest_Locker2(Tag, LockClass, locker, count) \
{	auto now = kd::now_time(); \
	for (int i = 0; i < count; i++) { std::lock_guard<LockClass> lock(locker); } \
	mtx_test_debug1("%s * %d = %llu ms", #LockClass, count, kd::now_time() - now); }


// kd_mtx_test
namespace kd_mtx_test {

// test mutex cost time
inline void test_mtx_cost_time() {
	mtx_test_debug1("Start testing mutex cost");

	std::thread([] {
		using namespace kd;
		using std_mutex = std::mutex;
		NamedMutex mtx("testNamedMutex");
		int count = 10000000;

		KDTTest_Locker1("  SpinLock", SpinLock, count);
		KDTTest_Locker1(" FastMutex", FastMutex, count);
		KDTTest_Locker1("  StdMutex", std_mutex, count);
		KDTTest_Locker2("NamedMutex", NamedMutex, mtx, count / 10);

	}).detach();
}

// test_fastmtx
inline void test_fastmtx() {
	
	std::thread([] {
		kd::FastMutex mtx;
		std::string name = "FastMutex";
		mtx_test_debug2("Start testing %s", name.c_str());

		auto lockAction = [&](std::string tid) {
			for (int i = 0; i < 3; i++) {
				int lock_time = 3000;
				auto start = kd::now_time();
				mtx_test_debug2("[%s] Thread%s is Waiting", name.c_str(), tid.c_str());
				mtx.lock();
				mtx_test_debug2("[%s] Thread%s Locks %d ms, waitTime = %llu ms", name.c_str(), tid.c_str(), lock_time, kd::now_time() - start);
				kd::thread_sleep(lock_time);
				mtx.unlock();
				mtx_test_debug2("[%s] Thread%s Unlocks", name.c_str(), tid.c_str());
			}
		};

		std::thread thread1(lockAction, "11111111");
		std::thread thread2(lockAction, "2222");

		thread1.join();
		thread2.join();
		mtx_test_debug2("Finish testing %s", name.c_str());
	}).detach();
}

// test_namedmtx
inline void test_namedmtx() {

	std::thread([] {
		kd::NamedMutex mtx("test_nm");
		std::string name = "NamedMutex";
		mtx_test_debug2("Start testing %s", name.c_str());
        
        int lock_time = kd::random::generateUInt(30, 60)*100;
		std::string tid = kd::this_thread_id();
		for (int i = 0; i < 5; i++) {
			auto start = kd::now_time();
			mtx_test_debug2("[%s] Thread%s is Waiting", name.c_str(), tid.c_str());
			mtx.lock();
			mtx_test_debug2("[%s] Thread%s Locks %d ms, waitTime = %llu ms", name.c_str(), tid.c_str(), lock_time, kd::now_time() - start);
			kd::thread_sleep(lock_time);
			mtx.unlock();
			mtx_test_debug2("[%s] Thread%s Unlocks", name.c_str(), tid.c_str());
		}

		mtx_test_debug2("Finish testing %s", name.c_str());
	}).detach();
}

inline std::string WaitEventResultToString(kd::WaitEventResult reslut) {
	if (reslut == kd::WaitEventResult::Success) { return "Success"; }
	if (reslut == kd::WaitEventResult::Timeout) { return "Timeout"; }
	if (reslut == kd::WaitEventResult::Interrupted) { return "Interrupted"; }
	if (reslut == kd::WaitEventResult::ErrorOccurred) { return "Error"; }

	return "Unknown";
}

// test_namedevent
inline void test_namedevent(bool wait, bool auto_reset = true, 
	int action_count = 5, int action_time = 10000) {
	mtx_test_debug2("Start testing NamedEvent, wait = %d, areset = %d, acount = %d, atime = %d ms",
	 		wait, auto_reset, action_count, action_time);

	std::thread([=] {
		std::string role = (wait? "Waiter" : "Emitter");
        std::string ev_name = (auto_reset? "1111": "2222");
		kd::NamedEvent ev(ev_name, auto_reset);
		
        auto emitAction = [=, &ev](bool hasNext, int nextMs, int step){
            if(hasNext) {
                mtx_test_debug2("[NamedEvent] %s emits(%d), next = %d ms", role.c_str(), step, nextMs);
            } else {
                mtx_test_debug2("[NamedEvent] %s emits(%d)", role.c_str(), step);
            }
            
            ev.signal();
            
        };
        
		auto waitAction = [=, &ev](int step, int wait_time) {
			if (action_count < 10) {
				mtx_test_debug2("[NamedEvent] %s is Waiting(%d)", role.c_str(), step);
			}
            if (!ev.isAutoReset()) {
                ev.reset();
            }
            
			auto start = kd::now_time();
			auto result = ev.wait(wait_time);
			auto cost = kd::now_time() - start;

            if (result == kd::WaitEventResult::Success) {
                mtx_test_debug2("[NamedEvent] %s was Signaled(%d) ====SIG",
                    role.c_str(), step);
            } else {
                mtx_test_debug2("[NamedEvent] %s Waits(%d) error = %s ====ERR",
                    role.c_str(), step, WaitEventResultToString(result).c_str());
            }
		};
		
		//
		for (int i = 0; i < action_count; i++) {
			if (wait) {
				waitAction(i, action_time);
			} else {
				bool hasNext = (i != action_count-1);
                int next = 0;
				if (action_time > 0) {
					next = action_time;
				} else {
					next = kd::random::generateUInt(30, 60)*100;
				}
                
                emitAction(hasNext, next, i);
				if (hasNext) {
					kd::thread_sleep(next);
				}
			}
		}

		mtx_test_debug2("Finish testing NamedEvent");
	}).detach();
}

}; // namespace kd_mtx_test
