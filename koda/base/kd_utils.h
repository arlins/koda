/* **************************************************************
 Created by: Arlin (arlins.dps@gmail.com).
 This file implements the APIs for the utility tools.
************************************************************** */

#pragma once

#include <iostream>
#include <string>
#include <streambuf>
#include <cstdio>
#include <thread>
#include <sstream>
#include <chrono>
#include <errno.h>
#include <ctime>
#include <vector>
#include <algorithm>
#include <functional>
#include <atomic>
#include <iomanip>

#ifdef KD_OS_WIN
#include <windows.h>
#include <direct.h>  // _mkdir
#include <io.h> // _access
#include <tchar.h> // for _T("")
#else // Unix
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <errno.h>
	#if defined(KD_OS_DARWIN)
	#include <mach/mach.h>
	#include <mach/thread_act.h>
	#include <mach/thread_policy.h>
	#include <os/log.h> // os_log
	#include <dispatch/dispatch.h>
	#include <pthread.h>
	#include <pthread/qos.h>
	#include <sys/qos.h>

	extern "C" {
		int pthread_set_qos_class_np(pthread_t __pthread, qos_class_t __qos_class, int __relative_priority);
	}

	#endif // KD_OS_DARWIN

	#if defined(KD_OS_ANDROID)
	#include <sys/resource.h> // Android
	#endif // KD_OS_ANDROID
#endif // KD_OS_WIN

#include "koda/kd_global.h"
#include "koda/base/kd_str.h"
#include "koda/base/kd_scopeguard.h"

// ================
// Defines
// ================
// Time
#define KD_TIME_BEGIN(n) auto __start_time__##n = kd::now_time();
#define KD_TIME_END(n) printf("[%s] time = %lld ms\n", #n, kd::now_time() - __start_time__##n);

// std::cout
#define KD_StdPrint(Class) \
friend std::ostream& operator<<(std::ostream& os, const Class& obj) {\
    os << obj.std_cout(); return os;\
}

// ================
// Utility
// ================
__NAMESPACE_KD_BEGIN

// Get string id of thread
inline std::string thread_id_str(std::thread::id tid, bool hex = false, int width = 0) {
	std::ostringstream oss;
	if (hex) {
		oss << std::hex;
	}
	if (width > 0) {
		oss << std::setw(width) << std::setfill('0');
	}

	oss << tid;
	return oss.str();
}

// Get string id of current thread
inline std::string this_thread_id(bool hex = false, int width = 0) {
	return thread_id_str(std::this_thread::get_id(), hex, width);
}

inline void thread_sleep(int ms) {
	std::this_thread::sleep_for(std::chrono::milliseconds(ms)); //ms
}

inline std::thread::native_handle_type get_current_native_handle() {
#if defined(KD_OS_WIN)
	return GetCurrentThread();
#elif defined(KD_OS_UNIX)
	return pthread_self();
#else
	KD_ASSERT_M(false, "Unknown platform for get_current_native_handle");
	return std::thread::native_handle_type();
#endif
}

// Obtain the index of the CPU core 
// on which the current thread is running
inline int get_current_cpu() {
#if defined(KD_OS_WIN)
	return GetCurrentProcessorNumber();
#elif defined(KD_OS_DARWIN)
	// Apple has not provided a stable API for 
	// obtaining the Core ID officially.
	return -1;
#elif defined(KD_OS_UNIX)
	return sched_getcpu();

#else
	return -1; // Unsupported platform
#endif
}

// Bind the current thread to the specified CPU core
inline bool run_on_cpu(int cpu_id) {
#if defined(KD_OS_WIN)
	DWORD_PTR mask = (1ULL << cpu_id);
	return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
#elif defined(KD_OS_ANDROID) || defined(KD_OS_LINUX)
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_id, &cpuset);
	return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == 0;
#elif defined(KD_OS_DARWIN)
	thread_port_t thread = mach_thread_self();
	thread_affinity_policy_data_t policy = { cpu_id };
	boolean_t success = thread_policy_set(
		thread,
		THREAD_AFFINITY_POLICY,
		(thread_policy_t)&policy,
		THREAD_AFFINITY_POLICY_COUNT
	);
	mach_port_deallocate(mach_task_self(), thread);
	return success == KERN_SUCCESS;
