/** *****************************************************************
Created by: Arlin (arlins.dps@gmail.com)
SharedMemory: A cross-platform shared memory implementation
***************************************************************** **/

#pragma once
#include <string>
#include <cstdint>
#include <cstring>
#include <atomic>

#include "koda/kd_global.h"
#include "koda/base/kd_scopeguard.h"
#include "koda/async/kd_namedmutex.h"
#include "koda/async/kd_namedevent.h"

#if defined(KD_OS_WIN)
#include <windows.h>
#elif defined(KD_OS_UNIX)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#endif

__NAMESPACE_KD_BEGIN

namespace shm_detail {
inline std::string ensureStartingWithSlash(const std::string& name) {
	if (name.empty() || name[0] != '/') {
		return "/" + name;
	}
	return name;
}

#ifdef KD_OS_WIN
inline bool generateSecurity(SECURITY_ATTRIBUTES& sa, SECURITY_DESCRIPTOR& sd) {
	if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
		return false;

	if (!SetSecurityDescriptorDacl(&sd, TRUE, (PACL)NULL, FALSE))
		return false;

	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = &sd;
	sa.bInheritHandle = FALSE;
	return true;
}
#endif
}

// =====================
// SharedMemory
// =====================
class SharedMemory {
	KD_DISABLE_COPY(SharedMemory)
	KD_DISABLE_MOVE(SharedMemory)

public:
	enum class Error {
		NoError,
		PermissionDenied,
		SecurityError,
		LockError,
		NotFound,
		InvalidSize,
		AlreadyExists,
		CreateError,
		MapError,
		UnknownError
	};

	static constexpr int kSharedMemoryHeaderSize = 16;
	static constexpr int kOffsetTotalCapacity = 0;
	static constexpr int kOffsetDataSize = 4;

public:
	SharedMemory(const std::string& key, bool auto_reset = true)
		: m_key(key)
		, m_mutex(key + "_DD74EBB92DD84F1F_ksmtx")
		, m_event(key + "_F00309157B08465CA_ksmev", auto_reset) {
	}

	~SharedMemory() {
		detach();
	}

	Error create(uint32_t capacity) {
		if (capacity == 0) {
			return Error::InvalidSize;
		}
		if (!lock()) {
			return Error::LockError;
		}

		// Automatically unlock upon exit
		ScopeGuard guard([this] { unlock(); });
		uint32_t totalSize = capacity + kSharedMemoryHeaderSize;

#if defined(KD_OS_WIN)
		SECURITY_ATTRIBUTES sa = { 0 };
		SECURITY_DESCRIPTOR sd = { 0 };
		if (!shm_detail::generateSecurity(sa, sd)) {
			return Error::SecurityError;
		}

		m_hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa,
			PAGE_READWRITE, 0, totalSize, m_key.c_str());
		if (NULL == m_hMapFile) {
			return Error::CreateError;
		}
		if (GetLastError() == ERROR_ALREADY_EXISTS) {
			CloseHandle(m_hMapFile);
			m_hMapFile = NULL;
			return Error::AlreadyExists;
		}

