/** ***************************************************************************
Created by: Arlin (arlins.dps@gmail.com).

Implementation of IPCConnectionImpl on Unix
It is implemented based on UDS (Unix Domain Sockets).
On Android/Linux UDS is based on Abstract Namespace addressing . 
On other Unix-like OS (iOS/macOS...), UDS is based on Pathname addressing.
************************************************************************** **/

#pragma once
#include "koda/ipc/kd_ipcdefs.h"

// =====================
// Implementation - Unix
// =====================
#if defined(KD_OS_UNIX)

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <string>
#include <vector>
#include <cstring>
#include <functional>
#include "koda/base/kd_str.h"
#include "koda/base/kd_utils.h" 
#include "koda/base/kd_internals.h"

__NAMESPACE_KD_BEGIN

namespace ipc_detail {
inline void setNonBlocking(int fd) {
    ::fcntl(fd, F_SETFL, O_NONBLOCK);
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
}

inline bool checkSocketPath(const std::string& socketPath) {
    struct sockaddr_un addr{};
    return socketPath.size() < sizeof(addr.sun_path);
}

inline socklen_t addressLength(bool useAbstractName, size_t pathLen) {
    size_t addrLen = (useAbstractName
                        ? (offsetof(struct sockaddr_un, sun_path) + pathLen)
                        : sizeof(struct sockaddr_un) );
    return (socklen_t)(addrLen);
}

inline void clearPipe(int fd) {
    if (fd >= 0) {
        char dummy[1024];
        while (::read(fd, dummy, sizeof(dummy)) > 0);
    }
}

// Setup SO_NOSIGPIPE on Darwin to against SIGPIPE crashes
// caused by sudden shutdown of the peer.
inline void disableSigPipe(int fd) {
#if defined(KD_OS_DARWIN)
    if (fd >= 0) {
        int value = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value));
    }
#endif
}

} // namespace ipc_detail

// IPCConnectionImplUnix
class IPCConnectionImplUnix : public IPCConnectionImpl {
public:
    IPCConnectionImplUnix(IPCDelegate* delegate, const std::string& channel, bool isServer,
                          unsigned int readBufferSize, unsigned int writeBufferSize)
        : IPCConnectionImpl(delegate, channel, isServer, readBufferSize, writeBufferSize) {

#if defined(KD_OS_LINUX)
        // Use Abstract Namespace UDS on Linux
        m_socketPath = std::string("\0", 1) + "kd_ipc_" + channel;
        m_useAbstractName = true;
#else
        std::string basePath = kd::crossProcessSharedDir("/ipc");
        kd::create_directory(basePath);
        if (!basePath.empty() && basePath.back() != '/') {
            basePath += "/";
        }

        // .ipcsock file is not display in Finder on Darwin
        m_socketPath = basePath + channel + ".ipcsock";
        m_socketLockPath   = m_socketPath + ".flock";
        m_useAbstractName = false;
#endif

        KD_ASSERT_M(ipc_detail::checkSocketPath(m_socketPath), 
        "Socket path length is limited to 1024");

        // Create interrupt pipe
        if (::pipe(m_interruptPipe) == 0) {
            ipc_detail::setNonBlocking(m_interruptPipe[0]);
            ipc_detail::setNonBlocking(m_interruptPipe[1]);
        }
    }

    virtual ~IPCConnectionImplUnix() {
        _cleanUpSocket();
        
        std::lock_guard<IPCMutex> lock(m_connMtx);
        if (m_interruptPipe[0] >= 0) {
            ::close(m_interruptPipe[0]);
            m_interruptPipe[0] = -1;
        }
        if (m_interruptPipe[1] >= 0) {
            ::close(m_interruptPipe[1]);
            m_interruptPipe[1] = -1;
        }
    }

public:
    bool open(const std::function<void()>& aboutToConnect,
              const std::function<void(bool)>& didConnected) override {
        ipc_detail::clearPipe(m_interruptPipe[0]);
        
        if (m_isServer) {
            return _openServer(aboutToConnect, didConnected);
        } else {
            return _openClient(aboutToConnect, didConnected);
        }
    }