#elif defined(KD_OS_UNIX)
	// Other Unix-like systems have extremely fragmented CPU 
	// affinity APIs, with some not even being supported at all.
	return false; 
#else
	return false; // Unsupported platform
#endif
}
// ==============================
// Thread Priority
// ==============================

enum class ThreadPriority {
	Low,
	Normal,
	High,
	VeryHigh
};

#if defined(KD_OS_WIN)
static inline int priority_to_windows_value(ThreadPriority priority) {
	switch (priority) {
		case ThreadPriority::Low:			return THREAD_PRIORITY_BELOW_NORMAL;
		case ThreadPriority::Normal:		return THREAD_PRIORITY_NORMAL;
		case ThreadPriority::High:			return THREAD_PRIORITY_ABOVE_NORMAL;
		case ThreadPriority::VeryHigh:	return THREAD_PRIORITY_HIGHEST;
		default:										return THREAD_PRIORITY_NORMAL;
	}
}
#endif // KD_OS_WIN

#if defined(KD_OS_DARWIN)
static inline qos_class_t priority_to_apple_qos_class(ThreadPriority priority) {
	switch (priority) {
		case ThreadPriority::Low:			return QOS_CLASS_UTILITY;
		case ThreadPriority::Normal:		return QOS_CLASS_DEFAULT;
		case ThreadPriority::High:			return QOS_CLASS_USER_INITIATED;
		case ThreadPriority::VeryHigh:	return QOS_CLASS_USER_INTERACTIVE;
		default:										return QOS_CLASS_DEFAULT;
	}
}
#endif // KD_OS_DARWIN

#if defined(KD_OS_ANDROID)
static inline int priority_to_android_value(ThreadPriority priority) {
	// level: -20 (high) ~ 19 (low)
	switch (priority) {
		case ThreadPriority::Low:			return 10;
		case ThreadPriority::Normal:		return 0;
		case ThreadPriority::High:			return -8;
		case ThreadPriority::VeryHigh:	return -16;
		default:										return 0;

	}
}
#endif // KD_OS_ANDROID

#if defined(KD_OS_UNIX)
static inline int priority_to_unix_value(ThreadPriority priority) {
	int min_prio = sched_get_priority_min(SCHED_OTHER);
	int max_prio = sched_get_priority_max(SCHED_OTHER);
	if (max_prio <= min_prio) {
		return 0;
	}

	int range = max_prio - min_prio;
	switch (priority) {
		case ThreadPriority::Low:			return min_prio + range * 1 / 4;
		case ThreadPriority::Normal:		return min_prio + range * 2 / 4;
		case ThreadPriority::High:			return min_prio + range * 3 / 4;
		case ThreadPriority::VeryHigh:	return max_prio;
		default:										return min_prio + range * 2 / 4;
	}
}
#endif // KD_OS_UNIX

static inline bool set_thread_priority(std::thread & thread, ThreadPriority priority) {
	if (!thread.joinable()) {
		KD_ASSERT_M(false, "Thread must be joinable");
		return false;
	}

#if defined(KD_OS_WIN)
	HANDLE handle = thread.native_handle();
	return SetThreadPriority(handle, priority_to_windows_value(priority)) != 0;
#elif defined(KD_OS_DARWIN)
	pthread_t handle = thread.native_handle();
	qos_class_t qos = priority_to_apple_qos_class(priority);
	return pthread_set_qos_class_np(handle, qos, 0) == 0;
#elif defined(KD_OS_UNIX)
	pthread_t handle = thread.native_handle();
	sched_param param;
	param.sched_priority = priority_to_unix_value(priority);
	return pthread_setschedparam(handle, SCHED_OTHER, &param) == 0;
#else
	KD_ASSERT_M(false, "Unsupported OS, unable to set thread priority");
	return false;
#endif
}

