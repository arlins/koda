#pragma once 
#include <functional>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <time.h> 
#include <stdlib.h>

// ============================
// OS Defines
// ============================
#undef KD_OS_WIN
#define KD_OS_UNIX 1

//#define KD_OS_DARWIN 1

//#define KD_OS_OHOS 1
//#define KD_HAS_GLIB 1

#define KD_OS_LINUX 1
#define KD_OS_ANDROID 1


// ============================
// Common Defines
// ============================
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#ifdef KD_BIT_64
typedef __int64 ssize_t;
#else
typedef int     ssize_t;
#endif
#endif

typedef int socklen_t;
typedef int mode_t;

#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define FD_CLOEXEC      1

#define O_RDONLY        00000000
#define O_WRONLY        00000001
#define O_RDWR          00000002
#define O_CREAT         00000100
#define O_EXCL          00000200
#define O_NONBLOCK      00004000
#define O_CLOEXEC       02000000

#define F_OK            0
#define X_OK            1
#define W_OK            2
#define R_OK            4

#define POLLIN          0x0001
#define POLLPRI         0x0002
#define POLLOUT         0x0004
#define POLLERR         0x0008
#define POLLHUP         0x0010
#define POLLNVAL        0x0020

#define MSG_NOSIGNAL    0x4000
#define SHUT_RDWR       2

template<typename... Args>
inline int open(const char*, int, Args&&...) { return 1005; }
inline int fcntl(int, int, ...) { return 0; }
inline int access(const char*, int) { return 0; }
inline int mkdir(const char*, ...) { return 0; }
inline mode_t umask(mode_t) { return 0; }
inline int mkfifo(const char*, mode_t) { return 0; }
inline int getpid() { return 9999; }

#ifdef getsockopt
#undef getsockopt
#endif
inline int getsockopt(uintptr_t, int, int, void*, socklen_t*) { return 0; }
inline int getsockopt(uintptr_t, int, int, char*, int*) { return 0; }

#ifdef setsockopt
#undef setsockopt
#endif
template<typename T1, typename T2, typename T3, typename T4, typename T5>
inline int setsockopt(T1&&, T2&&, T3&&, T4&&, T5&&) { return 0; }

using pthread_t = void*;
inline pthread_t pthread_self() { return nullptr; }

#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

struct sched_param {
	int sched_priority;
};

struct cpu_set_t {
	uint64_t bits[16];
};
inline void CPU_ZERO(cpu_set_t* cs) { if (cs) { for (int i = 0; i < 16; ++i) cs->bits[i] = 0; } }
inline void CPU_SET(int cpu, cpu_set_t* cs) { if (cs) { cs->bits[cpu / 64] |= (1ULL << (cpu % 64)); } }

inline int sched_getcpu() { return 0; }
inline int sched_get_priority_min(int) { return 0; }
inline int sched_get_priority_max(int) { return 99; }
inline int pthread_setschedparam(pthread_t, int, const sched_param*) { return 0; }
inline int sched_setaffinity(int, size_t, const cpu_set_t*) { return 0; }

using sem_t = void*;
#define SEM_FAILED ((sem_t*)(void*)-1)
inline int sem_init(sem_t*, int, unsigned int) { return 0; }
inline int sem_destroy(sem_t*) { return 0; }
inline int sem_wait(sem_t*) { return 0; }
inline int sem_trywait(sem_t*) { return 0; }
inline int sem_post(sem_t*) { return 0; }
inline sem_t* sem_open(const char*, int, ...) { return (sem_t*)12345; }
inline int sem_close(sem_t*) { return 0; }
inline int sem_unlink(const char*) { return 0; }

inline int posix_memalign(void** memptr, size_t alignment, size_t size) {
	if (!memptr) return 22;
	void* ptr = _aligned_malloc(size, alignment);
	if (!ptr) return 12;
	*memptr = ptr;
	return 0;
}

struct sockaddr_un {
	unsigned short sun_family;
	char           sun_path[108];
};

#define F_RDLCK         0
#define F_WRLCK         1
#define F_UNLCK         2
#define F_SETLK         6
#define F_SETLKW        7

struct flock {
	short l_type;
	short l_whence;
	__int64 l_start;
	__int64 l_len;
	int   l_pid;
};