    void close() override {
        _cleanUpSocket();
    }

    bool ready() override {
        std::lock_guard<IPCMutex> lock(m_connMtx);
        return m_socketFd >= 0;
    }

    void interrupt() override {
		int fd = -1;
		{
			std::lock_guard<IPCMutex> lock(m_connMtx);
			fd = m_interruptPipe[1];
		}

        if (fd >= 0) {
            char dummy = 'I';
            ::write(fd, &dummy, 1);
        }
    }

    bool read(void* data, uint32_t size) override {
        if (size == 0 || data == nullptr) {
            return true;
        }

        int socketFd = -1;
        int interruptFd = -1;
        {
            std::lock_guard<IPCMutex> lock(m_connMtx);
            socketFd = m_socketFd;
            interruptFd = m_interruptPipe[0];
        }
        if (socketFd < 0 || interruptFd < 0) {
            kd_ipc_debug("Read failed: invaild fd");
            return false; // Failed
        }
        
        uint32_t totalRead = 0;
        char* pBuffer = static_cast<char*>(data);

        while (totalRead < size && !m_delegate->isQuit()) {
            // Read
            uint32_t bytesToRead = size - totalRead;
            ssize_t readRes = ::read(socketFd, pBuffer + totalRead, bytesToRead);

            // Check read result
            if (readRes > 0) { // Read successfully
                totalRead += readRes;
                continue; // Continue
            }
            if (readRes == 0) { // EOF
                // readRes = 0 indicates that all write processes have closed 
                // the file descriptor (fd). The fd contains no data and no 
                // more data will be added in the future.
                kd_ipc_debug("Read failed: EOF (peer closed)");
                return false; // Failed
            }
            if (readRes < 0 && errno == EINTR) { // Interrupted by os
                continue; // Continue
            }

            // Read: readRes < 0
            // No more data available and need to wait for more
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfds[2];
                pfds[0].fd = socketFd;
                pfds[0].events = POLLIN;
                pfds[1].fd = interruptFd;
                pfds[1].events = POLLIN;
                
                // Poll
                int pollRes = -1;
                do {
                    pollRes = ::poll(pfds, 2, -1);
                } while (pollRes == -1 && errno == EINTR);
                
                // Check result
                if (pollRes <= 0) {
                    kd_ipc_debug("Read failed: poll system error or timeout");
                    return false; // Failed
                }
                if ( !(pfds[0].revents & POLLIN) || m_delegate->isQuit() ) {
                    kd_ipc_debug("Read failed: Socket broken/interrupted: res = %d, sockevt = %d, quit = %d", 
                        pollRes, pfds[0].revents, m_delegate->isQuit());
                    return false; // Failed
                }
                    
                // Continue to read
                continue; 
            } else {
                kd_ipc_debug("Read failed: other error %d", errno);
                return false; // Failed
            }
        } // while

        // Finally
		bool success = (totalRead == size);
		if (!success) {
			kd_ipc_debug("Read unfinished: partial data readed (%u/%u). Quit signaled: %d",
				totalRead, size, m_delegate->isQuit());
		}