static inline bool set_current_thread_priority(ThreadPriority priority) {
#if defined(KD_OS_WIN)
	return SetThreadPriority(GetCurrentThread(), priority_to_windows_value(priority)) != 0;
#elif defined(KD_OS_DARWIN)
	qos_class_t qos = priority_to_apple_qos_class(priority);
	return pthread_set_qos_class_self_np(qos, 0) == 0;
#elif defined(KD_OS_ANDROID)
	// nice: -20 (high) ~ 19 (low)。
	int nice_value = priority_to_android_value(priority);
	return setpriority(PRIO_PROCESS, 0, nice_value) == 0;
#elif defined(KD_OS_UNIX)
	sched_param param;
	param.sched_priority = priority_to_unix_value(priority);
	return pthread_setschedparam(pthread_self(), SCHED_OTHER, &param) == 0;
#else
	KD_ASSERT_M(false, "Unsupported OS, unable to set current thread priority");
	return false;
#endif
}

inline int64_t current_pid() {
#if defined(KD_OS_WIN)
    return static_cast<int64_t>(GetCurrentProcessId());
#else
    return static_cast<int64_t>(getpid());
#endif
}

// Get now time
template <typename TimeUnit = std::chrono::milliseconds>
inline long long now_time() {
	auto now = std::chrono::high_resolution_clock::now();
	long long time = std::chrono::duration_cast<TimeUnit>(now.time_since_epoch()).count();
	return time;
}

template <typename TimePoint, typename TimeUnit = std::chrono::milliseconds>
inline long long time_to_longlong(TimePoint t) {
	long long time = std::chrono::duration_cast<TimeUnit>(t.time_since_epoch()).count();
	return time;
}

// Print console log
inline void console_log(const std::string& str) {
#ifdef KD_OS_WIN
	std::wstring wstr = kd::str2wstr(str);
	::OutputDebugStringW(wstr.c_str());
#elif defined(KD_OS_DARWIN)
	os_log(OS_LOG_DEFAULT, "%s", str.c_str());
#else
	fprintf(stderr, "%s", str.c_str());
#endif
}

// Join path: dir/sub, without ending '/'
inline std::string join_path(const std::string& dir = "", const std::string& sub = "") {
	std::string _dir(dir);
	std::string _sub(sub);

	for (auto& c : _dir) {
		if (c == '\\') c = '/';
	}
	for (auto& c : _sub) {
		if (c == '\\') c = '/';
	}

	//
	if (_dir.empty() && _sub.empty()) {
		return "";
	}
	if (_dir.empty()) {
		while (!_sub.empty() && _sub.back() == '/') {
			_sub.pop_back();
		}
		return _sub;
	}
	if (_sub.empty()) {
		while (!_dir.empty() && _dir.back() == '/') {
			_dir.pop_back();
		}
		return _dir;
	}

	//
	while (!_dir.empty() && _dir.back() == '/') {
		_dir.pop_back();
	}

	size_t sub_start = 0;
	while (sub_start < _sub.length() && _sub[sub_start] == '/') {
		sub_start++;
	}
	if (sub_start > 0) {
		_sub = _sub.substr(sub_start);
	}

	while (!_sub.empty() && _sub.back() == '/') {
		_sub.pop_back();
	}

	//
	if (_sub.empty()) {
		return _dir;
	}
	if (_dir.empty()) {
		return _sub;
	}
	return _dir + "/" + _sub;
}

// Recursive directory creation
inline bool create_directory(const std::string& path) {
	if (path.empty()) {
		return false;
	}

#if defined(KD_OS_WIN) // Windows
	if (_access(path.c_str(), 0) == 0) {
		return true;
	}

	std::string normalizedPath = kd::str_replace(path, "\\", "/");
	size_t pos = 0;
	if (normalizedPath.length() >= 3 && normalizedPath[1] == ':') {
		pos = 3; // Skip drive letters (e.g., "C:\")
	}

	while ((pos = normalizedPath.find('/', pos)) != std::string::npos) {
		std::string currentPath = normalizedPath.substr(0, pos);
		if (!currentPath.empty() && currentPath.back() != ':') {
			_mkdir(currentPath.c_str());
		}
		pos++;
	}

	return _mkdir(normalizedPath.c_str()) == 0 || errno == EEXIST;

#else // Unix
	if (access(path.c_str(), F_OK) == 0) {
		return true;
	}

	std::string normalizedPath = kd::str_replace(path, "\\", "/");
	size_t pos = 0;
	if (normalizedPath[0] == '/') {
		pos = 1; // Skip leading slash for absolute paths on Unix
	}

	while ((pos = normalizedPath.find('/', pos)) != std::string::npos) {
		std::string currentPath = normalizedPath.substr(0, pos);
		if (!currentPath.empty()) {
			mkdir(currentPath.c_str(), 0777);
		}
		pos++;
	}

	return mkdir(normalizedPath.c_str(), 0777) == 0 || errno == EEXIST;
#endif
}

