/** **************************************************************************

Created by: Arlin (arlins.dps@gmail.com).

IPCConnection
IPCConnection is a cross-platform IPC library .It can be used in any threads,
and all APIs can be called from any threads without blocking. But it is
recommended to use it in a single-threaded, especially the start/stop APIs.

Implementations
On Windows, IPCConnection is implemented based on asynchronous pipes using byte streams.
On Unix, IPCConnection is implemented based on UDS (Unix Domain Sockets).
Specifically, UDS is based on Abstract Namespace addressing on Android/Linux.
On other Unix-like systems (e.g. iOS/macOS), UDS is based on Pathname addressing.

IPCConnectionHandler
All connection handlers callbacks will be called on an internal worker thread.
Please switch threads at their respective callback interfaces if necessary.
If you need to switch to the main thread, IPCPerformOnMainThread API is preferred.

************************************************************************** **/

#pragma once
#include "koda/ipc/kd_ipcdefs.h"
#if defined(KD_OS_WIN)
#include "koda/ipc/kd_ipcimpl_win32.h"
#elif defined(KD_OS_UNIX) 
#include "koda/ipc/kd_ipcimpl_unix.h"
#else
#include "koda/ipc/kd_ipcimpl_dummy.h"
#endif

//
__NAMESPACE_KD_BEGIN

// IPCState
enum class IPCState {
	Initializing,
	Preparing,
	WaitingForConnection,
	Connecting,
	Connected,
	ConnectionFailed,
	Disconnected,
	Stopping,
	Stopped
};

// Perform action on main-thread 
inline void IPCPerformOnMainThread(std::function<void()> func) {
	PerformOnMainThread(std::move(func));
}

// Convert state to string
inline std::string IPCStateToString(IPCState state) {
	if (state == IPCState::Stopped) { return "Stopped"; }
	if (state == IPCState::Stopping) { return "Stopping"; }
	if (state == IPCState::Initializing) { return "Initializing"; }
	if (state == IPCState::Preparing) { return "Preparing"; }
	if (state == IPCState::WaitingForConnection) { return "WaitingForConnection"; }
	if (state == IPCState::Connecting) { return "Connecting"; }
	if (state == IPCState::Connected) { return "Connected"; }
	if (state == IPCState::ConnectionFailed) { return "ConnectionFailed"; }
	if (state == IPCState::Disconnected) { return "Disconnected"; }
	return "UnknownState";
}

//============================
// IPCConnectionHandler 
// All the callbacks will be called on connection working thread.
// Using IPCPerformOnMainThread to switch to main-thread if needed
//============================
class IPCConnection;
class IPCConnectionHandler {
public:
	virtual ~IPCConnectionHandler() {}
	virtual void onIPCConnectionRecvData(std::shared_ptr<IPCConnection> conn, std::shared_ptr<std::vector<char>> data) {};
	virtual void onIPCConnectionStateChanged(std::shared_ptr<IPCConnection> conn, IPCState state) {};
};

//============================
// IPCConnection
//============================
class IPCConnection : public IPCDelegate, public std::enable_shared_from_this<IPCConnection> {
	static constexpr uint32_t kMaxPackageSize = 1024 * 1024 * 1024; // 1GB

public:
	static std::shared_ptr<IPCConnection> create(
		const std::string& channel, bool isServer, bool autoReconnect,
		unsigned int readBufferSize = 1024 * 1024,
		unsigned int writeBufferSize = 1024 * 1024,
		unsigned int maxPacketSize = 1000) {
		std::shared_ptr<IPCConnection> conn(new IPCConnection());
		conn->init(channel, isServer, autoReconnect, readBufferSize, writeBufferSize, maxPacketSize);
		return conn;
	}

private:
	IPCConnection()
	{}