		return success;
    }

    bool write(const void* data, uint32_t size) override {
        if (size == 0 || data == nullptr) {
            return true;
        }

        int socketFd = -1;
        int interruptFd = -1;
        {
            std::lock_guard<IPCMutex> lock(m_connMtx);
            socketFd = m_socketFd;
            interruptFd = m_interruptPipe[0];
        }
        if (socketFd < 0 || interruptFd < 0) {
            kd_ipc_debug("Write failed: invaild fd");
            return false; 
        }
        
        uint32_t totalWritten = 0;
        const char* pBuffer = static_cast<const char*>(data);

        while (totalWritten < size && !m_delegate->isQuit()) {
            // Write
            // Use MSG_NOSIGNAL to defend against SIGPIPE crashes 
            // caused by sudden shutdown of the peer.
            uint32_t bytesToWrite = size - totalWritten;
            ssize_t writeRes = ::send(socketFd, pBuffer + totalWritten, bytesToWrite, MSG_NOSIGNAL);

            // Check write result
            if (writeRes > 0) { // // Write successfully
                totalWritten += writeRes;
                continue; // Continue
            }
            if (writeRes == 0) {
                // When `send` returns 0 and `size > 0`, it typically indicates 
                // a non-fatal window blocking event at the protocol stack level, 
                // or that the peer closed the connection at a very subtle moment, 
                // leaving the kernel in a gray area of ​​state transition. 
                // In this case, we return `false`.
                kd_ipc_debug("Write failed: send return 0 when size > 0");
                return false; // Failed
            }
            if (writeRes < 0 && errno == EINTR) { // Interrupted by os
                continue; // Continue
            }

            // Write: writeRes < 0
            // No data could be written and need to wait
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfds[2];
                pfds[0].fd = socketFd;
                pfds[0].events = POLLOUT;
                pfds[1].fd = interruptFd;
                pfds[1].events = POLLIN;
                
                // Poll
                int pollRes = -1;
                do {
                    pollRes = ::poll(pfds, 2, -1);
                } while (pollRes == -1 && errno == EINTR);
                
                // Check result
                if (!(pfds[0].revents & POLLOUT) || m_delegate->isQuit()) {
                    kd_ipc_debug("Write failed: Socket broken/interrupted: res = %d, sockevt = %d, quit = %d", 
                        pollRes, pfds[0].revents, m_delegate->isQuit());
                    return false; // Failed
                }
                
                // Continue to write
                continue; 
            } else {
                kd_ipc_debug("Write failed: other error %d", errno);
                return false; // Failed
            }
        }

        // Finally
		bool success = (totalWritten == size);
		if (!success) {
			kd_ipc_debug("Write unfinished: partial data writteb (%u/%u). Quit signaled: %d",
				totalWritten, size, m_delegate->isQuit());
		}

        return success;
    }