		m_pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
#else
		// Open shm
		std::string shmPath = _shmPath();
		int fd = shm_open(shmPath.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
		if (fd == -1) {
			if (errno == EEXIST) {
				return Error::AlreadyExists;
			}
			return Error::CreateError;
		}

		// Allocate shm size
		if (ftruncate(fd, totalSize) == -1) {
			close(fd);
			shm_unlink(shmPath.c_str());
			return Error::CreateError;
		}

		// Map shm
		m_pBuffer = mmap(NULL, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		close(fd);
		if (m_pBuffer == MAP_FAILED) {
			m_pBuffer = nullptr;
			shm_unlink(shmPath.c_str());
			return Error::MapError;
		}
#endif

		if (nullptr == m_pBuffer) {
			return Error::MapError;
		}

		// Initialize Header
		memset(m_pBuffer, 0, totalSize);
		m_capacity = capacity;
		*(uint32_t*)((char*)m_pBuffer + kOffsetTotalCapacity) = capacity; // Container size
		*(uint32_t*)((char*)m_pBuffer + kOffsetDataSize) = 0; // Data size

		return Error::NoError;
	}

	Error attach() {
		if (m_pBuffer != nullptr) {
			return Error::NoError;
		}

#if defined(KD_OS_WIN) // Win
		// Open the file
		if (NULL == m_hMapFile) {
			m_hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, m_key.c_str());
			if (NULL == m_hMapFile) {
				DWORD err = GetLastError();
				if (err == ERROR_ACCESS_DENIED) {
					return Error::PermissionDenied;
				}
				if (err == ERROR_FILE_NOT_FOUND) {
					return Error::NotFound;
				}
				return Error::UnknownError;
			}
		}

		// Map the file and read the capacity
		void* pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		if (nullptr == pBuffer) {
			return Error::MapError;
		}

		if (!lock()) {
			UnmapViewOfFile(pBuffer);
			return Error::LockError;
		}

		ScopeGuard unlockGuard([this] { unlock(); });
		m_pBuffer = pBuffer;
		m_capacity = *(uint32_t*)((char*)m_pBuffer + kOffsetTotalCapacity);
		return Error::NoError;

#else // Unix
		// Map the file
		int fd = shm_open(_shmPath().c_str(), O_RDWR, 0666);
		if (fd == -1) {
			if (errno == ENOENT) {
				return Error::NotFound;
			}
			return Error::UnknownError;
		}

		// Lock to read data
		if (!lock()) {
			close(fd);
			return Error::LockError;
		}

		// Automatically clean upon exit
		ScopeGuard cleanGuard([this, fd] {
			this->unlock();
			::close(fd);
		});

		// Map minimum length to read Header
		void* headerPtr = mmap(NULL, kSharedMemoryHeaderSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (headerPtr == MAP_FAILED) {
			return Error::MapError;
		}

		// Read the actual capacity and calculate the total size
		uint32_t capacity = *(uint32_t*)((char*)headerPtr + kOffsetTotalCapacity);
		uint32_t totalSize = capacity + kSharedMemoryHeaderSize;
		munmap(headerPtr, kSharedMemoryHeaderSize); // Unmap

		// Perform an actual full mapping
		void* pBuffer = mmap(NULL, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (pBuffer == MAP_FAILED) {
			return Error::MapError;
		}

		m_pBuffer = pBuffer;
		m_capacity = capacity;
		return Error::NoError;
#endif // KD_OS_WIN
	}

	void detach() {
#if defined(KD_OS_WIN)
		if (m_pBuffer) {
			UnmapViewOfFile(m_pBuffer);
			m_pBuffer = nullptr;
		}
		if (m_hMapFile) {
			CloseHandle(m_hMapFile);
			m_hMapFile = NULL;
		}
#else
		if (m_pBuffer) {
			uint32_t totalSize = m_capacity + kSharedMemoryHeaderSize;
			munmap(m_pBuffer, totalSize);
			m_pBuffer = nullptr;
		}
#endif

		m_capacity = 0;
	}

	// Explicitly remove the SHM from system
	// For Unix only, Win handles this by closing last handle
	void remove() {
		detach();
#if defined(KD_OS_UNIX)
		shm_unlink(_shmPath().c_str());
#endif
	}

	bool lock() {
		return m_mutex.lock();
	}

	void unlock() {
		m_mutex.unlock();
	}

	void emitEvent() {
		m_event.signal();
	}
    
    bool isAutoResetEvent() {
        return m_event.isAutoReset();
    }
    
	void resetEvent() {
		m_event.reset();
	}

    WaitEventResult waitEvent(int timeoutMs = -1) {
		return m_event.wait(timeoutMs);
	}

	void* data() {
		if (m_pBuffer == nullptr) {
			return nullptr;
		}

		return (char*)m_pBuffer + kSharedMemoryHeaderSize;
	}

	uint32_t capacity() const {
		return m_capacity;
	}

	void setDataSize(uint32_t len) {
		if (m_pBuffer && len <= m_capacity) {
			*(uint32_t*)((char*)m_pBuffer + kOffsetDataSize) = len;
		}
	}

	uint32_t getDataSize() {
		if (!m_pBuffer) {
			return 0;
		}

		uint32_t size = *(uint32_t*)((char*)m_pBuffer + kOffsetDataSize);
		return size;
	}

private:
	std::string _shmPath() {
		std::string shmPath = shm_detail::ensureStartingWithSlash(m_key);
		shmPath += "_kdshm";
		return shmPath;
	}

private:
	std::string m_key;
	NamedMutex m_mutex;
	NamedEvent m_event;
	uint32_t m_capacity{ 0 };
	void* m_pBuffer{ nullptr };

#if defined(KD_OS_WIN)
	HANDLE m_hMapFile{ NULL };
#endif
};

__NAMESPACE_KD_END