struct RLEventFakeBridge {
	intptr_t val;
	RLEventFakeBridge() : val(0) {}
	RLEventFakeBridge(intptr_t v) : val(v) {}
	operator intptr_t() const { return val; }
	template<typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
	RLEventFakeBridge(T ptr) : val(reinterpret_cast<intptr_t>(ptr)) {}
	template<typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
	operator T() const { return reinterpret_cast<T>(val); }
};

template<typename T>
inline int pipe(T&& fds) { fds[0] = 1001; fds[1] = 1002; return 0; }
template<typename... Args>
inline ssize_t write(Args&&...) { return 0; }
template<typename... Args>
inline ssize_t read(Args&&...) { return 0; }
inline int close(int) { return 0; }

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_SHARED 0x01
#define MAP_FAILED ((void *)-1)

inline void* mmap(void*, size_t, int, int, int, __int64) { return MAP_FAILED; }
inline int munmap(void*, size_t) { return 0; }
template<typename... Args>
inline int shm_open(const char*, int, Args&&...) { return 1006; }
inline int shm_unlink(const char*) { return 0; }
inline int ftruncate(int, __int64) { return 0; }

typedef unsigned long nfds_t;
struct pollfd { int fd; short events; short revents; };
inline int poll(struct pollfd*, nfds_t, int) { return 0; }


// ============================
// Linux
// ============================
#define EPOLL_CLOEXEC   02000000
#define EPOLLIN         0x001
#define EPOLLPRI        0x002
#define EPOLLOUT        0x004
#define EPOLLERR        0x008
#define EPOLLHUP        0x010
#define EPOLLRDHUP      0x2000
#define EPOLLET         (1u << 31)

#define EPOLL_CTL_ADD   1
#define EPOLL_CTL_DEL   2
#define EPOLL_CTL_MOD   3

#define EFD_NONBLOCK    00004000
#define EFD_CLOEXEC     02000000

#define TFD_NONBLOCK    00004000
#define TFD_CLOEXEC     02000000
#define CLOCK_MONOTONIC 1
#define PRIO_PROCESS    0

typedef union epoll_data {
	void* ptr;
	int fd;
	uint32_t u32;
	uint64_t u64;
} epoll_data_t;

struct epoll_event {
	uint32_t events;
	epoll_data_t data;
};

struct itimerspec {
	struct timespec it_interval;
	struct timespec it_value;
};

inline int epoll_create1(int) { return 1; }
inline int epoll_ctl(int, int, int, epoll_event*) { return 0; }
inline int epoll_wait(int, epoll_event*, int, int) { return 0; }
inline int eventfd(unsigned int, int) { return 1; }
inline int pipe2(int*, int) { return 0; }
inline int timerfd_create(int, int) { return 1; }
inline int timerfd_settime(int, int, const struct itimerspec*, struct itimerspec*) { return 0; }
inline int setpriority(int, int, int) { return 0; }


// ============================
// Android
// ============================
struct ALooper { int dummy; };

#define ALOOPER_PREPARE_ALLOW_NON_CALLBACK  (1 << 0)
#define ALOOPER_POLL_CALLBACK               (-2)
#define ALOOPER_POLL_ERROR                  (-4)
#define ALOOPER_EVENT_INPUT                 (1 << 0)
#define ALOOPER_EVENT_OUTPUT                (1 << 1)

typedef int (*ALooper_callbackFunc)(int fd, int events, void* data);

inline ALooper* ALooper_forThread() { static ALooper dummy{}; return &dummy; }
inline ALooper* ALooper_prepare(int) { static ALooper dummy{}; return &dummy; }
inline void ALooper_acquire(ALooper*) {}
inline void ALooper_release(ALooper*) {}
inline int ALooper_wake(ALooper*) { return 0; }
inline int ALooper_pollOnce(int, int*, int*, void**) { return 0; }
inline int ALooper_addFd(ALooper*, int, int, int, ALooper_callbackFunc, void*) { return 0; }
inline int ALooper_removeFd(ALooper*, int) { return 0; }


// ============================
// HarmonyOS
// ============================
struct uv_object {
	void* data;
};