private:
	// Open server
    bool _openServer(const std::function<void()>& aboutToConnect,
                     const std::function<void(bool)>& didConnected) {
        if (!ipc_detail::checkSocketPath(m_socketPath)) {
            kd_ipc_debug("Socket path length is limited to 1024");
            KD_ASSERT_M(false, "Socket path length is limited to 1024");
            return false; // Failed
        }

        // Check socket lock and fd
        int interruptFd = -1;
        {
            std::lock_guard<IPCMutex> lock(m_connMtx);
			if (m_interruptPipe[0] < 0) {
                kd_ipc_debug("Interrupt pipe error");
				return false; // Failed
			}

            // Because the bind operation will fail if the socket file exists, 
            // we use a file lock to check whether the current socket file can 
            // be safely deleted before binding.
            if (!_acquireSocketLockNoLock()) {
                kd_ipc_debug("Failed to acquire socket lock");
                KD_ASSERT_M(false, "Failed to acquire socket lock");
                return false; // Failed
            }

            interruptFd = m_interruptPipe[0];
        }

		// Automatically release the Socket Lock by RAII.
        kd::ScopeGuard sockLockGuard([this]() {
            std::lock_guard<IPCMutex> lock(m_connMtx);
            this->_releaseSocketLockNoLock();
        });

		// Create UDS
        int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listenFd == -1) {
            kd_ipc_debug("Failed to create socket");
            return false; // Failed
        }
        ipc_detail::setNonBlocking(listenFd);
		kd::ScopeGuard listenFdGuard([listenFd]() {
			::close(listenFd);
		});
		
		// Prepare to bind
        size_t pathLen = m_socketPath.size();
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        ::memcpy(addr.sun_path, m_socketPath.data(), pathLen);
        
		// Bind and listen (socket file created after binding)
        socklen_t addrLen = ipc_detail::addressLength(m_useAbstractName, pathLen);
        if (::bind(listenFd, (struct sockaddr*)&addr, addrLen) == -1 || ::listen(listenFd, 5) == -1) {
            kd_ipc_debug("Failed to bind socket");
            return false; // Failed
        }
		if (m_delegate->isQuit()) {
            kd_ipc_debug("Open failed: quit by delegate");
            return false; // Failed
        }

		// Prepare to connect
        struct pollfd pfds[2];
        pfds[0].fd = listenFd;
        pfds[0].events = POLLIN;
		pfds[1].fd = interruptFd;
		pfds[1].events = POLLIN;

        if (aboutToConnect) {
            aboutToConnect(); 
        }

		// Waiting for a client connection (poll)
        int pollRes = ::poll(pfds, 2, -1);

		// Check poll result
        bool connected = false;
        if (pollRes > 0 && (pfds[0].revents & POLLIN) && !m_delegate->isQuit()) {
			struct sockaddr_un clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);

			// Accept a client
            int clientFd = ::accept(listenFd, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientFd >= 0) {
				if (!m_delegate->isQuit()) {
                    ipc_detail::setNonBlocking(clientFd);
                    ipc_detail::disableSigPipe(clientFd);

					std::lock_guard<IPCMutex> lock(m_connMtx);
                    m_socketFd = clientFd;
                    connected = true; // Connected
				}
                
				if (!connected) {
					::close(clientFd);
				}
            }	
        }

		// Finally
        if (connected) {
            // Cancel auto-releasing socket lock
            sockLockGuard.cancel();
        }
        if (didConnected) {
            didConnected(connected);
        }

        return connected;
    }

	// Open client
    bool _openClient(const std::function<void()>& aboutToConnect,
                     const std::function<void(bool)>& didConnected) {
        if (!ipc_detail::checkSocketPath(m_socketPath)) {
            kd_ipc_debug("Socket path length is limited to 1024");
            KD_ASSERT_M(false, "Socket path length is limited to 1024");
            return false; // Failed
        }
        
        // Check interrupt fd
        int interruptFd = -1;
        {
            std::lock_guard<IPCMutex> lock(m_connMtx);
			if (m_interruptPipe[0] < 0) {
                kd_ipc_debug("Interrupt pipe error");
				return false; // Failed
			}
            interruptFd = m_interruptPipe[0];
        }

        //  Check if socket file is ready on non-Linux OS
        if (!m_useAbstractName && ::access(m_socketPath.c_str(), F_OK) != 0) {
            kd_ipc_debug("The server socket is not ready yet");
            return false; // Failed
        }
        if (m_delegate->isQuit()) {
            kd_ipc_debug("Connect failed: quit by delegate");
            return false; // Failed
        }

        // Create UDS
        int clientFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (clientFd == -1) {
            kd_ipc_debug("Failed to create socket");
            return false; // Failed
        }
        ipc_detail::setNonBlocking(clientFd);
        ipc_detail::disableSigPipe(clientFd);
        kd::ScopeGuard clientFdGuard([clientFd]() {
			::close(clientFd); // Auto close fd by RAII
		});

        // Prepare to connect
        size_t pathLen = m_socketPath.size();
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        ::memcpy(addr.sun_path, m_socketPath.data(), pathLen);
        if (aboutToConnect) {
            aboutToConnect(); 
        }

        // Connecting
        bool connected = false;
        socklen_t addrLen = ipc_detail::addressLength(m_useAbstractName, pathLen);
        int ret = ::connect(clientFd, (struct sockaddr*)&addr, addrLen);

        // Check if connected
        if (ret == 0) { // Connected
            if (!m_delegate->isQuit()) {
                connected = true;
            }
        } else if (ret == -1 && errno == EINPROGRESS) { // Waiting to connect
            struct pollfd pfds[2];
            pfds[0].fd = clientFd;
            pfds[0].events = POLLOUT; // Socket is ready to be written
            pfds[1].fd = interruptFd;
            pfds[1].events = POLLIN;

            // Poll 1000 ms
            int pollRes = ::poll(pfds, 2, 1000);

            // Check polling result
            if (pollRes > 0 && (pfds[0].revents & POLLOUT) && !m_delegate->isQuit()) {
                // If the connection is refused, the kernel will still make the socket 
                // writable (triggering POLLOUT, and some systems may even trigger 
                // POLLIN or POLLERR). Therefore, we must use getsockopt to check 
                // whether the connection was actually successful.
                int socketErr = 0;
                socklen_t errLen = sizeof(socketErr);
                if (::getsockopt(clientFd, SOL_SOCKET, SO_ERROR, &socketErr, &errLen) == 0 && socketErr == 0) {
                    if (!m_delegate->isQuit()) {
                        connected = true;
                    }
                } else {
                    kd_ipc_debug("Connect failed via SO_ERROR, socketErr = %d", socketErr);
                }
            } else {
                kd_ipc_debug("Connect failed after polling, res = %d", pollRes);
            }
        } else {
            kd_ipc_debug("Connect failed immediately, err = %d", errno);
        }

        // Finally
        if (connected) {
            // Cancel auto-releasing fd
            clientFdGuard.cancel();
            {
                std::lock_guard<IPCMutex> lock(m_connMtx);
                m_socketFd = clientFd;
            }
        }
        if (didConnected) {
            didConnected(connected);
        }

        return connected;
    }

	// Acquire socket lock by server without lock
	// to prevent socket files from being deleted
    bool _acquireSocketLockNoLock() {
        if (m_useAbstractName || !m_isServer) {
            return true; 
        }
        if (m_socketLockFd >= 0) {
            return true; // Already locked
        }

        int lockFd = ::open(m_socketLockPath.c_str(), O_RDWR | O_CREAT, 0666);
        if (lockFd == -1) {
			return false;
		}

        struct flock fl{};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0; // Whole file

		// Try to lock
		// Locked means the socket file can be safely deleted
        if (::fcntl(lockFd, F_SETLK, &fl) == -1) {
            ::close(lockFd);
            return false; // Lock failed
        }

        // Assign socket lock file and delete socket file
        m_socketLockFd = lockFd;
        ::unlink(m_socketPath.c_str());

        return true;
    }

	// Release socket lock by server without lock
	// when socket is closed
    void _releaseSocketLockNoLock() {
		if (m_useAbstractName || !m_isServer) {
            return; 
        }

		// Deleted the socket file
        ::unlink(m_socketPath.c_str());

        // Close the socket lock file
        if (m_socketLockFd >= 0) {
            struct flock fl{};
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            ::fcntl(m_socketLockFd, F_SETLK, &fl);

            ::close(m_socketLockFd);
            m_socketLockFd = -1;
            // Do not unlink socket lock file here
        }
    }

    void _cleanUpSocket() {
        // Clear interrupt pipe
        ipc_detail::clearPipe(m_interruptPipe[0]);

		{
            std::lock_guard<IPCMutex> lock(m_connMtx);
            // Close socket
            if (m_socketFd >= 0) {
                ::shutdown(m_socketFd, SHUT_RDWR);
                ::close(m_socketFd);
                m_socketFd = -1;
            }

            // Only server need to release the socket lock
            if (m_isServer) {
                _releaseSocketLockNoLock();
            }
        }		
    }

private:
    std::string m_socketPath;
    std::string m_socketLockPath;
    bool        m_useAbstractName{ false };

    int         m_socketFd{ -1 };
    int         m_socketLockFd{ -1 };
    int         m_interruptPipe[2]{ -1, -1 }; 
};

__NAMESPACE_KD_END

#endif // KD_OS_UNIX