	void init(const std::string& channel, bool isServer, bool autoReconnect,
		unsigned int readBufferSize, unsigned int writeBufferSize, unsigned int maxPacketSize) {
		m_autoReconnect = autoReconnect;
		m_maxPacketSize = maxPacketSize;

#if defined(KD_OS_WIN)
		m_connImpl = std::make_unique<IPCConnectionImplWin>(this, channel, isServer, readBufferSize, writeBufferSize);
#elif defined(KD_OS_UNIX) 
		m_connImpl = std::make_unique<IPCConnectionImplUnix>(this, channel, isServer, readBufferSize, writeBufferSize);
#else 
		m_connImpl = std::make_unique<IPCConnectionImplDummy>(this, channel, isServer, readBufferSize, writeBufferSize);
#endif
	}

public:
	virtual ~IPCConnection() {
		stop();
	}

public:
	void addHandler(std::weak_ptr<IPCConnectionHandler> handler) {
		if (handler.expired()) {
			return;
		}

		std::lock_guard<IPCMutex> lock(m_handlerMtx);
		for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it) {
			if (kd::is_same_weak_ptr(*it, handler))
				return; // Already exists
		}
		m_handlers.push_back(handler);
	}

	void removeHandler(std::weak_ptr<IPCConnectionHandler> handler) {
		if (handler.expired()) {
			return;
		}

		std::lock_guard<IPCMutex> lock(m_handlerMtx);
		auto it = m_handlers.begin();
		while (it != m_handlers.end()) {
			if (it->expired() || kd::is_same_weak_ptr(*it, handler)) {
				// Remove expired handlers or matched handler
				it = m_handlers.erase(it);
			} else {
				++it;
			}
		}
	}

	std::string name() {
		return m_connImpl->name();
	}

	bool isServer() {
		return m_connImpl->m_isServer;
	}

	bool isConnected() {
		return m_state.load() == IPCState::Connected;
	}

	bool canStart() {
		return m_state.load() == IPCState::Stopped;
	}

	bool canStop() {
		auto s = m_state.load();
		return s != IPCState::Stopped && s != IPCState::Stopping;
	}

	IPCState connectionState() {
		return m_state.load();
	}

public:
	// Start the connection
	bool start() {
		{
			std::lock_guard<IPCMutex> lock(m_connImpl->m_connMtx);
			if (m_state != IPCState::Stopped) {
				kd_ipc_debug("[ERROR] Unable to start when state is %s",
					IPCStateToString(m_state.load()).c_str());
				return false;
			}

			m_state = IPCState::Initializing;
			m_quit = false;
			kd_ipc_debug("Changing state = %s", IPCStateToString(m_state.load()).c_str());
		}
		_onIPCConnectionStateChanged(IPCState::Initializing);
		{
			std::lock_guard<std::mutex> lock(m_sendQueueMtx);
			m_sendQueue.clear();
		}

		// Create a queue with one thread to execute the working loop. 
		// When the working loop exits, the queue will automatically destroyed.
		auto self = shared_from_this();
		auto queue = OperationQueue::createShared(1);
		queue->addOperation([this, self] { _mainLoop(); });
		queue->setAutoDestroyedAfterAllOpsDone(true);

		return true;
	}

	// Stop the connection
	// Calling `stop` will asynchronously stopping the connection.
	// Initiating `start` immediately after `stop` will fail. The correct approach 
	// is to listen for `Stopped`  in `onIPCConnectionStateChanged` in handler
	// and then call `start` to restart the connection.
	void stop() {
		{
			std::lock_guard<IPCMutex> lock(m_connImpl->m_connMtx);
			if (m_state == IPCState::Stopped || m_state == IPCState::Stopping) {
				kd_ipc_debug("[ERROR] Unable to stop when state is %s",
					IPCStateToString(m_state.load()).c_str());
				return;
			}

			m_state = IPCState::Stopping;
			m_quit = true;
			kd_ipc_debug("Changing state = %s", IPCStateToString(m_state.load()).c_str());
		}
		_onIPCConnectionStateChanged(IPCState::Stopping);

		m_connImpl->interrupt();
		{	// Wake up and Quit the send loop
			std::lock_guard<std::mutex> lock(m_sendQueueMtx);
			m_sendQueue.clear();
			m_sendQueueCv.notify_all();
		}
	}

	void send(std::vector<char>&& data) {
		if (data.size() == 0 || m_quit) {
			return;
		}
		if (data.size() > kMaxPackageSize) {
			kd_ipc_debug("[ERROR] Package exceeds maximum size limit: %u bytes", data.size());
			return;
		}
		if (!m_connImpl->ready()) {
			kd_ipc_debug("[ERROR] Failed to send data when connection is not ready");
			return;
		}

		{
			std::lock_guard<std::mutex> lock(m_sendQueueMtx);
			// Reduce the existing data to half of the remaining maximum.
			if (m_maxPacketSize > 2 && m_sendQueue.size() >= m_maxPacketSize) {
				size_t keepCount = m_maxPacketSize / 2;
				size_t numToDelete = m_sendQueue.size() - keepCount;
				m_sendQueue.erase(m_sendQueue.begin(), m_sendQueue.begin() + numToDelete);
			}
			m_sendQueue.push_back(std::move(data));
		}
		m_sendQueueCv.notify_one();
	}

	void send(const std::vector<char>& _data) {
		std::vector<char> data = _data;
		send(std::move(data));
	}

	void send(const char* data_ptr, size_t size) {
		if (data_ptr && size > 0) {
			std::vector<char> data(size);
			memcpy(data.data(), const_cast<char*>(data_ptr), size);
			send(std::move(data));
		}
	}

	void send(const std::string& s) {
		if (s.length() > 0) {
			std::vector<char> data(s.begin(), s.end());
			send(std::move(data));
		}
	}