using uv_loop_t = int;
using uv_async_t = uv_object;
using uv_handle_t = uv_object;
using uv_timer_t = uv_object;
using uv_poll_t = uv_object;

#define UV_RUN_NOWAIT 1
#define UV_RUN_ONCE 2
#define UV_EBUSY 1
#define UV_READABLE 1
#define UV_WRITABLE 1

typedef void (*uv_async_cb)(uv_async_t* handle);
typedef void (*uv_timer_cb)(uv_timer_t* handle);
typedef void (*uv_poll_cb)(uv_poll_t* handle, int status, int events);
typedef void (*uv_close_cb)(uv_handle_t* handle);

inline uv_loop_t* uv_default_loop() { static uv_loop_t dummy_loop = 0; return &dummy_loop; }
inline int uv_loop_init(uv_loop_t*) { return 0; }
inline int uv_loop_close(uv_loop_t*) { return 0; }
inline int uv_run(uv_loop_t*, int) { return 0; }

inline int uv_async_init(uv_loop_t*, uv_async_t*, uv_async_cb) { return 0; }
inline int uv_async_send(uv_async_t*) { return 0; }

inline int uv_timer_init(uv_loop_t*, uv_timer_t*) { return 0; }
inline int uv_timer_start(uv_timer_t*, uv_timer_cb, uint64_t, uint64_t) { return 0; }
inline int uv_timer_stop(uv_timer_t*) { return 0; }

inline int uv_poll_init(uv_loop_t*, uv_poll_t*, int) { return 0; }
inline int uv_poll_start(uv_poll_t*, int, uv_poll_cb) { return 0; }
inline int uv_poll_stop(uv_poll_t*) { return 0; }

inline void uv_close(uv_handle_t*, uv_close_cb) {}


// ============================
// GLib
// ============================
using gboolean = int;
using gint = int;
using guint = unsigned int;
using gpointer = void*;

#define FALSE 0
#define TRUE  1

#define G_SOURCE_REMOVE         0
#define G_SOURCE_CONTINUE       1
#define G_PRIORITY_DEFAULT      0
#define G_PRIORITY_DEFAULT_IDLE 200

enum GIOCondition {
	G_IO_IN = 1,
	G_IO_OUT = 4,
	G_IO_PRI = 2,
	G_IO_ERR = 8,
	G_IO_HUP = 16,
	G_IO_NVAL = 32
};

#define G_SOURCE_FUNC(f) ((GSourceFunc) (void (*)(void)) (f))

struct GMainContext { GMainContext* context; };
struct GMainLoop { int dummy; };
struct GSource { int dummy; };

typedef gboolean(*GSourceFunc)(gpointer data);
typedef void (*GDestroyNotify)(gpointer data);

inline GMainContext* g_main_context_get_thread_default() { static GMainContext dummy{}; dummy.context = &dummy; return &dummy; }
inline GMainContext* g_main_context_new() { static GMainContext dummy{}; dummy.context = &dummy; return &dummy; }
inline GMainContext* g_main_context_ref(GMainContext* c) { return c; }
inline void g_main_context_unref(GMainContext*) {}
inline void g_main_context_push_thread_default(GMainContext*) {}
inline void g_main_context_pop_thread_default(GMainContext*) {}
inline gboolean g_main_context_iteration(GMainContext*, gboolean) { return FALSE; }
inline void g_main_context_wakeup(GMainContext*) {}

inline void g_main_context_invoke_full(GMainContext*, gint, GSourceFunc, gpointer, GDestroyNotify) {}

inline GMainLoop* g_main_loop_new(GMainContext*, gboolean) { static GMainLoop dummy{}; return &dummy; }
inline void g_main_loop_run(GMainLoop*) {}
inline void g_main_loop_quit(GMainLoop*) {}
inline void g_main_loop_unref(GMainLoop*) {}

inline GSource* g_timeout_source_new(guint) { static GSource dummy{}; return &dummy; }
inline GSource* g_source_ref(GSource* s) { return s; }
inline void g_source_unref(GSource*) {}
inline void g_source_set_callback(GSource*, GSourceFunc, gpointer, GDestroyNotify) {}
inline guint g_source_attach(GSource*, GMainContext*) { return 1; }
inline void g_source_destroy(GSource*) {}