__NAMESPACE_KD_END // Utility

// =========================
// invoke for C++14
// =========================
__NAMESPACE_KD_BEGIN
// Normal function
template <typename F, typename... Args>
auto invoke(F&& f, Args&&... args)
-> decltype(std::forward<F>(f)(std::forward<Args>(args)...))
{
	return std::forward<F>(f)(std::forward<Args>(args)...);
}

// Non-const class function ( T&)
template <typename R, typename T, typename... Args, typename Obj, typename... Rest>
auto invoke(R(T::* f)(Args...), Obj&& obj, Rest&&... args)
-> decltype((std::forward<Obj>(obj).*f)(std::forward<Rest>(args)...))
{
	return (std::forward<Obj>(obj).*f)(std::forward<Rest>(args)...);
}

// Const class function (T&)
template <typename R, typename T, typename... Args, typename Obj, typename... Rest>
auto invoke(R(T::* f)(Args...) const, Obj&& obj, Rest&&... args)
-> decltype((std::forward<Obj>(obj).*f)(std::forward<Rest>(args)...))
{
	return (std::forward<Obj>(obj).*f)(std::forward<Rest>(args)...);
}

// Non-const class function(T*)
template <typename R, typename T, typename... Args, typename Obj, typename... Rest>
auto invoke(R(T::* f)(Args...), Obj* obj, Rest&&... args)
-> decltype((obj->*f)(std::forward<Rest>(args)...))
{
	return (obj->*f)(std::forward<Rest>(args)...);
}

// Const class function (T*)
template <typename R, typename T, typename... Args, typename Obj, typename... Rest>
auto invoke(R(T::* f)(Args...) const, Obj* obj, Rest&&... args)
-> decltype((obj->*f)(std::forward<Rest>(args)...))
{
	return (obj->*f)(std::forward<Rest>(args)...);
}

__NAMESPACE_KD_END // invoke


// =========================
// algorithms
// =========================
__NAMESPACE_KD_BEGIN

// Obtain all matching elements
// auto matches = find_all_if(c.begin(), c.end(),[](auto& val) { return true; });
template <typename InputIter, typename Predicate>
std::vector<typename std::iterator_traits<InputIter>::value_type>
find_all_elements(InputIter first, InputIter last, Predicate pred) {
	using value_type = typename std::iterator_traits<InputIter>::value_type;
	std::vector<value_type> matches;
	for (InputIter it = first; it != last; ++it) {
		if (pred(*it)) {
			matches.push_back(*it);
		}
	}

	return matches;
}

// Obtain iterators for all matching elements
// auto match_iters = find_all_if(c.begin(), c.end(),[](auto& val) { return true; });
template <typename InputIter, typename Predicate>
std::vector<InputIter> find_all_iterators(InputIter first, InputIter last, Predicate pred) {
	std::vector<InputIter> matches;
	for (InputIter it = first; it != last; ++it) {
		if (pred(*it)) {
			matches.push_back(it);
		}
	}

	return matches;
}

// Remove all matching elements
// remove_all(c, [](auto& val){ return true; })
template <typename Container, typename Predicate>
void remove_all(Container& cont, Predicate pred) {
	for (auto it = cont.begin(); it != cont.end(); ) {
		if (pred(*it)) {
			it = cont.erase(it);
		} else {
			++it;
		}
	}
}

// Remove one of all matching elements
// remove_one(c, [](auto& val){ return true; })
template <typename Container, typename Predicate>
void remove_one(Container& cont, Predicate pred) {
	for (auto it = cont.begin(); it != cont.end(); ) {
		if (pred(*it)) {
			cont.erase(it);
			break;
		}
	}
}

