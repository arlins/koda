/** *******************************************************************
Created by: Arlin (arlins.dps@gmail.com).

NamedMutex
A cross-platform named mutex for cross-process synchronization
based on system native APIs. Tt is thread-safe, all APIs can be 
used by any threads.On Windows, NamedMutex is based on Mutex 
Kernel Object. On Unix, NamedMutex is based on Record Lock (fcntl). 

[!] About Record Lock (fcntl) on Unix
1. Closing any file descriptor (FD) of a file within the same process will cause that 
    process to lose all locks.
2. If Thread A already holds a NamedMutex within the same process, and Thread B 
    also calls lock(), the system will automatically allow it (because they belong to the 
	same process, the kernel considers them to be the same holder).
3. In short, fcntl locks are based on "process" rather than "thread," 
    so NamedMutex can only be used in multi-process scenarios. and only one lock 
	with the same name/file descriptor can be held within a process.

[!] Cross-process Mutex implementations on Unix:
1. Record Lock (fcntl)
	This is the default implementation of named mutex on Unix we are using.
	Record locking is part of the POSIX standard.

	Once a lock file is created, automatic deletion is not recommended, as it can
	trigger race conditions, leading to file remnants. Closing any descriptor of the
	locked file within the same process will cause all locks held by that process on
	that file to be lost. The lock is automatically released when the program crashes

2. File Lock (flock)
	File Lock are not part of the POSIX standard; a flock lock is associated with 
	a file descriptor table entry. Multiple file descriptors (fds) pointing to the 
	same file can be safely opened within the same process. The lock is automatically 
	released when the program crashes.

	However, in certain Unix/POSIX environments (especially early macOS, some older 
	Linux kernels, or cross-platform mounted network file systems such as NFS/SMB), 
	there are potential problems, inconsistent behavior, or even complete failure.

3. Semaphores (sem_open):
	If a process crashes (SIGKILLs) while holding a semaphore, the semaphore will remain
	locked indefinitely (the reference count will not automatically reset to zero), causing
	other processes to be permanently blocked. This error persisted until the system 
	restarted.

4. Shared Memory + Mutex (pthread_mutex_t in shm):
	Using a pthread_mutex in shared memory with PTHREAD_PROCESS_SHARED
	attribute.  On macOS or Other Unix-like systems unsupported 
	PTHREAD_MUTEX_ROBUST, if the program crashes while locking Mutex , 
	Mutex will throw exceptions, even after the process is restarted. These exceptions 
	will persist until the system restarted .
************************************************************************* **/

#pragma once
#include <string>
#include <cstdlib>
#include <mutex>
#include <set>
#include "koda/kd_global.h"
#include "koda/base/kd_internals.h"

#if defined(KD_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/mman.h>
#endif

//====================
// Defines
//====================
#ifdef KD_DEBUG
//#define KD_NDMUTEX_ENABLE_DEBUG
#endif