inline GSource* g_unix_fd_source_new(gint, GIOCondition) { static GSource dummy{}; return &dummy; }
inline void g_source_set_can_recurse(GSource*, gboolean) {}


// ============================
// Darwin (macOS / iOS...)
// ============================
typedef int32_t         SInt32;
typedef uint32_t        CFOptionFlags;
typedef double          CFTimeInterval;
typedef double          CFAbsoluteTime;
typedef const void* CFTypeRef;
typedef int             boolean_t;

inline CFAbsoluteTime CFAbsoluteTimeGetCurrent() { return 0.0; }

#define kCFAllocatorDefault     nullptr
#define kCFRunLoopDefaultMode   nullptr
#define kCFRunLoopCommonModes   nullptr

#define __block

enum {
	kCFRunLoopRunFinished = 1,
	kCFRunLoopRunStopped = 2,
	kCFRunLoopRunTimedOut = 3,
	kCFRunLoopRunHandledSource = 4
};

static constexpr double kFarFutureDelaySec = 315360000.0;

struct __CFRunLoop { int dummy; };
struct __CFRunLoopSource { int dummy; };
struct __CFRunLoopTimer { int dummy; };
struct __CFFileDescriptor { int dummy; };
struct __CFString { int dummy; };

typedef struct __CFRunLoop* CFRunLoopRef;
typedef struct __CFRunLoopSource* CFRunLoopSourceRef;
typedef struct __CFRunLoopTimer* CFRunLoopTimerRef;
typedef struct __CFFileDescriptor* CFFileDescriptorRef;
typedef const struct __CFString* CFStringRef;

inline CFRunLoopRef CFRunLoopGetCurrent() { static __CFRunLoop dummy{}; return &dummy; }
inline void CFRunLoopRun() {}
inline SInt32 CFRunLoopRunInMode(CFStringRef, CFTimeInterval, bool) { return kCFRunLoopRunFinished; }
inline void CFRunLoopStop(CFRunLoopRef) {}
inline void CFRunLoopWakeUp(CFRunLoopRef) {}
inline void CFRetain(CFTypeRef) {}
inline void CFRelease(CFTypeRef) {}

struct CFRunLoopSourceContext {
	long version; void* info;
	void* (*retain)(void* info); void (*release)(void* info);
	void* copyDescription; void* equal; void* hash;
	void (*schedule)(void* info, CFRunLoopRef rl, CFStringRef mode);
	void (*cancel)(void* info, CFRunLoopRef rl, CFStringRef mode);
	void (*perform)(void* info);
};

inline CFRunLoopSourceRef CFRunLoopSourceCreate(void*, long, CFRunLoopSourceContext*) {
	static __CFRunLoopSource dummy{}; return &dummy;
}
inline void CFRunLoopAddSource(CFRunLoopRef, CFRunLoopSourceRef, CFStringRef) {}
inline void CFRunLoopRemoveSource(CFRunLoopRef, CFRunLoopSourceRef, CFStringRef) {}
inline void CFRunLoopSourceSignal(CFRunLoopSourceRef) {}

struct CFRunLoopTimerContext {
	long version; void* info;
	void* (*retain)(void* info); void (*release)(void* info);
	void* copyDescription;
};

typedef void (*CFRunLoopTimerCallBack)(CFRunLoopTimerRef timer, void* info);

inline CFRunLoopTimerRef CFRunLoopTimerCreate(void*, CFAbsoluteTime, CFTimeInterval, CFOptionFlags, long, CFRunLoopTimerCallBack, CFRunLoopTimerContext*) {
	static __CFRunLoopTimer dummy{}; return &dummy;
}
inline void CFRunLoopAddTimer(CFRunLoopRef, CFRunLoopTimerRef, CFStringRef) {}
inline void CFRunLoopRemoveTimer(CFRunLoopRef, CFRunLoopTimerRef, CFStringRef) {}
inline void CFRunLoopTimerSetNextFireDate(CFRunLoopTimerRef, CFAbsoluteTime) {}
inline void CFRunLoopTimerInvalidate(CFRunLoopTimerRef) {}