// for_each2(c.begin(), c.end(), [](auto& val){ return true; })
template <typename InputIter, typename Handler>
void for_each2(InputIter first, InputIter last, Handler handler) {
	for (InputIter it = first; it != last; ++it) {
		bool _continue = handler(*it);
		if (!_continue) {
			break;
		}
	}
}

__NAMESPACE_KD_END // algorithms


// ================
// Print
// ================
__NAMESPACE_KD_BEGIN
class StdCoutRedirector : public std::streambuf {
private:
	std::function<void(const std::string&)> callback;
	std::string buffer;
	std::streambuf* old_buf;

public:
	explicit StdCoutRedirector(std::function<void(const std::string&)> func)
		: callback(std::move(func)) {
		old_buf = std::cout.rdbuf(this);
	}

	~StdCoutRedirector() {
		std::cout.rdbuf(old_buf);
	}

protected:
	// Processing single character input
	int overflow(int c) override {
		if (c == traits_type::eof()) {
			return !traits_type::eof();
		}
		buffer.push_back(static_cast<char>(c));

		// If a newline character is encountered, 
		// the callback is immediately triggered and the buffer is cleared.
		if (c == '\n') {
			sync();
		}
		return c;
	}

	// Processing batch character input
	std::streamsize xsputn(const char* s, std::streamsize n) override {
		buffer.append(s, size_t(n));
		if (buffer.find('\n') != std::string::npos) {
			sync();
		}
		return n;
	}

	// Synchronous operation 
	// Triggered when std::endl or flush is called
	int sync() override {
		if (!buffer.empty()) {
			callback(buffer);
			buffer.clear();
		}
		return 0;
	}
};

__NAMESPACE_KD_END // StdCoutRedirector

// ===========================
// Debugging tools
// ===========================

#if defined(KD_DEBUG) && defined(KD_DEBUGGING_TOOLS)

// Define self-deadlock detection in class
#define KDD_DEFINE_SELFDEADLOCK_DETECTION(Class) \
private: \
    std::mutex sdl_mtx_; \
    bool sdl_is_held_{ false }; \
    std::thread::id sdl_owner_id_;

// Call before locking: Check for self-deadlock
#define KDD_SDL_CHECK_PRE_LOCK() do { \
    std::lock_guard<std::mutex> lock(sdl_mtx_); \
    if (sdl_is_held_ && sdl_owner_id_ == std::this_thread::get_id()) { \
        KD_ASSERT(false && "Self-DeadLock detected, thread tried to lock twice"); \
    } \
} while(0)

// Mark as locked after successful locking.
#define KDD_SDL_RECORD_POST_LOCK() do { \
    std::lock_guard<std::mutex> lock(sdl_mtx_); \
    sdl_is_held_ = true; \
    sdl_owner_id_ = std::this_thread::get_id(); \
} while(0)

// Call before unlocking: Check for illegal operations
#define KDD_SDL_CHECK_PRE_UNLOCK() do { \
    std::lock_guard<std::mutex> lock(sdl_mtx_); \
    if (!sdl_is_held_) { \
        KD_ASSERT(false && "Attempt to unlock a NOT held lock"); \
    } \
    if (sdl_owner_id_ != std::this_thread::get_id()) { \
        KD_ASSERT(false && "Thread tried to unlock a lock held by other thread"); \
    } \
} while(0)

// Mark as unlocked after successful unlocking.
#define KDD_SDL_RECORD_POST_UNLOCK() do { \
    std::lock_guard<std::mutex> lock(sdl_mtx_); \
    sdl_is_held_ = false; \
    sdl_owner_id_ = std::thread::id(); \
} while(0)

#else
#define KDD_DEFINE_SELFDEADLOCK_DETECTION(Class)
#define KDD_SDL_CHECK_PRE_LOCK()
#define KDD_SDL_RECORD_POST_LOCK()
#define KDD_SDL_CHECK_PRE_UNLOCK()
#define KDD_SDL_RECORD_POST_UNLOCK()
#endif // KD_DEBUGGING_TOOLS