private:
	// Main working loop
	void _mainLoop() {
		while (!m_quit) {
			// Preparing and connecting
			_updateStateWithLock(IPCState::Preparing);
			bool opened = m_connImpl->open(
				[this] {
				if (this->isServer()) {
					this->_updateStateWithLock(IPCState::WaitingForConnection);
				} else {
					this->_updateStateWithLock(IPCState::Connecting);
				}
			},
				[this](bool connected) {
				if (connected) {
					this->_updateStateWithLock(IPCState::Connected);
				} else {
					this->_updateStateWithLock(IPCState::ConnectionFailed);
				}
			}); // open

			if (m_quit) {
				if (opened) { m_connImpl->close(); }
				break; // Quit the loop
			}
			if (!opened) {
				int delayMs = (isServer() ? 200 : 500);
				std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
				continue; // Retry
			}

			// Execute the session
			_runSession();

			// Session stopped, close the connection
			_updateStateWithLock(IPCState::Disconnected);
			m_connImpl->close();

			// Quit the loop or reconnect
			if (m_autoReconnect && !m_quit) {
				// Delayed reconnect
				kd_ipc_debug("About to reconnect");
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			} else {
				_updateStateWithLock(IPCState::Stopping);
				break;  // Quit the loop
			}
		}

		// Quit
		m_quit = false;
		_updateStateWithLock(IPCState::Stopped);
		kd_ipc_debug("Stopped...");
	}

	// Run read and write session
	void _runSession() {
		kd_ipc_debug("Enter session");

		// Reset the flag of quitting send loop 
		// before entering the send loop
		m_quitSendLoop = false;

		// Start receiving and sending loop in session.
		// Regardless of whether the receiving loop or the sending loop exits, 
		// the other loop must also exit, so that the worker thread can exit normally.
		std::thread sendThread(&IPCConnection::_runSendLoop, this);
		_runReceiveLoop();
		_quitSendLoop();  // Ensure exiting the sending loop
		if (sendThread.joinable()) {
			sendThread.join();
		}

		kd_ipc_debug("Leave session");
	}
	// Send data loop
	void _runSendLoop() {
		kd_ipc_debug("Enter sending loop");
		while (!m_quit) {
			// Read all data packets
			std::vector<std::vector<char>> copySendQueue;
			{
				std::unique_lock<std::mutex> lock(m_sendQueueMtx);
				m_sendQueueCv.wait(lock, [this] {
					return !m_sendQueue.empty() || m_quit || m_quitSendLoop; });
				if (m_quit || m_quitSendLoop) {
					break;
				}
				m_sendQueue.swap(copySendQueue);
			}

			// Write all data to peer
			bool errorOccurred = false;
			for (auto& data : copySendQueue) {
				uint32_t packetSize = (uint32_t)data.size();

				// Write data header. Failure indicates the connection was broken
				errorOccurred = !m_connImpl->write(&packetSize, sizeof(packetSize));
				if (errorOccurred || m_quit) {
					break;
				}

				// Write the data. Failure indicates the connection was broken
				errorOccurred = !m_connImpl->write(data.data(), (uint32_t)data.size());
				if (errorOccurred || m_quit) {
					break;
				}
			}

			// Quit the loop：Connection was broken or requesting quit.
			if (m_quit || m_quitSendLoop || errorOccurred) {
				break;
			}
		}

		// Quit
		m_connImpl->interrupt(); // Ensure exiting the receiving loop
		kd_ipc_debug("Leave sending loop");
	}

	// Receive data loop
	void _runReceiveLoop() {
		kd_ipc_debug("Enter receiving loop");

		while (!m_quit) {
			uint32_t packetSize = 0;
			bool errorOccurred = false;

			// Read data header. Failure indicates the connection was broken
			errorOccurred = !m_connImpl->read(&packetSize, sizeof(packetSize));
			if (m_quit || errorOccurred) {
				break;
			}
			if (packetSize > kMaxPackageSize) {
				kd_ipc_debug("[ERROR] Package exceeds maximum size limit: %u bytes", packetSize);
				break;
			}

			// Read the data. Failure indicates the connection was broken
			auto buffer = ::kd::make_shared<std::vector<char>>(packetSize);
			errorOccurred = !m_connImpl->read(buffer->data(), packetSize);
			if (m_quit || errorOccurred) {
				break;
			}

			// Execute callback of handlers
			_onIPCConnectionRecvData(buffer);
		}

		// Quit
		kd_ipc_debug("Leave receiving loop");
	}

	void _quitSendLoop() {
		kd_ipc_debug("Request to quit send loop");
		{
			std::unique_lock<std::mutex> lock(m_sendQueueMtx);
			m_quitSendLoop = true;
		}
		m_sendQueueCv.notify_all();
	}

	// Setup state, it will lock connMtx inside.
	void _updateStateWithLock(IPCState state) {
		bool changed = false;
		{
			std::lock_guard<IPCMutex> lock(m_connImpl->m_connMtx);

			// Keep the state when the currently state is stopping,
			// Avoid changing state while stopping is processing
			bool keepState = (m_state.load() == IPCState::Stopping && state < IPCState::Stopping);
			if (keepState) {
				kd_ipc_debug("Keep state %s  while changing to %s",
					IPCStateToString(m_state.load()).c_str(), IPCStateToString(state).c_str());
				return; // Keep state
			}

			if (m_state.load() != state) {
				kd_ipc_debug("Changing state, new = %s, old = %s", IPCStateToString(state).c_str(),
					IPCStateToString(m_state.load()).c_str());
				m_state = state;
				changed = true;
			}
		}

		if (changed) {
			_onIPCConnectionStateChanged(state);
		}
	}