enum {
	kCFFileDescriptorReadCallBack = 1u << 0,
	kCFFileDescriptorWriteCallBack = 1u << 1
};

struct CFFileDescriptorContext {
	long version; void* info;
	void* (*retain)(void* info); void (*release)(void* info); void* copyDescription;
};

typedef void (*CFFileDescriptorCallBack)(CFFileDescriptorRef f, CFOptionFlags callBackTypes, void* info);

inline CFFileDescriptorRef CFFileDescriptorCreate(void*, int, bool, CFFileDescriptorCallBack, const CFFileDescriptorContext*) {
	static __CFFileDescriptor dummy{}; return &dummy;
}
inline int CFFileDescriptorGetNativeDescriptor(CFFileDescriptorRef) { return -1; }
inline void CFFileDescriptorEnableCallBacks(CFFileDescriptorRef, CFOptionFlags) {}
inline void CFFileDescriptorDisableCallBacks(CFFileDescriptorRef, CFOptionFlags) {}
inline CFRunLoopSourceRef CFFileDescriptorCreateRunLoopSource(void*, CFFileDescriptorRef, long) {
	static __CFRunLoopSource dummy{}; return &dummy;
}
inline void CFFileDescriptorInvalidate(CFFileDescriptorRef) {}

#define EVFILT_READ     (-1)
#define EVFILT_WRITE    (-2)
#define EVFILT_USER     (-11)

#define EV_ADD          0x0001
#define EV_DELETE       0x0002
#define EV_ENABLE       0x0004
#define EV_DISABLE      0x0008
#define EV_CLEAR        0x0020
#define NOTE_TRIGGER    0x01000000

struct kevent {
	RLEventFakeBridge ident;
	int16_t           filter;
	uint16_t          flags;
	uint32_t          fflags;
	intptr_t          data;
	void* udata;
};

inline void EV_SET(struct kevent* kevp, RLEventFakeBridge ident, int16_t filter, uint16_t flags, uint32_t fflags, intptr_t data, void* udata) {
	kevp->ident = ident; kevp->filter = filter; kevp->flags = flags;
	kevp->fflags = fflags; kevp->data = data; kevp->udata = udata;
}

inline int kqueue() { return 1; }
inline int kevent(int, const struct kevent*, int, struct kevent*, int, const struct timespec*) { return 0; }

typedef uint32_t thread_port_t;
typedef uint32_t thread_policy_flavor_t;
typedef int* thread_policy_t;

#define THREAD_AFFINITY_POLICY        4
#define THREAD_AFFINITY_POLICY_COUNT  1
#define KERN_SUCCESS                  0

struct thread_affinity_policy_data_t {
	int affinity_tag;
};

inline thread_port_t mach_thread_self() { return 1; }
inline thread_port_t mach_task_self() { return 1; }
inline int mach_port_deallocate(thread_port_t, thread_port_t) { return 0; }
inline int thread_policy_set(thread_port_t, thread_policy_flavor_t, thread_policy_t, uint32_t) { return 0; }

struct qos_class_t {
	int value;
	qos_class_t() : value(0) {}
	qos_class_t(int v) : value(v) {}
	operator int() const { return value; }
};

#define QOS_CLASS_USER_INTERACTIVE      0x21
#define QOS_CLASS_USER_INITIATED        0x19
#define QOS_CLASS_DEFAULT               0x15
#define QOS_CLASS_UTILITY               0x11
#define QOS_CLASS_BACKGROUND            0x09

inline int pthread_set_qos_class_np(pthread_t, qos_class_t q, int) { return 0; }
inline int pthread_set_qos_class_self_np(qos_class_t q, int) { return 0; }

#define OS_LOG_DEFAULT nullptr
inline void os_log(void*, const char*, ...) {}

using os_unfair_lock = struct { uint32_t _os_unfair_lock_opaque; };
#define OS_UNFAIR_LOCK_INIT os_unfair_lock{0}
inline void os_unfair_lock_lock(os_unfair_lock*) {}
inline void os_unfair_lock_unlock(os_unfair_lock*) {}
inline bool os_unfair_lock_trylock(os_unfair_lock*) { return true; }

#define SO_NOSIGPIPE 0x1022