#ifdef KD_NDMUTEX_ENABLE_DEBUG
#define kd_ndmutx_debug(fmt, ...) \
printf("[NDMUTEX] " fmt "\n", ##__VA_ARGS__);
#else
#define kd_ndmutx_debug(fmt, ...) do {} while(0);
#endif // KD_NDMUTEX_ENABLE_DEBUG

__NAMESPACE_KD_BEGIN

// ====================
// NamedMutex
// ====================
class NamedMutex {
	KD_DISABLE_COPY(NamedMutex)
	KD_DISABLE_MOVE(NamedMutex)

public:
	~NamedMutex();
	explicit NamedMutex(const std::string& name);
#ifndef KD_OS_WIN
	explicit NamedMutex(const std::string& name, int fd, bool takeOwnership);
#endif

	bool lock();
	void unlock() noexcept;
	bool try_lock() noexcept;

private:
	std::mutex m_mtx;
	std::string m_name;

#if defined(KD_OS_WIN)
	HANDLE m_handle{ nullptr };
#else
	int m_fd{ -1 };
	bool m_isOwned{ false };
#endif // KD_OS_WIN
};

__NAMESPACE_KD_END


// ===============================
// Implementations
// ===============================

__NAMESPACE_KD_BEGIN

#if defined(KD_OS_WIN)
// =========================
// Implementation - Win32
// =========================

inline NamedMutex::NamedMutex(const std::string& name)
	: m_name(name) {
	m_handle = CreateMutexA(NULL, FALSE, name.c_str());
	if ( m_handle == NULL ) {
		kd_ndmutx_debug("[ERROR] Failed to create NamedMutex, err = %d", GetLastError());
		KD_ASSERT_M(false, "[ERROR] Failed to create NamedMutex");
	}
}

inline NamedMutex::~NamedMutex() {
	std::lock_guard<std::mutex> mtx_lock(m_mtx);
	if (m_handle) {
		CloseHandle(m_handle);
		m_handle = nullptr;
	}
}

inline bool NamedMutex::lock() {
	HANDLE handle = NULL;
	{
		std::lock_guard<std::mutex> mtx_lock(m_mtx);
		handle = m_handle;
	}
	if(handle == NULL) {
		return false;
	}

	DWORD result = WaitForSingleObject(handle, INFINITE);
	// WAIT_ABANDONED: Owner process crashed, OS released the lock. 
	// We still own it now, which is the desired behavior for a robust mutex.
	KD_ASSERT_M(result == WAIT_OBJECT_0 || result == WAIT_ABANDONED, "Failed to lock");
	return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
}

inline void NamedMutex::unlock() noexcept {
	HANDLE handle = NULL;
	{
		std::lock_guard<std::mutex> mtx_lock(m_mtx);
		handle = m_handle;
	}
	if(handle == NULL) {
		return;
	}

	if (handle) {
		ReleaseMutex(handle);
	}
}

inline bool NamedMutex::try_lock() noexcept {
	HANDLE handle = NULL;
	{
		std::lock_guard<std::mutex> mtx_lock(m_mtx);
		handle = m_handle;
	}
	if(handle == NULL) {
		return false;
	}

	DWORD result = WaitForSingleObject(handle, 0);
	return (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED);
}

#else
// ===========================================
// Implementation (Record Lock) - Unix (Darwin/Linux/Android)
// ===========================================

#ifdef KD_DEBUG
namespace nd_detail {
	inline std::set<std::string>& globalNamedMutexs() {
		static std::set<std::string> namedMutexs;
		return namedMutexs;
	}
	
	inline std::mutex& globalNamedMutexsLock() {
		static std::mutex mtx;
		return mtx;
	}

	inline void addGlobalNamedMutex(const std::string& name) {
		if (name.empty()) {
			return;
		}

		std::lock_guard<std::mutex> lock(globalNamedMutexsLock());
		auto& namedMutexs = globalNamedMutexs();
		auto it = namedMutexs.find(name);
		if (it != namedMutexs.end()) {
			KD_ASSERT_M(false, 
				"Can not use NamedMutex with the same name simultaneously"
				" within a single process.");
		} else {
			namedMutexs.insert(name);
		}
	}

	inline void removeGlobalNamedMutex(const std::string& name) {
		if (name.empty()) {
			return;
		}

		std::lock_guard<std::mutex> lock(globalNamedMutexsLock());
		auto& namedMutexs = globalNamedMutexs();
		auto it = namedMutexs.find(name);
		if (it != namedMutexs.end()) {
			namedMutexs.erase(it);
		}
	}
};
#endif

inline NamedMutex::NamedMutex(const std::string& name)
	: m_name(name) {
	std::string fileDir = kd::crossProcessSharedDir("/namedmutex");
	if (!fileDir.empty() && fileDir.back() != '/') {
		fileDir += "/";
	}
	kd::create_directory(fileDir);

	// O_CLOEXEC: 
	// When a child process becomes a new program after `exec`, it silently 
	// inherits all file descriptors (fds) opened by the parent process. 
	// Adding `FD_CLOEXEC` means the kernel will automatically close the file 
	// descriptor when the child process executes `exec`.
	// Set close-on-exec to prevent FD inheritance.
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

	std::string fullPath = fileDir + name + ".flock";
	m_fd = open(fullPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
	if (m_fd == -1) {
		kd_ndmutx_debug("[ERROR] Failed to create NamedMutex (open lock file failed), err = %d", errno);
		KD_ASSERT_M(false, "[ERROR] Failed to create NamedMutex (open lock file failed)");
		return;
	}

#ifdef KD_DEBUG
	nd_detail::addGlobalNamedMutex(m_name);
#endif
	m_isOwned = true;

	// System is extremely old
	// Fall back if O_CLOEXEC is defined but not supported by the kernel at runtime
	if (m_fd != -1 && O_CLOEXEC == 0) {
		int flags = fcntl(m_fd, F_GETFD);
		if (flags != -1) {
			fcntl(m_fd, F_SETFD, flags | FD_CLOEXEC);
		}
	}
}

inline NamedMutex::NamedMutex(const std::string& name, int fd, bool takeOwnership) 
	: m_name(name) {
#ifdef KD_DEBUG
	nd_detail::addGlobalNamedMutex(name);
#endif
	m_fd = fd;
	m_isOwned = takeOwnership;
}

inline NamedMutex::~NamedMutex() {
	std::lock_guard<std::mutex> mtx_lock(m_mtx);
	if (m_fd != -1 && m_isOwned) {
		// Closing the descriptor automatically releases 
		// all fcntl locks held by this process.
		close(m_fd);
		m_fd = -1;
	}

#ifdef KD_DEBUG
	nd_detail::removeGlobalNamedMutex(m_name);
#endif
}

inline bool NamedMutex::lock() {
	int fd = -1;
	{
		std::lock_guard<std::mutex> mtx_lock(m_mtx);
		fd = m_fd;
	}
	if (fd == -1) {
		return false;
	}

	struct flock fl {};
	fl.l_type = F_WRLCK;    // Exclusive write lock
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;  // Lock the entire file

	int result;
	do {
		// F_SETLKW: Blocking wait for the lock
		result = fcntl(fd, F_SETLKW, &fl);
	} while (result == -1 && errno == EINTR); // Survive signal interruptions
	
	bool success = (result == 0);
	if (!success) {
		kd_ndmutx_debug("[ERROR] Failed to lock NamedMutex, err = %d", errno);
		KD_ASSERT_M(false, "Failed to lock NamedMutex");
	}

	return success;
}

inline void NamedMutex::unlock() noexcept {
	int fd = -1;
	{
		std::lock_guard<std::mutex> mtx_lock(m_mtx);
		fd = m_fd;
	}
	if (fd == -1) {
		return;
	}

	struct flock fl {};
	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	int result = fcntl(fd, F_SETLK, &fl);

	bool success = (result == 0);
	if (!success) {
		kd_ndmutx_debug("[ERROR] Failed to unlock NamedMutex, err = %d", errno);
		KD_ASSERT_M(false, "Failed to unlock NamedMutex");
	}
}

inline bool NamedMutex::try_lock() noexcept {
	int fd = -1;
	{
		std::lock_guard<std::mutex> mtx_lock(m_mtx);
		fd = m_fd;
	}
	if (fd == -1) {
		return false;
	}

	struct flock fl {};
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	// F_SETLK: Non-blocking attempt to acquire the lock
	int result = fcntl(fd, F_SETLK, &fl);
	return result == 0;
}

#endif // KD_OS_WIN

__NAMESPACE_KD_END