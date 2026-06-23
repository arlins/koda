/** ****************************************************************************
 Created by: Arlin (arlins.dps@gmail.com)
 
 NamedEvent
 A cross-platform named event for cross-process synchronization. Supports 
 Manual/Auto Reset and Timeout. Synchronization within a single process 
 can be achieved using std::condition_variable. NamedEvent is thread-safe 
 and all APIs can be used from any threads. On Windows, NamedEvent is 
 implemented based on CreateEvent + WaitForMultipleObjects. On Unix, 
 it is based on Named Pipe(FIFO) + State(in SHM) + poll.
 **************************************************************************** **/

#pragma once
#include <string>
#include <atomic>
#include <cstdint>
#include <mutex>
#include "koda/kd_global.h"
#include "koda/base/kd_str.h"
#include "koda/base/kd_internals.h"
#include "koda/async/kd_namedmutex.h"

#if defined(KD_OS_WIN)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <poll.h>
#include <cerrno>
#endif

#ifdef KD_DEBUG
//#define KD_NDEVENT_ENABLE_DEBUG
#endif

#ifdef KD_NDEVENT_ENABLE_DEBUG
#define kd_ndevent_debug(fmt, ...) \
printf("[NDEVENT] " fmt "\n", ##__VA_ARGS__);
#else
#define kd_ndevent_debug(fmt, ...) do {} while(0);
#endif // KD_NDEVENT_ENABLE_DEBUG

__NAMESPACE_KD_BEGIN


// ====================
// NamedEvent
// ====================
enum class WaitEventResult {
    Success,
    Timeout,
    Interrupted,
    ErrorOccurred
};

class NamedEvent {
    KD_DISABLE_COPY(NamedEvent)
    KD_DISABLE_MOVE(NamedEvent)
    
public:
    NamedEvent(const std::string& name, bool auto_reset = true, bool initially_signaled = false);
    ~NamedEvent() noexcept;
    
    void reset();
    void signal();

    // Interrupt waiting operation
    void interrupt();
    WaitEventResult wait(int timeout_ms = -1);
    
    // This function will automatically reset the signal state
    // if automatic reset is enabled.
    bool isSignaled();
    
    bool isAutoReset() {
        return m_autoReset;
    }
    
private:
    bool m_autoReset {true};
    std::mutex  m_eventMtx; 

#if defined(KD_OS_WIN)
    HANDLE m_handle{ NULL };
    HANDLE m_interruptEvent{ NULL };
#else
    std::string m_fifoPath;
    int         m_fifoFd {-1};
    int         m_interruptPipe[2];
    uint32_t*   m_ptr{ nullptr };
    NamedMutex  m_stateMutex;
    
    int m_lifecycleFd {-1};

    bool _ensureFifoReadyNoLock();
    bool _readFifo(int fd, int bytes, bool& broken);
    bool _notifyFifo(int fd, bool& broken);
    
#endif
};

// ===============================
// Implementations
// ===============================

#if defined(KD_OS_WIN)
// ===============================
// Implementations - Win32
// ===============================

inline NamedEvent::NamedEvent(const std::string& name, bool auto_reset, bool initially_signaled)
: m_autoReset(auto_reset) {
    m_handle = CreateEventA(NULL, auto_reset ? FALSE : TRUE, initially_signaled ? TRUE : FALSE, name.c_str());
    m_interruptEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

    if (m_handle == NULL || m_interruptEvent == NULL) {
        kd_ndevent_debug("[ERROR] Failed to create NamedEvent");
        KD_ASSERT_M(false, "[ERROR] Failed to create NamedEvent");
    }
}

inline NamedEvent::~NamedEvent() noexcept {
    interrupt();

    std::lock_guard<std::mutex> lock(m_eventMtx);
    if (m_handle) {
        CloseHandle(m_handle);
        m_handle = NULL;
    }
    if (m_interruptEvent) {
        CloseHandle(m_interruptEvent);
        m_interruptEvent = NULL;
    }
}
inline void NamedEvent::signal() {
    HANDLE handle = NULL;
    {
        std::lock_guard<std::mutex> lock(m_eventMtx);
        handle = m_handle;
    }
    if (handle) {
        SetEvent(handle);
    }
}

inline void NamedEvent::reset() {
    HANDLE handle = NULL;
    {
        std::lock_guard<std::mutex> lock(m_eventMtx);
        handle = m_handle;
    }

    if (handle) {
        ResetEvent(handle);
    }
}

inline bool NamedEvent::isSignaled() {
    HANDLE handle = NULL;
    {
        std::lock_guard<std::mutex> lock(m_eventMtx);
        handle = m_handle;
    }
    if(handle == NULL) {
        return false; // Return false if event is invalid
    }

    return WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
}

inline void NamedEvent::interrupt() {
    HANDLE interruptHandle = NULL;
    {
        std::lock_guard<std::mutex> lock(m_eventMtx);
        interruptHandle = m_interruptEvent;
    }

    if (interruptHandle) {
        SetEvent(interruptHandle);
    }
}

inline WaitEventResult NamedEvent::wait(int timeout_ms) {
    HANDLE handles[2] = { nullptr, nullptr };
    {
        std::lock_guard<std::mutex> lock(m_eventMtx);
        if (!m_handle || !m_interruptEvent) {
            return WaitEventResult::ErrorOccurred;
        }
        handles[0] = m_handle;
        handles[1] = m_interruptEvent;
        ResetEvent(m_interruptEvent); // Reset the interrupt event
    }
    
    // Start waiting
    DWORD dwTimeout = (timeout_ms < 0) ? INFINITE : static_cast<DWORD>(timeout_ms);
    DWORD waitRes = WaitForMultipleObjects(2, handles, FALSE, dwTimeout);
    if (waitRes == WAIT_OBJECT_0) {
        return WaitEventResult::Success;
    } else if (waitRes == (WAIT_OBJECT_0 + 1)) {
        return WaitEventResult::Interrupted;
    } else if (waitRes == WAIT_TIMEOUT) {
        return WaitEventResult::Timeout;
    }
    
    return WaitEventResult::ErrorOccurred;
}

#else
// ===============================
// Implementations - UNIX
// Named FIFO + State(in SHM)
// ===============================

namespace ne_detail {
//
// Bit 0:    AutoReset status (0: ManualReset, 1: AutoReset)
// Bit 1:    Initialization status(unused) (0: Uninitialized, 1: Initialized)
// Bit 2:    Trigger status (0: Not triggered, 1: Triggered)
// Bit 3-31: Signal sequence number (29 bits in total)
//
static constexpr uint32_t MASK_AUTORESET = 0x00000001; // 0001
static constexpr uint32_t MASK_INIT      = 0x00000002; // 0010
static constexpr uint32_t MASK_SIGNALED  = 0x00000004; // 0100
static constexpr uint32_t MASK_SEQ       = 0xFFFFFFF8; // 1111...1000

static constexpr uint32_t SHIFT_INIT     = 1;
static constexpr uint32_t SHIFT_SIGNALED = 2;
static constexpr uint32_t SHIFT_SEQ      = 3;
static constexpr uint32_t MAX_SEQ_VAL    = 0x1FFFFFFF; // 29 bits max: 536870911

// Auto reset
inline bool getAutoReset(uint32_t val) {
    return (val & MASK_AUTORESET) != 0;
}

inline void setAutoReset(uint32_t& val, bool auto_reset) {
    if (auto_reset) {
        val |= MASK_AUTORESET;
    } else {
        val &= ~MASK_AUTORESET;
    }
}

// Initialized (unused now)
inline bool isInitialized(uint32_t val) {
    return (val & MASK_INIT) != 0;
}

inline void setInitialized(uint32_t& val, bool initialized) {
    if (initialized) {
        val |= MASK_INIT;
    } else {
        val &= ~MASK_INIT;
    }
}

// Signaled
inline bool getSignaled(uint32_t val) {
    return (val & MASK_SIGNALED) != 0;
}

inline void setSignaled(uint32_t& val, bool signaled) {
    if (signaled) {
        val |= MASK_SIGNALED;
    } else {
        val &= ~MASK_SIGNALED;
    }
}

// Sequence
inline uint32_t getSequence(uint32_t val) {
    return (val & MASK_SEQ) >> SHIFT_SEQ;
}

inline void increaseSequence(uint32_t& val) {
    uint32_t current_seq = getSequence(val);
    uint32_t next_seq = current_seq + 1;
    if (next_seq > MAX_SEQ_VAL) {
        next_seq = 0;
    }
    val = (val & ~MASK_SEQ) | (next_seq << SHIFT_SEQ);
}

inline bool isAnyProcessHolding(int fd) {
    if (fd < 0) {
        return false;
    }

    struct flock fl {};
    fl.l_type = F_WRLCK;// Exclusive write lock
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    // Check if any other process holds the lock.
    if (::fcntl(fd, F_SETLK, &fl) == 0) {
        // Successfully acquired the write lock! 
        // This means that no process in the entire 
        // system currently holds a read lock.
        // Unlock immediately after inspection.
        fl.l_type = F_UNLCK;
        ::fcntl(fd, F_SETLK, &fl);
        return false; 
    }
    
    // If it fails (returns -1, errno is EACCES or EAGAIN), 
    // it means that another live process is holding the 
    // read lock in the background.
    return true; 
}

inline bool holdLifecycleFd(int fd) {
    if (fd < 0) {
        return false;
    }
    
    struct flock fl {};
    fl.l_type = F_RDLCK; // Read lock
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    return ::fcntl(fd, F_SETLK, &fl) == 0;
}
} // namespace ne_detail


// NamedEvent
inline NamedEvent::NamedEvent(const std::string& name, bool auto_reset, bool initially_signaled)
: m_autoReset(auto_reset)
, m_fifoFd(-1)
, m_ptr(nullptr)
, m_stateMutex(name + "_knevst")
{
    // Init fifo
    std::string basePath = kd::crossProcessSharedDir("/namedevent");
    if (!basePath.empty() && basePath.back() != '/') {
        basePath += "/";
    }
    kd::create_directory(basePath);
    m_fifoPath = basePath + name + ".fifo";
    _ensureFifoReadyNoLock();
    
    if (::pipe(m_interruptPipe) == 0) {
        ::fcntl(m_interruptPipe[0], F_SETFL, O_NONBLOCK);
        ::fcntl(m_interruptPipe[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(m_interruptPipe[1], F_SETFL, O_NONBLOCK);
        ::fcntl(m_interruptPipe[1], F_SETFD, FD_CLOEXEC);
    } else {
        m_interruptPipe[0] = -1;
        m_interruptPipe[1] = -1;
    }

    // Open and init the state in SHM
    {
        NamedMutex initMutex(name + "_knevinit");
        std::lock_guard<NamedMutex> initLock(initMutex);

        // Format shm name, length of shm name is limited to 31 on Darwin
        std::string shmName = name + "knevs";
        if (shmName.length() > 28) {
            shmName = shmName.substr(0,10) + kd::str_hash(shmName); // 10+16
        }
        KD_ASSERT_M(shmName.length() < 30, "Length of name is limited to 30");
        shmName = "/" + shmName;

        // Open shm
        int shm_fd = ::shm_open(shmName.c_str(), O_RDWR | O_CREAT, 0666);
        if (shm_fd < 0) {
            KD_ASSERT_M(false, "shm_open failed");
            return;
        }
        
        // Allocate shm size
        struct stat st;
        if (::fstat(shm_fd, &st) == 0 && st.st_size < (off_t)sizeof(uint32_t)) {
            ::ftruncate(shm_fd, sizeof(uint32_t));
        }
        
        // Map shm
        void* addr = ::mmap(NULL, sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        ::close(shm_fd);
        if (addr == MAP_FAILED) {
            KD_ASSERT_M(false, "mmap failed");
            return;
        }
        m_ptr = static_cast<uint32_t*>(addr);
        
        // Check if shared memory needs to be initialized. 
        // We found that even after all processes holding shared memory 
        // for this event exited, the operating system did not release 
        // the shared memory, causing us to use old data. 
        // Therefore, we use an fcntl lock to keep track of whether any 
        // processes are still using this shared memory. If not, 
        // then we initialize the shared memory. 
        std::string lifecyclePath = basePath + name + ".lifecycle";
        m_lifecycleFd = ::open(lifecyclePath.c_str(), O_RDWR | O_CREAT, 0666);
        if (m_lifecycleFd >= 0) {
            // When a child process becomes a new program after `exec`, it silently 
            // inherits all file descriptors (fds) opened by the parent process. 
            // Adding `FD_CLOEXEC` means the kernel will automatically close the file 
            // descriptor when the child process executes `exec`.
            // Set close-on-exec to prevent FD inheritance.
            ::fcntl(m_lifecycleFd, F_SETFD, FD_CLOEXEC);
        }

        bool lifecycleHolded = ne_detail::isAnyProcessHolding(m_lifecycleFd);
        ne_detail::holdLifecycleFd(m_lifecycleFd);

        if (!lifecycleHolded) {
            uint32_t init_val = 0;
            bool init_signaled = (m_autoReset? false : initially_signaled);
            ne_detail::setAutoReset(init_val, auto_reset);
            ne_detail::setSignaled(init_val, init_signaled);
            
            *m_ptr = init_val;
            kd_ndevent_debug("Shm has been initialized");
        } else {
            bool shm_autoReset = ne_detail::getAutoReset(*m_ptr);
            if (m_autoReset != shm_autoReset) {
                m_autoReset = shm_autoReset;
                kd_ndevent_debug("[WARNING] Update auto_reset %d to the existing value %d", 
                    auto_reset, shm_autoReset);
            }
      
        }
    }
}

inline NamedEvent::~NamedEvent() noexcept {
    interrupt(); 

    std::lock_guard<std::mutex> lock(m_eventMtx);
    if (m_ptr) {
        ::munmap(m_ptr, sizeof(uint32_t));
        m_ptr = nullptr;
    }
    
    if (m_fifoFd >= 0) {
        ::close(m_fifoFd);
        m_fifoFd = -1;
    }
    if (m_lifecycleFd >= 0) {
        ::close(m_lifecycleFd);
        m_lifecycleFd = -1;
    }

    if (m_interruptPipe[0] >= 0) {
        ::close(m_interruptPipe[0]);
        m_interruptPipe[0] = -1;
    }
    if (m_interruptPipe[1] >= 0) {
        ::close(m_interruptPipe[1]);
        m_interruptPipe[1] = -1;
    }
}

inline void NamedEvent::signal() {
    std::lock_guard<std::mutex> lock(m_eventMtx);
    if (!m_ptr) {
        return;
    }

    {
        std::lock_guard<NamedMutex> lock(m_stateMutex);
        uint32_t val = *m_ptr;
        ne_detail::setSignaled(val, true);
        ne_detail::increaseSequence(val);
        *m_ptr = val;
    }

    // Ensure fifo ready
    if (m_fifoFd < 0 && !_ensureFifoReadyNoLock()) {
        return;
    }
    if (m_fifoFd < 0) { 
        return; 
    }
    
    // Weak up all waiters
    bool broken = false;
    _notifyFifo(m_fifoFd, broken);
    if (broken) { // Fifo broken, fix it
        _ensureFifoReadyNoLock();
        _notifyFifo(m_fifoFd, broken); // Retry
    }
}

inline void NamedEvent::reset() {
    std::lock_guard<std::mutex> lock(m_eventMtx);
    if (!m_ptr) {
        return;
    }
    
    {
        std::lock_guard<NamedMutex> lock(m_stateMutex);
        uint32_t old_val = *m_ptr;
        uint32_t next_val = old_val;
        ne_detail::setSignaled(next_val, false);
        *m_ptr = next_val;
    }
}

inline bool NamedEvent::isSignaled() {
    return wait(0) == WaitEventResult::Success;
}

inline void NamedEvent::interrupt() {
    int notify_fd = -1;
    {
        std::lock_guard<std::mutex> lock(m_eventMtx);
        notify_fd = m_interruptPipe[1];
    }

    if (notify_fd >= 0) {
        char dummy = 'I';
        ::write(notify_fd, &dummy, 1); 
    }
}

inline WaitEventResult NamedEvent::wait(int timeout_ms) {
    uint32_t waiting_seq = 0;
    {
        std::lock_guard<std::mutex> ev_lock(m_eventMtx);
        if (!m_ptr) {
            return WaitEventResult::ErrorOccurred;
        }
        
        {
            std::lock_guard<NamedMutex> lock(m_stateMutex);
            waiting_seq = ne_detail::getSequence(*m_ptr);
        }

        if (m_fifoFd < 0 && !_ensureFifoReadyNoLock()) {
            return WaitEventResult::ErrorOccurred;
        }
    }
    
    // Start to wait
    long long start_time = now_time();
    int remaining_ms = timeout_ms;

    while(true) {
        struct pollfd pfds[2];
        {
            std::lock_guard<std::mutex> lock(m_eventMtx);
            if (m_fifoFd < 0) {
                return WaitEventResult::ErrorOccurred; // Failed
            }

            pfds[0].fd = m_fifoFd;
            pfds[0].events = POLLIN;
            
            pfds[1].fd = m_interruptPipe[0];
            pfds[1].events = POLLIN;
        }
        
        // Poll
        int ret = 0;
        do {
            ret = ::poll(pfds, 2, remaining_ms);
        } while (ret == -1 && errno == EINTR);
        
        // Check poll result
        if(ret == 0) { // Timeout
            return WaitEventResult::Timeout; // Failed
        }
        if (ret < 0) { // System-level error
            return WaitEventResult::ErrorOccurred; // Failed
        }

        bool isFd0Signaled = (pfds[0].revents != 0);
        bool isFd1Signaled = (pfds[1].revents != 0);
        bool isFd0PollIn = bool(pfds[0].revents & POLLIN);
        bool isFd1PollIn = bool(pfds[1].revents & POLLIN);

        // Interrupted fd(pfds[1]) signaled
        if (isFd1Signaled) { 
            bool fd1ErrorOccurred = (pfds[1].revents & (POLLERR | POLLHUP | POLLNVAL));

            // Read 1 byte from fifo if fifo signaled
            if(isFd0PollIn && (isFd1PollIn || fd1ErrorOccurred)) {
                bool broken = false;
                _readFifo(pfds[0].fd, 1, broken);
                if (broken) { // Fifo broken, fix it
                    std::lock_guard<std::mutex> lock(m_eventMtx);
                    _ensureFifoReadyNoLock();
                }
            }
            
            // Check if interrupted or error
            if (isFd1PollIn) {
                char dummy[1024];
                while (::read(pfds[1].fd, dummy, sizeof(dummy)) > 0);
                return WaitEventResult::Interrupted; // Failed
            }
            if (fd1ErrorOccurred) {
                return WaitEventResult::ErrorOccurred;
            }

        }
        
        // Fifo fd (pfds[0]) signaled
        bool success = false;
        if(isFd0Signaled) {  
            if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) { // Fifo error
                // POLLNVAL: Invalid FD
                // POLLHUP: Hang Up, the other end of the pipe is closed
                // POLLERR: An error has occurred in the equipment or piping.  
                std::lock_guard<std::mutex> lock(m_eventMtx);
                _ensureFifoReadyNoLock(); // Fifo broken, fix it
                return WaitEventResult::ErrorOccurred; // Failed
            }

            // Reconfirm the status
            if(isFd0PollIn) {
                // As long as poll is activated, we always read 
                // 1 byte from FIFO if polled in without lock
                bool broken = false;
                bool readed = _readFifo(pfds[0].fd, 1, broken);
                if (broken) { // Fifo broken, fix it
                    std::lock_guard<std::mutex> lock(m_eventMtx);
                    _ensureFifoReadyNoLock();
                }
                
                if (readed || !m_autoReset) {
                    std::lock_guard<std::mutex> ev_lock(m_eventMtx);
                    std::lock_guard<NamedMutex> lock(m_stateMutex);

                    uint32_t current_val = *m_ptr;
                    bool is_signaled = ne_detail::getSignaled(current_val);
                    uint32_t current_seq = ne_detail::getSequence(current_val);
                    
                    if (m_autoReset) { // Auto reset
                        if (is_signaled) {
                            success = true;
                            ne_detail::setSignaled(current_val, false);
                            *m_ptr = current_val;
                        }
                    } else { // Manual reset
                        if (is_signaled || (current_seq != waiting_seq)) {
                            success = true;
                        }
                    }
                }
            }

            if (success) {
                return WaitEventResult::Success; // Success
            }
        } // isFd1Signaled


        // It might be a false wake-up.
        // Recalculate the remaining time and continue to wait.
        if (timeout_ms >= 0) {
            long long elapsed = now_time() - start_time;
            remaining_ms = timeout_ms - static_cast<int>(elapsed);
            if (remaining_ms <= 0) {
                return WaitEventResult::Timeout; // Failed
            }
        }
        
        // Yield if not succeed
        if( !success) {
            std::this_thread::yield(); 
        }

    } // while(true)

    kd_ndevent_debug("Unknown error detected while waiting");
    return WaitEventResult::ErrorOccurred;
}

// Ensure the fifo is vaild
inline bool NamedEvent::_ensureFifoReadyNoLock() {
    if (m_fifoFd >= 0) {
        ::close(m_fifoFd);
        m_fifoFd = -1;
    }

    mode_t old_mask = ::umask(0);
    int mk_ret = ::mkfifo(m_fifoPath.c_str(), 0666);
    ::umask(old_mask);

    m_fifoFd = ::open(m_fifoPath.c_str(), O_RDWR | O_NONBLOCK);

    if (m_fifoFd >= 0) {
        ::fcntl(m_fifoFd, F_SETFD, FD_CLOEXEC);
        return true;
    }
    return false;
}

// Read specified bytes from fifo
inline bool NamedEvent::_readFifo(int fd, int bytes, bool& broken) {
    broken = false;
    if (fd < 0 || bytes <= 0) {
        return false;
    }

    const int kBufferSize = 1024;
    char dummy[kBufferSize];
    int nbyte = (bytes > kBufferSize ? kBufferSize : bytes);
    
    long ret = -1;
    do {
        // EINTR: Interrupted by os, retry
        ret = ::read(fd, &dummy, nbyte);
    } while (ret == -1 && errno == EINTR);
    
    if (ret < 0 && (!(errno == EAGAIN || errno == EWOULDBLOCK))) {
        broken = true; // Pipe is broken
    }
    return ret > 0;
}

// Write 1 byte into fifo to wake up all waiters
inline bool NamedEvent::_notifyFifo(int fd, bool& broken) {
    broken = false;
    if (fd < 0) {
        return false;
    }

    char dummy = 'W';
    long ret = ::write(fd, &dummy, 1);
    if (ret < 0 && (errno == EPIPE || errno == EBADF || errno == ENOENT)) {
        broken = true; // Fifo broken, reopen
    }
    return ret > 0;
}

#endif // KD_OS_WIN

__NAMESPACE_KD_END