private: // IPCConnectionHandler Callbacks
	std::vector<std::shared_ptr<IPCConnectionHandler>> _getActiveHandlers() {
		std::vector<std::shared_ptr<IPCConnectionHandler>> activeHandlers;
		{
			std::lock_guard<IPCMutex> lock(m_handlerMtx);
			auto it = m_handlers.begin();
			while (it != m_handlers.end()) {
				if (auto sptr = it->lock()) {
					activeHandlers.push_back(sptr);
					++it;
				} else {
					it = m_handlers.erase(it);
				}
			}
		}

		return activeHandlers;
	}

	void _onIPCConnectionRecvData(std::shared_ptr<std::vector<char>> data) {
		auto activeHandlers = _getActiveHandlers();
		auto self = shared_from_this();
		for (auto& handler : activeHandlers) {
			if (handler) {
				handler->onIPCConnectionRecvData(self, data);
			}
		}
	};

	void _onIPCConnectionStateChanged(IPCState state) {
		auto activeHandlers = _getActiveHandlers();
		auto self = shared_from_this();
		for (auto& handler : activeHandlers) {
			if (handler) {
				handler->onIPCConnectionStateChanged(self, state);
			}
		}
	};

private: // IPCDelegate
	bool isQuit() override {
		return m_quit.load();
	};

private:
	bool m_autoReconnect{ true };
	std::atomic<IPCState> m_state{ IPCState::Stopped };
	std::atomic<bool> m_quit{ false };
	std::atomic<bool> m_quitSendLoop{ false };
	std::unique_ptr<IPCConnectionImpl> m_connImpl{ nullptr };

	unsigned int m_maxPacketSize{ 1000 };
	std::vector<std::vector<char>> m_sendQueue;
	std::mutex m_sendQueueMtx;
	std::condition_variable m_sendQueueCv;

	std::vector<std::weak_ptr<IPCConnectionHandler>> m_handlers;
	IPCMutex m_handlerMtx;
};

__NAMESPACE_KD_END
